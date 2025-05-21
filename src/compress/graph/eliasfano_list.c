/**
 * @file eliasfano_list.c
 * @author FR
 */

#include "eliasfano_list.h"

#include <math.h>
#include <bitarray.h>

#ifndef NDEBUG
#include <assert.h>

// only used for assertion
static inline void assert_is_sorted(const uint64_t* list, size_t len) {
	for(size_t i = 1; i < len; i++)
		assert(list[i] >= list[i - 1]);
}
#else
#define assert_is_sorted(list, len) ((void)0)
#endif

static inline void set_bits(BitArray* b, size_t off, uint64_t bits, int len) {
	for(int i = 0; i < len; i++) {
		uint64_t val = bits & (1 << (len - i - 1));
		bitarray_set(b, off + i, val > 0);
	}
}

int eliasfano_write(const uint64_t* list, size_t n, BitWriter* w, const BitsequenceParams* p) {
	assert_is_sorted(list, n);

	int res = -1;

	uint64_t universe = n > 0 ? list[n - 1] : 0;

	// determine number of lower and higher bits
	int lower_bits = universe > n ? (int) ceil(log2((double) universe / n)) : 0;
	size_t higher_bits_len = n + (universe >> lower_bits);

	uint64_t mask = (((uint64_t) 1) << lower_bits) - 1;
	size_t lower_bits_len = n * lower_bits;

	BitArray hi, lo;
	if(bitarray_init(&hi, higher_bits_len) < 0)
		return -1;
	if(bitarray_init(&lo, lower_bits_len) < 0)
		goto exit_0;

	for(size_t i = 0; i < n; i++) {
		uint64_t elem = list[i];

		uint64_t high = (elem >> lower_bits) + i;
		uint64_t low = elem & mask;

		bitarray_set(&hi, high, true);

		size_t off = i * lower_bits;
		set_bits(&lo, off, low, lower_bits);
	}

	if(bitwriter_write_vbyte(w, n) < 0)
		goto exit_1;
	if(bitwriter_write_vbyte(w, lower_bits) < 0)
		goto exit_1;
	if(bitwriter_write_vbyte(w, BYTE_LEN(bitarray_len(&lo))) < 0)
		goto exit_1;
	if(bitwriter_write_bitarray(w, &lo) < 0)
		goto exit_1;
	if(bitwriter_flush(w) < 0)
		goto exit_1;
	if(bitwriter_write_bitsequence(w, &hi, p) < 0)
		goto exit_1;

	res = 0;

exit_1:
	bitarray_destroy(&lo);
exit_0:
	bitarray_destroy(&hi);

	return res;
}
