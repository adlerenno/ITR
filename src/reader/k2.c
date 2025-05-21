/**
 * @file k2.c
 * @author FR
 */

#include "k2.h"

#include <stdlib.h>
#include <arith.h>
#include <reader.h>
#include <bitsequence_r.h>
#include <ringqueue.h>

K2Reader* k2_init(Reader* r) {
	size_t nbytes;
	uint64_t width = reader_vbyte(r, &nbytes);
	FileOff off = nbytes;

	uint64_t height = reader_vbyte(r, &nbytes);
	off += nbytes;

	uint64_t k = reader_vbyte(r, &nbytes);
	off += nbytes;

	uint64_t n = reader_vbyte(r, &nbytes);
	off += nbytes;

	if(!power_of(n, k))
		return NULL;
	if(width > n || height > n)
		return NULL;

	FileOff len_t = reader_vbyte(r, &nbytes);
	off += nbytes;

	K2Reader* k2 = malloc(sizeof(*k2));
	if(!k2)
		return NULL;

	k2->width = width;
	k2->height = height;
	k2->k = k;
	k2->n = n;

	if(len_t > 0) {
		Reader rt;
		reader_init(r, &rt, off);
		BitsequenceReader* t = bitsequence_reader_init(&rt);
		if(!t) {
			free(k2);
			return NULL;
		}

		// initialize reader for bits L
		reader_init(r, &rt, off + len_t);

		k2->t = t;
		k2->l = rt;
	}
	else
		k2->t = NULL; // k2->t == NULL means the matrix is empty

	return k2;
}

void k2_destroy(K2Reader* k) {
	if(k->t)
		bitsequence_reader_destroy(k->t);
	free(k);
}

bool k2_get(K2Reader* k, uint64_t r, uint64_t c) {
	if(r >= k->height || c >= k->width)
		return false;
	if(!k->t)
		return false;

	uint64_t n = k->n / k->k;
	uint64_t p = r % n;
	uint64_t q = c % n;
	uint64_t x = k->k * (r / n) + c / n;

	while(x < bitsequence_reader_len(k->t)) {
		if(!bitsequence_reader_access(k->t, x))
			return false;

		n /= k->k;

		x = bitsequence_reader_rank1(k->t, x) * (k->k * k->k) + k->k * (p / n) + q / n;

		p %= n;
		q %= n;
	}

	reader_bitpos(&k->l, x - bitsequence_reader_len(k->t));
	return reader_readbit(&k->l);
}

typedef struct {
	size_t len;
	size_t cap;
	uint64_t* data;
} IntList;

// low level uint64_t list with capacity
// used for the direct and reverse operations
int int_append(IntList* l, uint64_t i) {
	size_t cap = l->cap;

	if(cap == l->len) {
		cap = !cap ? 16 : (cap + (cap >> 1)); // default cap is 16 because the default rank is limited to 12
		uint64_t* data = realloc(l->data, cap * sizeof(*data));
		if(!data)
			return -1;

		l->cap = cap;
		l->data = data;
	}

	l->data[l->len++] = i;
	return 0;
}

// x is signed because it can be -1
static int k2reverse(K2Reader* k, uint64_t n, uint64_t q, uint64_t p, int64_t x, IntList* l) {
	if(p >= k->height)
		return 0;
	if(x >= (int64_t) bitsequence_reader_len(k->t)) { // Warning: comparing signed values
		reader_bitpos(&k->l, x - bitsequence_reader_len(k->t));
		if(reader_readbit(&k->l))
			if(int_append(l, p) < 0)
				return -1;
	}
	else {
		if(x == -1 || bitsequence_reader_access(k->t, x)) {
			uint64_t nnew = n / k->k;
			uint64_t y = bitsequence_reader_rank1(k->t, x) * (k->k * k->k) + (q / nnew);

			for(int j = 0; j < k->k; j++)
				if(k2reverse(k, nnew, q % nnew, p + nnew * j, y + j * k->k, l) < 0)
					return -1;
		}
	}
	return 0;
}

uint64_t* k2_column(K2Reader* k, uint64_t q, size_t* l) {
	if(q < 0 || q >= k->width) {
		*l = 0;
		return NULL;
	}
	if(!k->t) {
		*l = 0;
		return NULL;
	}

	IntList li = {0}; // initializing with zeros
	if(k2reverse(k, k->n, q, 0, -1, &li) < 0)
		return NULL;

	*l = li.len; // our list li has a capacity but we ignore it. just using len :)
	return li.data;
}

typedef struct {
	uint64_t n;
	uint64_t p;
	uint64_t q;
	int64_t x;
} K2IteratorElement;

static void k2_iter_init(K2Reader* k, uint64_t v, bool row, K2Iterator* it) {
	it->k = k;
	it->row = row;
	it->has_next = false; // set it to true if the element could be added to the queue

	if(k->t) {
		ringqueue_init(&it->queue, MIN(k->height, 16));

		K2IteratorElement* l = malloc(sizeof(*l));
		if(!l)
			return;

		l->n = k->n;
		l->x = -1;

		if(row) {
			l->p = v;
			l->q = 0;
		}
		else {
			l->p = 0;
			l->q = v;
		}

		if(ringqueue_enqueue(&it->queue, l) < 0)
			return;

		it->has_next = true;
	}
}

void k2_iter_init_row(K2Reader* k, uint64_t p, K2Iterator* it) {
	k2_iter_init(k, p, true, it);
}

static int k2_iter_next_element(K2Iterator* it, uint64_t* v) {
	int res = 0;
	while(!ringqueue_empty(&it->queue) && res != 1) {
		K2IteratorElement* l = ringqueue_dequeue(&it->queue);

		// check if width / height reached
		if(it->row) {
			if(l->q >= it->k->width)
				goto loop_continue;
		}
		else {
			if(l->p >= it->k->height)
				goto loop_continue;
		}

		if(l->x >= (int64_t) bitsequence_reader_len(it->k->t)) { // Warning: comparing signed values
			reader_bitpos(&it->k->l, l->x - bitsequence_reader_len(it->k->t));
			if(reader_readbit(&it->k->l)) {
				*v = it->row ? l->q : l->p;
				res = 1;
				goto loop_continue;
			}
		}
		else {
			if(l->x == -1 || bitsequence_reader_access(it->k->t, l->x)) {
				uint64_t k = it->k->k;
				uint64_t nnew = l->n / k;

				uint64_t y = bitsequence_reader_rank1(it->k->t, l->x) * (k * k);
				if(it->row)
					y += k * (l->p / nnew);
				else
					y += l->q / nnew;

				for(int j = 0; j < k; j++) {
					K2IteratorElement* el = malloc(sizeof(*el));
					if(!el) {
						res = -1;
						goto loop_continue;
					}

					el->n = nnew;
					if(it->row) {
						el->p = l->p % nnew;
						el->q = l->q + nnew * j;
						el->x = y + j;
					}
					else {
						el->p = l->p + nnew * j;
						el->q = l->q % nnew;
						el->x = y + j * k;
					}

					if(ringqueue_enqueue(&it->queue, el) < 0) {
						// TODO: goto und free
					}
				}
			}
		}

loop_continue:
		free(l);
	}

	return res;
}

int k2_iter_next(K2Iterator* it, uint64_t* v) {
	if(!it->has_next)
		return -1;

	uint64_t n;
	int res = k2_iter_next_element(it, &n);
	if(res != 1)
		k2_iter_finish(it);

	*v = n;
	return res;
}

void k2_iter_finish(K2Iterator* it) {
	if(it->has_next) {
		while(!ringqueue_empty(&it->queue))
			free(ringqueue_dequeue(&it->queue));
		ringqueue_destroy(&it->queue);
		it->has_next = false;
	}
}
