/**
 * @file rrr_writer.c
 * @author FR
 */

#include "rrr_writer.h"

#include <stdlib.h>
#include <constants.h>
#include <bitarray.h>
#include <writer.h>
#include <table.h>

#define BLOCKW (8 * sizeof(uint32_t)) // 32 bits
#define BLOCKWW (2 * BLOCKW)

static uint32_t get_bits(uint32_t* A, size_t start, size_t length) {
	if(length == 0)
		return 0;

	size_t i = start / BLOCKW;
	size_t j = start % BLOCKW;

	uint32_t result;
	if(j + length <= BLOCKW)
		result = (uint32_t)(A[i] << j) >> (BLOCKW - length);
	else {
		result = (uint32_t)(A[i] << j) >> (j - (length - (BLOCKW - j)));
		result = result | (A[i + 1] >> (BLOCKWW - j - length));
	}
	return result;
}

static void set_bits(uint32_t* A, size_t start, size_t length, uint32_t x) {
	if(length == 0)
		return;

	size_t i = start / BLOCKW;
	size_t j = start % BLOCKW;

	uint32_t mask = ((BLOCKW - j) < BLOCKW ? ~((uint32_t) 0) << (BLOCKW - j) : 0) | ((j + length) < BLOCKW ? ~((uint32_t) 0) >> (j + length) : 0);

	if(j + length <= BLOCKW)
		A[i] = A[i] & mask | x << (BLOCKW - j - length);
	else {
		A[i] = A[i] & mask | x >> (length - (BLOCKW - j));

		mask = ~((uint32_t) 0) >> (BLOCKWW - j - length);
		A[i + 1] = A[i + 1] & mask | x << (BLOCKWW - j - length);
	}
}

static uint32_t get_field(uint32_t* A, size_t length, size_t index) {
	switch(length) {
	case BLOCKW:
		return A[index];
	case 0:
		return 0;
	default:
		return get_bits(A, length * index, length);
	}
}

static void set_field(uint32_t* A, size_t length, size_t index, uint32_t x) {
	switch(length) {
	case BLOCKW:
		A[index] = x;
		// fallthrough
	case 0:
		return;
	default:
		set_bits(A, length * index, length, x);
	}
}

static int rrr_write_table(BitWriter* w, const uint32_t* table, size_t bytelen) {
	size_t i = 0;
	while(bytelen >= 4) {
		if(bitwriter_write_bits(w, table[i++], 32) < 0)
			return -1;
		bytelen -= 4;
	}
	if(bytelen > 0) {
		if(bitwriter_write_bits(w, table[i] >> (8 * (4 - bytelen)), 8 * bytelen) < 0)
			return -1;
	}

	return 0;
}

// Warning: very long function
int bitwriter_write_bitsequence_rrr(BitWriter* w, const BitArray* b, int sample_rate) {
	int res = -1;

	size_t l = bitarray_len(b);
	size_t ones = 0;

	// table block_types
	size_t block_type_len = DIVUP(l, BITS_PER_BLOCK);
	uint32_t* block_types = calloc(DIVUP(block_type_len * BLOCK_TYPE_BITS, BLOCKW), sizeof(*block_types));
	if(!block_types)
		return -1;

	size_t i;
	size_t block_ranks_len = 0;
	for(i = 0; i < block_type_len; i++) {
		uint32_t value = bitarray_count(b, i * BITS_PER_BLOCK, MIN(l - i * BITS_PER_BLOCK, BITS_PER_BLOCK), true);

		set_field(block_types, BLOCK_TYPE_BITS, i, value);

		ones += value;
		block_ranks_len += table_class_size(value);
	}

	// table block_ranks
	uint32_t* block_ranks = calloc(DIVUP(block_ranks_len, BLOCKW), sizeof(*block_ranks));
	if(!block_ranks)
		goto exit_0;

	size_t rank_pos = 0;
	for(i = 0; i < block_type_len; i++) {
		uint32_t value = bitarray_int(b, i * BITS_PER_BLOCK, MIN(l - i * BITS_PER_BLOCK, BITS_PER_BLOCK), true);
		uint32_t count = POPCNT32(value);

		set_bits(block_ranks, rank_pos, table_class_size(count), table_compute_offset(value));
		rank_pos += table_class_size(count);
	}

	//
	// Create sampling
	//

	// Sampling for c
	size_t sampling_len = block_type_len / sample_rate + 2;
	size_t sampling_field_bits = BIT_LEN(ones);

	uint32_t* sampling = calloc(MAX(1, DIVUP(sampling_len * sampling_field_bits, BLOCKW)), sizeof(*sampling));
	if(!sampling)
		goto exit_1;

	size_t sampling_sum = 0;
	for(i = 0; i < block_type_len; i++) {
		if(i % sample_rate == 0)
			set_field(sampling, sampling_field_bits, i / sample_rate, sampling_sum);

		sampling_sum += get_field(block_types, BLOCK_TYPE_BITS, i);
	}

	for(i = (block_type_len - 1) / sample_rate + 1; i < sampling_len; i++)
		set_field(sampling, sampling_field_bits, i, sampling_sum);

	// Sampling for O (table S)

	size_t block_ptr = DIVUP(block_type_len, sample_rate);
	size_t ptr_width = BIT_LEN(block_ranks_len);

	uint32_t* super_block_ptrs = calloc(DIVUP(block_ptr * ptr_width, BLOCKW), sizeof(*super_block_ptrs));
	if(!super_block_ptrs)
		goto exit_2;

	size_t pos = 0;
	for(i = 0; i < block_type_len; i++) {
		if(i % sample_rate == 0)
			set_field(super_block_ptrs, ptr_width, i / sample_rate, pos);

		pos += table_class_size(get_field(block_types, BLOCK_TYPE_BITS, i));
	}

	if(bitwriter_write_byte(w, BITSEQUENCE_RRR) < 0)
		goto exit_3;
	if(bitwriter_write_vbyte(w, l) < 0)
		goto exit_3;
	if(bitwriter_write_vbyte(w, sample_rate) < 0)
		goto exit_3;
	if(bitwriter_write_vbyte(w, ptr_width) < 0)
		goto exit_3;
	if(bitwriter_write_vbyte(w, sampling_field_bits) < 0)
		goto exit_3;
	if(bitwriter_write_vbyte(w, sampling_len) < 0)
		goto exit_3;

	size_t len_block_types = BYTE_LEN(block_type_len * BLOCK_TYPE_BITS);
	size_t len_block_ranks = BYTE_LEN(block_ranks_len);
	size_t len_sampling = BYTE_LEN(sampling_len * sampling_field_bits);
	size_t len_super_block_ptr = BYTE_LEN(DIVUP(block_type_len, sample_rate) * ptr_width);

	if(bitwriter_write_vbyte(w, len_block_types) < 0)
		goto exit_3;
	if(bitwriter_write_vbyte(w, len_block_ranks) < 0)
		goto exit_3;
	if(bitwriter_write_vbyte(w, len_sampling) < 0)
		goto exit_3;

	if(rrr_write_table(w, block_types, len_block_types) < 0)
		goto exit_3;
	if(rrr_write_table(w, block_ranks, len_block_ranks) < 0)
		goto exit_3;
	if(rrr_write_table(w, sampling, len_sampling) < 0)
		goto exit_3;
	if(rrr_write_table(w, super_block_ptrs, len_super_block_ptr) < 0)
		goto exit_3;

	if(bitwriter_flush(w) < 0)
		goto exit_3;

	res = 0;

exit_3:
	free(super_block_ptrs);
exit_2:
	free(sampling);
exit_1:
	free(block_ranks);
exit_0:
	free(block_types);

	return res;
}
