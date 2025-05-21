/**
 * @file bitsequence.c
 * @author FR
 */

#include "bitsequence.h"

#include <stdlib.h>
#include <constants.h>
#include <bitarray.h>
#include <arith.h>

#define BLOCKW 32

static size_t build_rank_sub(const BitArray* bits, size_t start, size_t blocks) {
	size_t num_blocks = DIVUP(bitarray_len(bits), BLOCKW);

	size_t rank = 0;
	for(size_t i = start; i < start + blocks; i++) {
		if(i < num_blocks)
			rank += bitarray_count(bits, i * BLOCKW, BLOCKW, true);
	}

	return rank;
}

int bitsequence_build(Bitsequence* b, const BitArray* bits, int factor) {
	if(factor <= 0)
		factor = DEFAULT_FACTOR;

	int s = BLOCKW * factor;

	size_t num_sblock = bitarray_len(bits) / s + 1;
	size_t* rs = malloc(num_sblock * sizeof(*rs));
	if(!rs)
		return -1;

	rs[0] = 0;
	for(size_t i = 1; i < num_sblock; i++)
		rs[i] = rs[i - 1] + build_rank_sub(bits, (i - 1) * factor, factor);

	b->bits = bits;
	b->factor = factor;
	b->s = s;
	b->rs_len = num_sblock;
	b->rs = rs;
	b->ones = bitsequence_rank1(b, bitarray_len(bits) - 1);

	return 0;
}

size_t bitsequence_rank0(const Bitsequence* b, ssize_t i) {
	if(i < 0)
		return 0;

	return i + 1 - bitsequence_rank1(b, i);
}

size_t bitsequence_rank1(const Bitsequence* b, ssize_t i) {
	if(i < 0)
		return 0;
	if(i >= bitsequence_len(b))
		return b->ones;

	i++;

	size_t res = b->rs[i / b->s];
	size_t aux = (i / b->s) * b->factor;

	size_t bit_len = i - BLOCKW * aux;

	if(bit_len) {
		size_t byte_len = BYTE_LEN(bit_len);
		uint8_t* data = b->bits->data + (BLOCKW * aux) / 8;

		int endbits = byte_len * 8 - bit_len;

		// Calculate number of 1-bits in the corresponding data
		if(endbits) {
			size_t end_byte = byte_len - 1;
			res += popcnt(data, end_byte);
			res += POPCNT8(data[end_byte] >> endbits);
		}
		else
			res += popcnt(data, byte_len);
	}

	return res;
}
