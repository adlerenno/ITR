/**
 * @file k2_writer.c
 * @author FR
 */

#include "k2_writer.h"

#include <stdint.h>
#include <stdbool.h>
#include <bitarray.h>
#include <ringqueue.h>
#include <arith.h>

// Do not change!
#define K 2
#define K2 ((K) * (K))
#define NEXT_POW2(n) ((n) == 0 ? 1 : (1 << (__typeof(n)) BIT_LEN(n - 1)))

typedef struct {
	size_t width;
	size_t height;
	size_t n;
	BitArray* bits;
	size_t len_t;
	size_t len_l;
	size_t off_l;
} K2WriteParams;

typedef struct {
	size_t offsetL;
	size_t offsetR;
} QueueElement;

static int k2_write_data(K2WriteParams* m, BitWriter* w, const BitsequenceParams* p) {
	int res = -1;

	if(bitwriter_write_vbyte(w, m->width) < 0)
		goto exit_0;
	if(bitwriter_write_vbyte(w, m->height) < 0)
		goto exit_0;
	if(bitwriter_write_vbyte(w, K) < 0)
		goto exit_0;
	if(bitwriter_write_vbyte(w, m->n) < 0)
		goto exit_0;

	BitWriter w0;
	bitwriter_init(&w0, NULL);

	if(m->len_l > 0) {
		// because the bitdata in the K2Params can be reused, no new data is allocated
		BitArray bits_tmp;

		// bitsequence T
		bits_tmp.len = m->len_t;
		bits_tmp.cap = BYTE_LEN(m->len_t);
		bits_tmp.data = m->bits->data;

		if(bitwriter_write_bitsequence(&w0, &bits_tmp, p) < 0)
			goto exit_1;

		if(bitwriter_write_vbyte(w, bitwriter_bytelen(&w0)) < 0)
			goto exit_1;
		if(bitwriter_write_bitwriter(w, &w0) < 0)
			goto exit_1;

		// bitsequence L
		bits_tmp.len = m->len_l;
		bits_tmp.cap = BYTE_LEN(m->len_l);
		bits_tmp.data = m->bits->data + m->off_l;

		if(bitwriter_write_bitarray(w, &bits_tmp) < 0)
			goto exit_1;
	}
	else {
		if(bitwriter_write_vbyte(w, 0) < 0)
			goto exit_1;
	}
	if(bitwriter_flush(w) < 0)
		goto exit_1;

	res = 0;

exit_1:
	bitwriter_close(&w0);
exit_0:
	return res;
}

static inline int k2_queue_enqueue(RingQueue* q, size_t offsetL, size_t offsetR) {
	QueueElement* qe = malloc(sizeof(*qe));
	if(!qe)
		return -1;

	qe->offsetL = offsetL;
	qe->offsetR = offsetR;

	if(ringqueue_enqueue(q, qe) < 0) {
		free(qe);
		return -1;
	}

	return 0;
}

static inline void k2_queue_dequeue(RingQueue* q, size_t* offsetL, size_t* offsetR) {
	QueueElement* qe = ringqueue_dequeue(q);
	*offsetL = qe->offsetL;
	*offsetR = qe->offsetR;

	free(qe);
}

int k2_write(size_t width, size_t height, K2Edge* tedges, size_t edge_count, BitWriter* w, const BitsequenceParams* p) {
	size_t nodes = MAX(MAX(width, height), 2); // minimum is 2 so 1x1-matrices can be k^2-encoded

	// initialize the k2 write params
	K2WriteParams kp;
	kp.width = width;
	kp.height = height;
	kp.n = NEXT_POW2(nodes);

	if(edge_count == 0) { // if no edges exist: do not build the K2 tree
		kp.bits = NULL;
		kp.len_t = 0;
		kp.len_l = 0;
		kp.off_l = 0;
		return k2_write_data(&kp, w, p);
	}

	// set the kval field in each edge to zero
	for(size_t i = 0; i < edge_count; i++)
		tedges[i].kval = 0;

	uint64_t counter[K2];
	uint64_t boundaries[K2 + 1];
	uint64_t pointer[K2 + 1];

	int maxl = BIT_LEN(nodes - 1) - 1; // max layers

	BitArray bits; // initialize the T and L bits
	if(bitarray_init(&bits, edge_count * maxl * K2 + 8) < 0) // reserve an extra byte, just to be sure
		return -1;

	int res = -1;

	RingQueue q;
	ringqueue_init(&q, 0); // default cap

	if(k2_queue_enqueue(&q, 0, edge_count) < 0) // add the default element
		goto exit_0;

	// predeclare all variables
	size_t pos = 0, dequeues = 1, tmpCount, mask, k /* loop variable */,
		offsetL, offsetR, tempk, tempx, tempy, o;
	int shift, j;

	for(int i = 0; i < maxl; i++) {
		tmpCount = 0;

		shift = maxl - i;
		mask = ((size_t) 1 << (shift)) - 1;

		for(k = 0; k < dequeues; k++) {
			k2_queue_dequeue(&q, &offsetL, &offsetR);

			for(j = 0; j < K2; j++) {
				counter[j] = 0;
				pointer[j] = 0;
			}

			for(o = offsetL; o < offsetR; o++) {
				tedges[o].kval = (tedges[o].xval >> shift) + (tedges[o].yval >> shift) * K;
				tedges[o].xval = tedges[o].xval & mask;
				tedges[o].yval = tedges[o].yval & mask;

				counter[tedges[o].kval]++;
			}

			boundaries[0] = offsetL;
			for(j = 0; j < K2; j++) {
				boundaries[j + 1] = boundaries[j] + counter[j];
				pointer[j] = boundaries[j];

				if(boundaries[j + 1] != boundaries[j]) {
					if(k2_queue_enqueue(&q, boundaries[j], boundaries[j + 1]) < 0)
						goto exit_1;

					tmpCount++;
					bitarray_set(&bits, pos, true);
				}

				pos++;
			}

			for(j = 0; j < K2; j++) {
				while(pointer[j] < boundaries[j + 1]) {
					if(tedges[pointer[j]].kval != j) {
						tempk = tedges[pointer[j]].kval;
						tempx = tedges[pointer[j]].xval;
						tempy = tedges[pointer[j]].yval;

						while(tedges[pointer[tempk]].kval == tempk)
							pointer[tempk]++;

						tedges[pointer[j]].kval = tedges[pointer[tempk]].kval;
						tedges[pointer[j]].xval = tedges[pointer[tempk]].xval;
						tedges[pointer[j]].yval = tedges[pointer[tempk]].yval;

						tedges[pointer[tempk]].kval = tempk;
						tedges[pointer[tempk]].xval = tempx;
						tedges[pointer[tempk]].yval = tempy;

						pointer[tempk]++;
					} else {
						pointer[j]++;
					}
				}
			}
		}

		dequeues = tmpCount;
	}

	size_t len_t = pos;
	size_t off_l = BYTE_LEN(len_t);
	pos = 8 * off_l;

	while(!ringqueue_empty(&q)) {
		k2_queue_dequeue(&q, &offsetL, &offsetR);

		for(j = 0; j < K2; j++)
			counter[j] = 0;

		for(o = offsetL; o < offsetR; o++) {
			tedges[o].xval = tedges[o].xval % K;
			tedges[o].yval = tedges[o].yval % K;
			tedges[o].kval = tedges[o].xval + tedges[o].yval * K;

			counter[tedges[o].kval]++;
		}

		for(j = 0; j < K2; j++) {
			if(counter[j] > 0)
				bitarray_set(&bits, pos, true);

			pos++;
		}
	}

	kp.bits = &bits;
	kp.len_t = len_t;
	kp.len_l = pos - 8 * off_l;
	kp.off_l = off_l;

	res = k2_write_data(&kp, w, p);

exit_1:
	while(!ringqueue_empty(&q))
		free(ringqueue_dequeue(&q));
exit_0:
	ringqueue_destroy(&q);
	bitarray_destroy(&bits);
	return res;
}
