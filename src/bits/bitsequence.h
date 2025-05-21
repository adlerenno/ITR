/**
 * @file bitsequence.h
 * @author FR
 */

#ifndef BITSEQUENCE_COMPRESS_H
#define BITSEQUENCE_COMPRESS_H

#include <sys/types.h>
#include <bitarray.h>

typedef struct {
	const BitArray* bits;
	int factor;
	int s;
	size_t rs_len;
	size_t* rs;
	size_t ones;
} Bitsequence;

int bitsequence_build(Bitsequence* b, const BitArray* bits, int factor);
#define bitsequence_destroy(b) free((b)->rs)

#define bitsequence_len(b) (bitarray_len((b)->bits))

// working with signed values because we allow the value -1
size_t bitsequence_rank0(const Bitsequence* b, ssize_t i);
size_t bitsequence_rank1(const Bitsequence* b, ssize_t i);

#endif
