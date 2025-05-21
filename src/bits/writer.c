/**
 * @file writer.c
 * @author FR
 */

#include "writer.h"

#include <assert.h>
#include <arith.h>
#include <constants.h>
#include <bitarray.h>
#include <bitsequence.h>

#ifdef RRR
// Table only needed for RRR
#include <rrr_writer.h>
#endif

int bitwriter_init(BitWriter* w, const char* path) {
	if(path) {
		FILE* f = fopen(path, "wb+");
		if(!f)
			return -1;

		w->is_file = true;
		w->out = f;
		w->byte_count = 0;
		w->bit_count = 0;
		w->accumulator = 0;
	}
	else {
		w->is_file = false;
		bitarray_init(&w->data, 0);
	}

	return 0;
}

int bitwriter_close(BitWriter* w) {
	if(w->is_file) {
		int ret = bitwriter_flush(w);

		if(fclose(w->out) < 0)
			return -1;

		return ret;
	}
	else {
		bitarray_destroy(&w->data);
		return 0;
	}
}

uint64_t bitwriter_len(const BitWriter* w) {
	if(w->is_file)
		return 8 * w->byte_count + w->bit_count;
	else
		return bitarray_len(&w->data);
}

int bitwriter_write_bits(BitWriter* w, uint64_t bits, int n) {
	assert(n >= 0 && n <= 8 * sizeof(bits)); // n should never exceed 64 bit

	if(w->is_file) {
		while(n > 0) {
			if(w->bit_count + n >= 8) {
				int n_bits = 8 - w->bit_count;
				w->accumulator |= bits >> (n - n_bits);
				w->bit_count += n_bits;

				if(bitwriter_flush(w) < 0)
					return -1;

				n -= n_bits;
			}
			else {
				int shift = 8 - (w->bit_count + n);
				w->accumulator |= bits << shift;
				w->bit_count += n;
				break;
			}
		}
	}
	else {
		if(bitarray_append_bits(&w->data, bits, n) < 0)
			return -1;
	}

	return 0;
}

int bitwriter_flush(BitWriter* w) {
	if(w->is_file) {
		if(w->bit_count > 0) {
			if(fwrite(&w->accumulator, sizeof(w->accumulator), 1, w->out) == 0)
				return -1;

			w->byte_count++;
			w->bit_count = 0;
			w->accumulator = 0;
		}
	}
	else {
		int mod = bitarray_len(&w->data) % 8;
		if(mod > 0)
			return bitarray_append_bits(&w->data, 0, 8 - mod);
	}

	return 0;
}

int bitwriter_write_bytes(BitWriter* w, const void* data, size_t size) {
	for(size_t i = 0; i < size; i++) {
		if(bitwriter_write_byte(w, ((const uint8_t*) data)[i]) < 0)
			return -1;
	}

	return 0;
}

int bitwriter_write_bitarray(BitWriter* w, const BitArray* b) {
	if(bitarray_len(b) == 0)
		return 0;

	size_t end_bit = bitarray_len(b);
	size_t end_byte = BYTE_LEN(end_bit);
	int last_bits = end_bit % 8;

	if(last_bits > 0)
		end_byte--;

	// write all bytes of the data of the bitarray
	if(bitwriter_write_bytes(w, b->data, end_byte) < 0)
		return -1;

	// write the last byte
	if(last_bits > 0) {
		uint8_t value = b->data[end_byte] >> (8 - last_bits);
		if(bitwriter_write_bits(w, value, last_bits) < 0)
			return -1;
	}

	return 0;
}

int bitwriter_write_vbyte(BitWriter* w, uint64_t n) {
	while(n > 0x7f) {
		if(bitwriter_write_byte(w, n & 0x7f) < 0)
			return -1;
		n >>= 7;
	}

	if(bitwriter_write_byte(w, n | 0x80) < 0)
		return -1;

	return 0;
}

int bitwriter_write_eliasdelta(BitWriter* w, uint64_t n) {
	n++;

	int len = BIT_LEN(n);
	int len_of_len = BIT_LEN(len) - 1;

	int i;
	for(i = len_of_len; i > 0; i--)
		if(bitwriter_write_bit(w, 0) < 0)
			return -1;

	for(i = len_of_len; i >= 0; i--)
		if(bitwriter_write_bit(w, (len >> i) & 1) < 0)
			return -1;

	for(i = len - 2; i >= 0; i--)
		if(bitwriter_write_bit(w, (n >> i) & 1) < 0)
			return -1;

	return 0;
}

int bitwriter_write_bitwriter(BitWriter* restrict w, const BitWriter* restrict src) {
	assert(!src->is_file);
	assert(bitarray_len(&src->data) % 8 == 0);

	if(bitwriter_write_bitarray(w, &src->data) < 0)
		return -1;
	return bitwriter_flush(w);
}

static int bitwriter_write_bitsequence_rg(BitWriter* w, const BitArray* b, int factor) {
	Bitsequence bs;
	if(bitsequence_build(&bs, b, factor) < 0)
		return -1;

	int res = -1;

	int bits_per_rs = BITS_NEEDED(bs.rs[bs.rs_len - 1]); // precondition: last block of rs contains the max value

	if(bitwriter_write_byte(w, BITSEQUENCE_RG) < 0)
		goto exit;
	if(bitwriter_write_vbyte(w, bitarray_len(b)) < 0)
		goto exit;
	if(bitwriter_write_vbyte(w, factor) < 0)
		goto exit;
	if(bitwriter_write_vbyte(w, bits_per_rs) < 0)
		goto exit;
	if(bitwriter_write_bitarray(w, b) < 0)
		goto exit;

	for(size_t i = 0; i < bs.rs_len; i++) {
		if(i > 0) { // skip first block
			if(bitwriter_write_bits(w, bs.rs[i], bits_per_rs) < 0)
				goto exit;
		}
	}
	if(bitwriter_flush(w) < 0)
		goto exit;

	res = 0;

exit:
	bitsequence_destroy(&bs);
	return res;
}

int bitwriter_write_bitsequence(BitWriter* w, const BitArray* b, const BitsequenceParams* params) {
	size_t len = bitarray_len(b);

	if(len <= 200) { // length <= 200: write without super blocks
		if(bitwriter_write_byte(w, BITSEQUENCE_REGULAR) < 0)
			return -1;
		if(bitwriter_write_vbyte(w, bitarray_len(b)) < 0)
			return -1;
		if(bitwriter_write_bitarray(w, b) < 0)
			return -1;
		if(bitwriter_flush(w) < 0)
			return -1;
		return 0;
	}
#ifdef RRR
	else if(params->rrr)
		return bitwriter_write_bitsequence_rrr(w, b, params->factor);
#endif
	else
		return bitwriter_write_bitsequence_rg(w, b, params->factor);
}
