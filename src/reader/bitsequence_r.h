/**
 * @file bitsequence.h
 * @author FR
 */

#ifndef BITSEQUENCE_R_H
#define BITSEQUENCE_R_H

#include <stddef.h>
#include <reader.h>
#include <stdbool.h>
#include <bitarray.h>

// This struct has the same attributes as the Python implementation.
typedef struct {
	Reader r;

	uint8_t type;
	FileOff len;

	union {
		struct { // regular or RG
			FileOff off;
			int factor;
			int bits_per_rs;
			int s;
			FileOff rs_off;
		};
#ifdef RRR
		struct { // RRR
			int sample_rate;
			uint8_t ptr_width;
			uint8_t sampling_field_bits;
			FileOff sampling_len;
			FileOff block_type_len;
			FileOff offset_block_types;
			FileOff offset_block_ranks;
			FileOff offset_sampling;
			FileOff offset_super_block_ptrs;
		};
#endif
	};

	uint64_t ones;
} BitsequenceReader;

BitsequenceReader* bitsequence_reader_init(Reader* r);
#define bitsequence_reader_destroy(b) free(b)

#define bitsequence_reader_len(b) ((b)->len)
#define bitsequence_reader_ones(b) ((b)->ones)
bool bitsequence_reader_access(BitsequenceReader* b, uint64_t i);

// working with signed values because we allow the value -1
uint64_t bitsequence_reader_rank0(BitsequenceReader* b, int64_t i);
uint64_t bitsequence_reader_rank1(BitsequenceReader* b, int64_t i);
int64_t bitsequence_reader_select0(BitsequenceReader* b, uint64_t i);
int64_t bitsequence_reader_select1(BitsequenceReader* b, uint64_t i);
int64_t bitsequence_reader_selectprev1(BitsequenceReader* b, uint64_t i);

#endif
