/**
 * @file bitarray.c
 * @author FR
 */

#include "bitarray.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <memdup.h>
#include <arith.h>
#include <panic.h>

#define BITARRAY_DEFAULT_CAP 8

int bitarray_init(BitArray* dst, size_t len) {
	if(len) {
		size_t cap = MAX(BYTE_LEN(len), BITARRAY_DEFAULT_CAP);
		uint8_t* data = calloc(cap, sizeof(uint8_t));
		if(!data)
			return -1;

		dst->len = len;
		dst->cap = cap;
		dst->data = data;
	}
	else {
		dst->len = len;
		dst->cap = 0;
		dst->data = NULL;
	}

	return 0;
}

bool bitarray_get(const BitArray* b, size_t i) {
	if(i >= b->len)
		panic("index %zu exceeds the length %zu", i, b->len);

	size_t byteindex = i / 8;
	size_t bitoff = i % 8;

	return ((b->data[byteindex] >> (8 - bitoff - 1)) & 1) == 1;
}

void bitarray_set(BitArray* b, size_t i, bool bit) {
	if(i >= b->len)
		panic("index %zu exceeds the length %zu", i, b->len);

	size_t byteindex = i / 8;
	size_t bitoff = i % 8;

	uint8_t mask = 1 << (8 - bitoff - 1);

	if(bit)
		b->data[byteindex] |= mask;
	else
		b->data[byteindex] &= ~mask;
}

int bitarray_clone(BitArray* restrict dst, const BitArray* restrict src) {
	uint8_t* data;
	if(src->cap != 0) {
		data = memdup(src->data, src->cap * sizeof(uint8_t));
		if(!data)
			return -1;
	}
	else
		data = NULL;

	dst->len = src->len;
	dst->cap = src->cap;
	dst->data = data;

	return 0;
}

static int bitarray_resize(BitArray* b, size_t min_cap) {
	uint8_t *old_data = b->data, *new_data;

	size_t old_cap = b->cap;
	size_t new_cap;

	if(old_data && old_cap > 0) {
		if(min_cap <= old_cap)
			return 0;

		new_cap = NEW_LEN(old_cap, min_cap - old_cap, min_cap >> 1);

		new_data = realloc(old_data, new_cap * sizeof(*new_data));
		if(!new_data)
			return -1;

		b->cap = new_cap;
		b->data = new_data;
	}
	else {
		new_cap = MAX(BITARRAY_DEFAULT_CAP, min_cap);

		new_data = malloc(new_cap * sizeof(*new_data));
		if(!new_data)
			return -1;

		b->cap = new_cap;
		b->data = new_data;
	}

	return 0;
}

int bitarray_append(BitArray* b, bool v) {
	size_t byteindex = b->len / 8;
	size_t bitoff = b->len % 8;

	if(bitoff == 0) {
		if(byteindex == b->cap) { // need realloc
			if(bitarray_resize(b, b->cap + 1) < 0)
				return -1;
		}

		// append by setting the last byte
		b->data[byteindex] = v ? 0x80 : 0;
	}
	else {
		uint8_t mask = ((uint8_t) 1) << (8 - bitoff - 1);

		if(v)
			b->data[byteindex] |= mask;
		else
			b->data[byteindex] &= ~mask;
	}

	b->len++;
	return 0;
}

int bitarray_append_bits(BitArray* b, uint64_t bits, int n) {
	assert(n >= 0 && n <= 8 * sizeof(bits)); // n should never exceed 64 bit

	if(n == 0)
		return 0;

	size_t new_cap = BYTE_LEN(b->len + n);
	if(bitarray_resize(b, new_cap) < 0)
		return -1;

	while(n > 0) {
		size_t byte_index = b->len / 8;
		int bit_count = b->len % 8;

		if(bit_count == 0) // clearing the byte needed
			b->data[byte_index] = 0;
		if(bit_count + n >= 8) {
			int n_bits = 8 - bit_count;
			b->data[byte_index] |= bits >> (n - n_bits);
			b->len += n_bits;

			n -= n_bits;
		}
		else { // `bits` fits in the last byte
			int shift = 8 - (bit_count + n);
			b->data[byte_index] |= bits << shift;
			b->len += n;
			break;
		}
	}

	return 0;
}

int bitarray_append_bitarray(BitArray* b, const BitArray* bs) {
	if(bs->len == 0)
		return 0;
	if(b->len == 0) {
		uint8_t* data = memdup(bs->data, bs->cap * sizeof(*data));
		if(!data)
			return -1;

		b->len = bs->len;
		b->cap = bs->cap;
		b->data = data;

		return 0;
	}

	size_t new_cap = BYTE_LEN(b->len + bs->len);
	if(bitarray_resize(b, new_cap) < 0)
		return -1;

	int expected_offset = b->len % 8;
	uint8_t* d = b->data + (b->len / 8); // data where the data of bs 

	if(expected_offset > 0) { // each byte of the new data has to be shifted
		int bits_in_last_byte = bs->len % 8;
		if(bits_in_last_byte == 0)
			bits_in_last_byte = 8;

		// set first byte by joining the last byte of `b` with the first byte of `bs`
		d[0] = d[0] & ~(((uint8_t) 0xff) >> expected_offset) | (bs->data[0] >> expected_offset);

		size_t byte_len = BYTE_LEN(bs->len);
		for(size_t i = 1; i < byte_len; i++)
			d[i] = ((bs->data[i - 1] << (8 - expected_offset)) & 0xff) | (bs->data[i] >> expected_offset);

		if(bits_in_last_byte + expected_offset > 8)
			d[byte_len] = bs->data[byte_len - 1] << (8 - expected_offset);
	}
	else
		memcpy(d, bs->data, BYTE_LEN(bs->len) * sizeof(uint8_t));

	b->len += bs->len;

	return 0;
}

size_t bitarray_count(const BitArray* b, size_t start, size_t len, bool bit) {
	if(len == 0)
		return 0;

	assert(start + len <= b->len);

	size_t start_byte = start / 8;
	size_t end_byte = BYTE_LEN(start + len) - 1;

	size_t count = popcnt(b->data + start_byte, end_byte - start_byte);

	size_t bit_offset = start % 8;
	if(bit_offset > 0)
		count -= POPCNT8(b->data[start_byte] >> (8 - bit_offset));

	size_t end_bits = (end_byte + 1) * 8 - (start + len);
	count += POPCNT8(b->data[end_byte] >> end_bits);

	return bit ? count : len - count;
}

#ifdef RRR // only needed with RRR
static inline uint64_t uint64_reverse(uint64_t value) {
	uint8_t* data = (uint8_t*) &value;

	uint64_t res;
	uint8_t* res_data = (uint8_t*) &res;

	for(int i = 0; i < sizeof(uint64_t); i++)
		res_data[sizeof(uint64_t) - i - 1] = byte_reverse(data[i]);

	return res;
}

uint64_t bitarray_int(const BitArray* b, size_t pos, size_t length, bool reverse) {
	if(pos + length > b->len)
		panic("data exceeds the length of the data");

	int off = pos % 8;
	uint64_t mask = ((uint64_t) 1 << length) - 1;
	int shift;

	const uint8_t* data = b->data + (pos / 8);

	uint64_t res;
	if(off + length <= 8) // shortcut if bits do not cross the boundaries of bytes
		res = (*data >> (8 - off - length)) & mask;
	else {
		int byte_len = BYTE_LEN(off + length);
		shift = 8 * byte_len - length - off;

		uint64_t val = 0;
		for(int i = 0; i < byte_len; i++)
			val = val << 8 | (uint64_t) data[i];

		res = (val >> shift) & mask;
	}

	if(reverse) {
		shift = (8 * sizeof(uint64_t)) - length;
		res = uint64_reverse(res) >> shift;
	}

	return res;
}
#endif
