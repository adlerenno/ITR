/**
 * @file bitarray.h
 * @author FR
 */

#ifndef BITARRAY_H
#define BITARRAY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// Using type size_t because the data is stored in the memory
typedef struct {
	size_t len;
	size_t cap; // capacity in bytes
	uint8_t* data;
} BitArray;

// if len == 0, then this function always returns 0
int bitarray_init(BitArray* dst, size_t len);
#define bitarray_destroy(b) \
	do{ \
		if((b)->data) { \
			free((b)->data); \
		} \
	} while(0)

#define bitarray_len(b) ((b)->len)

bool bitarray_get(const BitArray* b, size_t i);
void bitarray_set(BitArray* b, size_t i, bool bit);

int bitarray_clone(BitArray* restrict dst, const BitArray* restrict src);
int bitarray_append(BitArray* b, bool v);
int bitarray_append_bits(BitArray* b, uint64_t bits, int n);
int bitarray_append_bitarray(BitArray* b, const BitArray* bs);

size_t bitarray_count(const BitArray* b, size_t start, size_t len, bool bit);

#ifdef RRR // only needed with RRR
uint64_t bitarray_int(const BitArray* b, size_t pos, size_t length, bool reverse);
#endif

#endif
