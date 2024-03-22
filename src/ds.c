#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <jemalloc/jemalloc.h>
#include <evb/ds.h>

extern Event* events;


uint32_t get_bits(uint32_t x, uint32_t position, uint32_t count)
{
  uint32_t shifted = x >> position;
  uint32_t mask = ((uint64_t)1 << count) - 1;
  return shifted & mask;
}

/*
void pmtbundle_print(PMTBundle* p)
{
    printf("PMTBundle at %p:\n", (void*) p);
    printf("  pmtid =  %i:\n", pmtbundle_pmtid(p));
    printf("  gtid  =  %i:\n", pmtbundle_gtid(p));
    int i;
    for(i=0; i<3; i++)
        printf("  word%i =  %u:\n", i, p->word[i]);
}

uint32_t pmtbundle_gtid(PMTBundle* p)
{
    uint32_t gtid1 = get_bits(p->word[0], 0, 16);
    uint32_t gtid2 = get_bits(p->word[2], 12, 4);
    uint32_t gtid3 = get_bits(p->word[2], 28, 4);
    return (gtid1 + (gtid2<<16) + (gtid3<<20));
}

uint32_t pmtbundle_pmtid(PMTBundle* p)
{
    int ichan = get_bits(p->word[0], 16, 5);
    int icard = get_bits(p->word[0], 26, 4);
    int icrate = get_bits(p->word[0], 21, 5);
    return (512*icrate + 32*icard + ichan);
}
*/

Buffer* buffer_alloc(Buffer** pb, int size)
{
    *pb = (Buffer*) malloc(sizeof(Buffer));
    (*pb)->keys = (void**) malloc(size * sizeof(void*));
    (*pb)->type = (RecordType*) malloc(size * sizeof(RecordType));
    (*pb)->mutex_buffer = (pthread_mutex_t*) malloc(size* sizeof(pthread_mutex_t));
    int mem_allocated = sizeof(Buffer) + size * (sizeof(void*) + sizeof(RecordType) + sizeof(pthread_mutex_t));
    if(*pb) {
        printf("Initializing buffer: keys[%d] (%d KB allocated)\n", size, mem_allocated/1000);
        bzero(*pb, sizeof(Buffer));
        (*pb)->size = size;
        (*pb)->write = 0;
        (*pb)->read  = 0;
        (*pb)->offset = 0;

        int i;
        for(i=0; i<(*pb)->size; i++)
            pthread_mutex_init(&((*pb)->mutex_buffer[i]), NULL);
        pthread_mutex_init(&((*pb)->mutex_write), NULL);
        pthread_mutex_init(&((*pb)->mutex_read), NULL);
        pthread_mutex_init(&((*pb)->mutex_offset), NULL);
        pthread_mutex_init(&((*pb)->mutex_size), NULL);
    }
    return *pb;
}
 
inline int buffer_isfull(Buffer* b)
{
    return (((b->write + 1) % b->size) == b->read);
}
 
inline int buffer_isempty(Buffer* b)
{
    return (b->read == b->write);
}
 
int buffer_push(Buffer* b, RecordType type, void* key)
{
    pthread_mutex_lock(&(b->mutex_write));
    pthread_mutex_lock(&(b->mutex_buffer[b->write]));
    uint64_t write_old = b->write;
    int full = buffer_isfull(b);
    if(!full)
    {
        b->keys[b->write] = key;
        b->type[b->write] = type;
        b->write++;
        b->write %= b->size;
    }
    pthread_mutex_unlock(&(b->mutex_write));
    pthread_mutex_unlock(&(b->mutex_buffer[write_old]));
    return !full;
}

int buffer_pop(Buffer* b, RecordType* type, void** pk)
{
    pthread_mutex_lock(&(b->mutex_read));
    pthread_mutex_lock(&(b->mutex_buffer[b->read]));
    uint64_t read_old = b->read;
    int empty = buffer_isempty(b);
    if(!empty)
    {
        (*pk) = b->keys[b->read];
        (*type) = b->type[b->read];
        b->keys[b->read] = NULL;
        b->type[b->read] = (RecordType) 0;
        b->read++; // note: you can pop a NULL pointer off the end
        b->read %= b->size;
    }
    pthread_mutex_unlock(&(b->mutex_read));
    pthread_mutex_unlock(&(b->mutex_buffer[read_old]));
    return !empty;
}

void buffer_status(Buffer* b)
{
    printf("Buffer at %p:\n", (void*) b);
    printf("  write: %lu, read: %lu, full: %d, empty: %d\n", b->write,
                                                             b->read,
                                                             buffer_isfull(b),
                                                             buffer_isempty(b));
}

uint64_t buffer_keyid(Buffer* b, unsigned int id)
{
    return (id - b->offset) % b->size;
}

void buffer_clear(Buffer* b)
{
    pthread_mutex_lock(&(b->mutex_write));
    pthread_mutex_lock(&(b->mutex_read));
    int i;
    for(i=0; i<b->size; i++)
        pthread_mutex_lock(&(b->mutex_buffer[i]));

    for(i=0; i<b->size; i++) {
        if(b->keys[i] != NULL)
            free(b->keys[i]);
        b->keys[i] = NULL;
        b->type[i] = (RecordType) 0;
    }
    b->write = 0;
    b->read = 0;

    pthread_mutex_unlock(&(b->mutex_write));
    pthread_mutex_unlock(&(b->mutex_read));
    for(i=0; i<b->size; i++)
        pthread_mutex_unlock(&(b->mutex_buffer[i]));
}

int buffer_at(Buffer* b, unsigned int id, RecordType* type, void** pk)
{
    int keyid = buffer_keyid(b, id);
    if (keyid < b->size) {
        *type = b->type[keyid];
        *pk = b->keys[keyid];
        return pk == NULL ? 1 : 0;
    }
    else
        return 1;
}

int buffer_insert(Buffer* b, unsigned int id, RecordType type, void* pk)
{
    int keyid = buffer_keyid(b, id);
    if(!b->keys[keyid]) {
        b->type[keyid] = type;
        b->keys[keyid] = pk;
        b->write = keyid;
        b->write %= b->size;
        return 0;
    }
    else
        return 1;
}

Event* event_at(uint32_t key) {
  Event* s;
  HASH_FIND_INT(events, &key, s);
  //printf("event_at %i, size=%u\n", (int) key, HASH_COUNT(events));
  return s;
}

void event_push(uint32_t key, Event* s) {
  HASH_ADD_INT(events, timetag, s);
  //printf("event_push %i, size=%u\n", (int) key, HASH_COUNT(events));
}

Event* event_pop(uint32_t key) {
  Event* s = event_at(key);
  HASH_DEL(events, s);
  //printf("event_pop %i, size=%u\n", (int) key, HASH_COUNT(events));
  return s;
}
