#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ds.h"
#include "jemalloc/jemalloc.h"

void pmtbundle_print(PMTBundle* p)
{
    printf("PMTBundle at %p:\n", p);
    printf("  pmtid =  %i:\n", p->pmtid);
    printf("  gtid  =  %i:\n", p->gtid);
    int i;
    for(i=0; i<3; i++)
        printf("  word%i =  %u:\n", i, p->word[i]);
}

Buffer* buffer_alloc(Buffer** pb)
{
    int sz = sizeof(Buffer);
    *pb = malloc(sz);
    if(*pb)
    {
        printf("Initializing buffer: keys[%d] (%d)\n", BUFFER_SIZE, sz);
        memset(*pb, 0, sizeof(Buffer));
        (*pb)->size = BUFFER_SIZE;
        (*pb)->end = 0;
        (*pb)->start  = 0;
        (*pb)->offset = 0;
    }
    return *pb;
}
 
int buffer_isfull(Buffer* b)
{
    return (((b->end + 1) % b->size) == b->start);
}
 
int buffer_isempty(Buffer* b)
{
    return (b->start == b->end);
}
 
int buffer_push(Buffer* b, Event* key)
{
    int full = buffer_isfull(b);
    if(!full)
    {
        b->keys[b->end] = key;
        b->end++;
        b->end %= b->size;
    }
    return !full;
}
 
int buffer_pop(Buffer* b, Event* pk)
{
    int empty = buffer_isempty(b);
    if(!empty)
    {
        pk = b->keys[b->start];
        pthread_mutex_t m;
        if(pk) {
            m = b->keys[b->start]->mutex;
            pthread_mutex_lock(&m);
        }
        free(b->keys[b->start]);
        b->keys[b->start] = NULL;
        if(pk)
            pthread_mutex_unlock(&m);
        b->start++;
        b->start %= b->size;
    }
    return !empty;
}

void buffer_status(Buffer* b)
{
    printf("Buffer at %p:\n", b);
    printf("  write: %lu, read: %lu, full: %d, empty: %d\n", b->end,
                                                             b->start,
                                                             buffer_isfull(b),
                                                             buffer_isempty(b));
}

void buffer_clear(Buffer* b)
{
    int i;
    for(i=0; i<b->size; i++)
        if(b->keys[i]) {
            pthread_mutex_t m = b->keys[i]->mutex;
            pthread_mutex_lock(&m);
            //event_clear(b->keys[i]);
            free(b->keys[i]);
            b->keys[i] = NULL;
            pthread_mutex_unlock(&m);
        }
    b->end = 0;
    b->start = 0;
}

int buffer_at(Buffer* b, unsigned int id, Event** pk)
{
    int keyid = (id - b->offset) % b->size;
    if (keyid < b->size) {
        *pk = b->keys[keyid];
        return pk == NULL ? 1 : 0;
    }
    else
        return 1;
}

int buffer_insert(Buffer* b, unsigned int id, Event* pk)
{
    int keyid = (id - b->offset) % b->size;
    if (!b->keys[keyid] && (keyid < b->size)) {
        pthread_mutex_t m = b->mutex;
        pthread_mutex_lock(&m);
        b->keys[keyid] = pk;
        if(keyid > b->end) {
            b->end = keyid;
            b->end %= b->size;
        }
        pthread_mutex_unlock(&m);
        return 0;
    }
    else
        return 1;
}

