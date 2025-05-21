/**
 * @file k2.h
 * @author FR
 */

#ifndef K2TREE_H
#define K2TREE_H

#include <bitsequence_r.h>
#include <ringqueue.h>

typedef struct {
	uint64_t width;
	uint64_t height;
	int k; // just using normal int because normally our k is 2
	uint64_t n;

	BitsequenceReader* t; // bitsequence T with a bitsequence reader
	Reader l; // bitsequence L is not optimized for rank / select because only access is needed.
} K2Reader;

K2Reader* k2_init(Reader* r);
void k2_destroy(K2Reader* k);

bool k2_get(K2Reader* k, uint64_t r, uint64_t c);

// The column can be determined via a regular function because the number of elements in the pointer
// are limited to the rank of the compression.
uint64_t* k2_column(K2Reader* k, uint64_t q, size_t* l);

typedef struct {
	K2Reader* k;
	bool row;
	bool has_next;
	RingQueue queue;
} K2Iterator;

void k2_iter_init_row(K2Reader* k, uint64_t p, K2Iterator* it);

// return value:
// 1: next element exists
// 0: no next element exists
// -1: error occured
int k2_iter_next(K2Iterator* it, uint64_t* v);
void k2_iter_finish(K2Iterator* it); // needed if the K2Iterator was not iterated to the end

#endif
