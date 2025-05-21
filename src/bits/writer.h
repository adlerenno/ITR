/**
 * @file writer.h
 * @author FR
 */

#ifndef WRITER_H
#define WRITER_H

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <bitarray.h>
#include <arith.h>

typedef struct {
	bool is_file; // written to a file
	union {
		// This bitwriter writes either to a file or to a bitarray
		struct {
			FILE* out;
			uint64_t byte_count;
			int bit_count; // bit count in accumulator
			uint8_t accumulator;
		};
		BitArray data;
	};
} BitWriter;

// If path is NULL, the data is written to the memory.
int bitwriter_init(BitWriter* w, const char* path);
int bitwriter_close(BitWriter* w);

// length in bits
uint64_t bitwriter_len(const BitWriter* w);
#define bitwriter_bytelen(w) (BYTE_LEN(bitwriter_len(w)))

// main functions
int bitwriter_write_bits(BitWriter* w, uint64_t bits, int n);
int bitwriter_flush(BitWriter* w);

#define bitwriter_write_bit(w, b) bitwriter_write_bits(w, b, 1)
#define bitwriter_write_byte(w, b) bitwriter_write_bits(w, b, 8)
int bitwriter_write_bytes(BitWriter* w, const void* data, size_t size);
int bitwriter_write_bitarray(BitWriter* w, const BitArray* b);

int bitwriter_write_vbyte(BitWriter* w, uint64_t n);
int bitwriter_write_eliasdelta(BitWriter* w, uint64_t n);

int bitwriter_write_bitwriter(BitWriter* restrict w, const BitWriter* restrict src);

typedef struct {
	int factor;
#ifdef RRR
	bool rrr;
#endif
} BitsequenceParams;

int bitwriter_write_bitsequence(BitWriter* w, const BitArray* b, const BitsequenceParams* params);

#endif
