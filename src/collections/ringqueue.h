/**
 * @file ringqueue.h
 * @author FR
 */

#ifndef RINGQUEUE_H
#define RINGQUEUE_H

#include <stddef.h>

typedef struct {
	size_t read; // end or start
	size_t write; // end or start
	size_t len;
	size_t cap;
	void** data;
} RingQueue;

void ringqueue_init(RingQueue* q, size_t cap);
void ringqueue_destroy(RingQueue* q);

#define ringqueue_empty(q) ((q)->len == 0)

// memory is not managed by the ringqueue!
int ringqueue_enqueue(RingQueue* q, const void* val);
void* ringqueue_dequeue(RingQueue* q);

#endif
