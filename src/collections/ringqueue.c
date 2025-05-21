/**
 * @file ringqueue.c
 * @author FR
 */

#include "ringqueue.h"

#include <stdlib.h>
#include <string.h>
#include <arith.h>

#define RING_QUEUE_DEFAULT_CAPACITY 16

void ringqueue_init(RingQueue* q, size_t cap) {
	q->read = 0;
	q->write = 0;
	q->len = 0;
	q->cap = cap > 0 ? cap : RING_QUEUE_DEFAULT_CAPACITY;
	q->data = NULL;
}

void ringqueue_destroy(RingQueue* q) {
	if(q->data)
		free(q->data);
}

static int ringqueue_resize(RingQueue* q, size_t min_cap) {
	void **old_data = q->data, **new_data;

	size_t old_cap = q->cap;
	size_t new_cap;

	if(old_data && old_cap > 0) {
		new_cap = NEW_LEN(old_cap, min_cap - old_cap, min_cap >> 1);

		// realloc is sufficient
		if(q->write == 0) {
			new_data = realloc(old_data, new_cap * sizeof(void*));
			if(!new_data)
				return -1;

			q->write = old_cap;
			q->cap = new_cap;
			q->data = new_data;
		}
		else { // copying needed
			new_data = malloc(new_cap * sizeof(void*));
			if(!new_data)
				return -1;

			size_t upper_len = old_cap - q->read; // length from start to the cap, "upper" values
			memcpy(new_data, old_data + q->read, upper_len * sizeof(void*));
			memcpy(new_data + upper_len, old_data, (q->len - upper_len) * sizeof(void*));

			free(old_data);

			q->read = 0;
			q->write = q->len;
			q->cap = new_cap;
			q->data = new_data;
		}
	}
	else {
		new_cap = MAX(RING_QUEUE_DEFAULT_CAPACITY, min_cap);

		new_data = malloc(new_cap * sizeof(void*));
		if(!new_data)
			return -1;

		q->cap = new_cap;
		q->data = new_data;
	}

	return 0;
}

int ringqueue_enqueue(RingQueue* q, const void* val) {
	if(!q->data || q->len == q->cap) {
		if(ringqueue_resize(q, q->len + 1) < 0)
			return -1;
	}

	q->data[q->write++] = (void*) val;

	if(q->write == q->cap)
		q->write = 0;

	q->len++;
	return 0;
}

void* ringqueue_dequeue(RingQueue* q) {
	if(q->len == 0)
		return NULL;

	void* val = q->data[q->read++];

	if(q->read == q->cap)
		q->read = 0;

	q->len--;
	return val;
}
