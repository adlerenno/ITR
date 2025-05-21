/**
 * @file bitsequence.c
 * @author FR
 */

#include "bitsequence_r.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <reader.h>
#include <constants.h>
#include <panic.h>
#include <arith.h>
#include <bitarray.h>

#define BLOCKW 32

#ifdef RRR
// Table only needed for RRR
#include <table.h>
#endif

BitsequenceReader* bitsequence_reader_init(Reader* r) {
	uint8_t t = reader_readbyte(r);

	switch(t) {
	case BITSEQUENCE_REGULAR:
	case BITSEQUENCE_RG:
#ifdef RRR
	case BITSEQUENCE_RRR:
#endif
		break;
	default: // unknown type
		return NULL;
	}

	BitsequenceReader* b = malloc(sizeof(*b));
	if(!b)
		return NULL;

	b->r = *r;
	b->type = t;

	size_t nbytes;
	b->len = reader_vbyte(r, &nbytes);
	FileOff off = nbytes + 1;

	switch(t) {
	case BITSEQUENCE_REGULAR:
		b->off = 8 * off;
		break;
	case BITSEQUENCE_RG:
		b->factor = reader_vbyte(r, &nbytes);
		off += nbytes;

		b->bits_per_rs = reader_vbyte(r, &nbytes);
		off += nbytes;

		b->off = 8 * off;
		b->s = BLOCKW * b->factor;
		b->rs_off = b->off + b->len;
		break;
#ifdef RRR
	case BITSEQUENCE_RRR:
		b->sample_rate = reader_vbyte(r, &nbytes);
		off += nbytes;

		b->ptr_width = reader_vbyte(r, &nbytes);
		off += nbytes;

		b->sampling_field_bits = reader_vbyte(r, &nbytes);
		off += nbytes;

		b->sampling_len = reader_vbyte(r, &nbytes);
		off += nbytes;

		b->block_type_len = DIVUP(b->len, BITS_PER_BLOCK);

		FileOff len_block_types = reader_vbyte(r, &nbytes);
		off += nbytes;

		FileOff len_block_ranks = reader_vbyte(r, &nbytes);
		off += nbytes;

		FileOff len_sampling = reader_vbyte(r, &nbytes);
		off += nbytes;

		b->offset_block_types = 8 * off;
		b->offset_block_ranks = b->offset_block_types + 8 * len_block_types;
		b->offset_sampling = b->offset_block_ranks + 8 * len_block_ranks;
		b->offset_super_block_ptrs = b->offset_sampling + 8 * len_sampling;
#endif
	}

	if(b->len > 0)
		b->ones = bitsequence_reader_rank1(b, b->len - 1);
	else
		b->ones = 0;

	return b;
}

#ifdef RRR
static uint64_t get_bits(BitsequenceReader* b, FileOff offset, FileOff start, uint8_t length) {
	if(length == 0)
		return 0;

	FileOff pos = offset + start;
	int off = pos % 8;
	uint64_t mask = ((uint64_t) 1 << length) - 1;

	reader_bitpos(&b->r, 8 * (pos / 8));

	if(off + length <= 8) // shortcut if bits do not cross the boundaries of bytes
		return (reader_readbyte(&b->r) >> (8 - off - length)) & mask;

	int byte_len = BYTE_LEN(off + length);
	int shift = 8 * byte_len - length - off;

	const uint8_t* data = reader_read(&b->r, byte_len);

	uint64_t val = 0;
	for(int i = 0; i < byte_len; i++)
		val = val << 8 | (uint64_t) data[i];

	return (val >> shift) & mask;
}

#define get_field(b, off, len, index) get_bits(b, off, (len) * (index), len)

static bool access_rrr(BitsequenceReader* b, uint64_t i) {
	FileOff block = i / BITS_PER_BLOCK;
	FileOff super_block = block / b->sample_rate;

	FileOff rank_offset = get_field(b, b->offset_super_block_ptrs, b->ptr_width, super_block);

	FileOff block_type;
	for(FileOff k = super_block * b->sample_rate; k < block; k++) {
		block_type = get_field(b, b->offset_block_types, BLOCK_TYPE_BITS, k);
		rank_offset += table_class_size(block_type);
	}

	block_type = get_field(b, b->offset_block_types, BLOCK_TYPE_BITS, block);
	FileOff offset = get_bits(b, b->offset_block_ranks, rank_offset, table_class_size(block_type));

	FileOff mask = 1 << (i % BITS_PER_BLOCK);
	return (mask & table_short_bitmap(block_type, offset)) != 0;
}
#endif

bool bitsequence_reader_access(BitsequenceReader* b, uint64_t i) {
	if(i >= b->len)
		panic("index %" PRIu64 " exceeds the length %" PRIu64, i, b->len);

#ifdef RRR
	if(b->type == BITSEQUENCE_RRR)
		return access_rrr(b, i);
#endif

	reader_bitpos(&b->r, b->off + i);
	return reader_readbit(&b->r);
}

uint64_t bitsequence_reader_rank0(BitsequenceReader* b, int64_t i) {
	if(i < 0)
		return 0;

	return i + 1 - bitsequence_reader_rank1(b, i);
}

static uint64_t rs_value(BitsequenceReader* b, int64_t i) {
	if(i == 0)
		return 0;

	reader_bitpos(&b->r, b->rs_off + b->bits_per_rs * (i - 1));
	return reader_readint(&b->r, b->bits_per_rs);
}

#ifdef RRR
static uint64_t rank1_rrr(BitsequenceReader* b, uint64_t i) {
	FileOff block = i / BITS_PER_BLOCK;
	FileOff super_block = block / b->sample_rate;

	FileOff c_sum = get_field(b, b->offset_sampling, b->sampling_field_bits, super_block);
	FileOff rank = get_field(b, b->offset_super_block_ptrs, b->ptr_width, super_block);

	FileOff k = super_block * b->sample_rate;
	FileOff aux;

	if(k % 2 == 1 && k < block) {
		aux = get_field(b, b->offset_block_types, BLOCK_TYPE_BITS, k);
		c_sum += aux;
		rank += table_class_size(aux);
		k++;
	}

	FileOff a = k / 2;

	int max_block = block > 0 ? block - 1 : 0; // max(block - 1, 0)
	while(k < max_block) {
		uint8_t val = get_field(b, b->offset_block_types, 8, a);

		c_sum += (val & 0xf) + (val >> 4);
		rank += table_class_size(val & 0xf) + table_class_size(val >> 4);
		a++;
		k += 2;
	}

	if(k < block) {
		aux = get_field(b, b->offset_block_types, BLOCK_TYPE_BITS, k);
		c_sum += aux;
		rank += table_class_size(aux);
		k++;
	}

	FileOff c = get_field(b, b->offset_block_types, BLOCK_TYPE_BITS, block);
	FileOff offset = get_bits(b, b->offset_block_ranks, rank, table_class_size(c));

	c_sum += POPCNT32(((2 << (i % BITS_PER_BLOCK)) - 1) & table_short_bitmap(c, offset));
	return c_sum;
}
#endif

uint64_t bitsequence_reader_rank1(BitsequenceReader* b, int64_t i) {
	if(i < 0)
		return 0;
	if(i >= b->len)
		return b->ones;
#ifdef RRR
	if(b->type == BITSEQUENCE_RRR)
		return rank1_rrr(b, i);
#endif

	i++;

	uint64_t res;
	FileOff aux;
	if(b->type == BITSEQUENCE_REGULAR) {
		res = 0;
		aux = 0;
	}
	else {
		res = rs_value(b, i / b->s);
		aux = (i / b->s) * b->factor;
	}

	FileOff bit_len = i - BLOCKW * aux;

	if(bit_len) {
		// set bitpos to first block
		reader_bitpos(&b->r, b->off + BLOCKW * aux);

		size_t byte_len = BYTE_LEN(bit_len);
		const uint8_t* data = reader_read(&b->r, byte_len);

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

// This function always returns the block in big endian order.
static inline uint32_t block_get(BitsequenceReader* b, FileOff i) {
	reader_bitpos(&b->r, b->off + i * BLOCKW);

	const uint8_t* data;
	if(i * BLOCKW + BLOCKW <= b->len) { // check if current block consists of 4 bytes
		data = reader_read(&b->r, BLOCKW / 8);

#if UNALIGN_ACCESS
		return *((uint32_t*) data);
#else
		// Determine the block data without doing unaligned accesses.
		union {
			uint32_t value;
			uint8_t block_data[4];
		} block;

		memcpy(block.block_data, data, sizeof(block));
		return block.value;
#endif
	}

	int byte_len = BYTE_LEN(b->len - i * BLOCKW);

	data = reader_read(&b->r, byte_len);

	uint32_t block = 0; // initialize with zero so the lower uncopied bytes are 0
	memcpy(&block, data, byte_len);
	return block;
}

// Reverses the bits in an block.
// Because the function "block_get" returns the block in big-endian-order, the block also needs to be reversed, if necessary.
static inline uint32_t block_reverse(uint32_t block) {
	uint8_t* data = (uint8_t*) &block;

	uint32_t res;
	uint8_t* res_data = (uint8_t*) &res;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	res_data[0] = byte_reverse(data[0]);
	res_data[1] = byte_reverse(data[1]);
	res_data[2] = byte_reverse(data[2]);
	res_data[3] = byte_reverse(data[3]);
#else
	res_data[3] = byte_reverse(data[0]);
	res_data[2] = byte_reverse(data[1]);
	res_data[1] = byte_reverse(data[2]);
	res_data[0] = byte_reverse(data[3]);
#endif

	return res;
}

static uint64_t select0_blocks(BitsequenceReader* b, uint64_t i, FileOff pos) {
	FileOff numblocks = DIVUP(b->len, BLOCKW);

	uint32_t j;
	int zeros;
	for(;;) {
		j = block_get(b, pos);
		zeros = POPCNT32(~j);

		if(zeros >= i)
			break;

		i -= zeros;

		if(++pos > numblocks)
			return b->len;
	}

	// Reverse the block if needed
	j = block_reverse(j);

	pos = BLOCKW * pos + select_bit(~j, i - 1);
	if(pos > b->len)
		return b->len;

	return pos;
}

static uint64_t select0_rg(BitsequenceReader* b, uint64_t i) {
	FileOff lv = 0;
	FileOff rv = b->len / b->s;
	FileOff mid = (lv + rv) / 2;
	uint64_t rankmid = mid * b->factor * BLOCKW - rs_value(b, mid);

	while(lv <= rv) {
		if(rankmid < i)
			lv = mid + 1;
		else
			rv = mid - 1;

		mid = (lv + rv) / 2;
		rankmid = mid * b->factor * BLOCKW - rs_value(b, mid);
	}

	FileOff pos = mid * b->factor;
	i -= rankmid;

	return select0_blocks(b, i, pos);
}

#ifdef RRR
static int64_t select0_rrr(BitsequenceReader* b, uint64_t i) {
	FileOff start = 0;
	FileOff end = b->sampling_len - 1;

	FileOff acc;
	FileOff mid;
	while(start < (int64_t) end - 1) {
		mid = (start + end) / 2;
		acc = mid * b->sample_rate * BITS_PER_BLOCK - get_field(b, b->offset_sampling, b->sampling_field_bits, mid);

		if(acc < i) {
			if(mid == start)
				break;
			start = mid;
		}
		else {
			if(end == 0)
				break;
			end = mid - 1;
		}
	}

	acc = get_field(b, b->offset_sampling, b->sampling_field_bits, start);
	while(start < b->block_type_len - 1 && acc + b->sample_rate * BITS_PER_BLOCK == get_field(b, b->offset_sampling, b->sampling_field_bits, start + 1)) {
		start++;
		acc += b->sample_rate * BITS_PER_BLOCK;
	}

	acc = start * b->sample_rate * BITS_PER_BLOCK - acc;
	FileOff pos = start * b->sample_rate;
	FileOff super_block = get_field(b, b->offset_super_block_ptrs, b->ptr_width, start);

	FileOff s;
	while(pos < b->block_type_len) {
		s = get_field(b, b->offset_block_types, BLOCK_TYPE_BITS, pos);
		if(acc + BITS_PER_BLOCK - s >= i)
			break;

		super_block += table_class_size(s);
		acc += BITS_PER_BLOCK - s;
		pos++;
	}

	pos *= BITS_PER_BLOCK;

	while(acc < i) {
		int block = table_short_bitmap(s, get_bits(b, b->offset_block_ranks, super_block, table_class_size(s)));

		super_block = super_block + table_class_size(s);
		int count = 0;

		while(acc < i && count < BITS_PER_BLOCK) {
			pos++;
			count++;
			acc += (block & 1) == 0 ? 1 : 0;
			block /= 2;
		}
	}

	return pos - 1;
}
#endif

int64_t bitsequence_reader_select0(BitsequenceReader* b, uint64_t i) {
	if(i == 0 || i > b->len - b->ones)
		return -1;

	switch(b->type) {
	case BITSEQUENCE_REGULAR:
		return select0_blocks(b, i, 0);
	case BITSEQUENCE_RG:
		return select0_rg(b, i);
#ifdef RRR
	case BITSEQUENCE_RRR:
		return select0_rrr(b, i);
#endif
	default:
		return -1;
	}
}

static uint64_t select1_blocks(BitsequenceReader* b, uint64_t i, FileOff pos) {
	FileOff numblocks = DIVUP(b->len, BLOCKW);

	uint32_t j;
	int ones;
	for(;;) {
		j = block_get(b, pos);
		ones = POPCNT32(j);

		if(ones >= i)
			break;

		i -= ones;

		if(++pos > numblocks)
			return b->len;
	}

	// Reverse the block if needed
	j = block_reverse(j);

	return BLOCKW * pos + select_bit(j, i - 1);
}

static uint64_t select1_rg(BitsequenceReader* b, uint64_t i) {
	FileOff lv = 0;
	FileOff rv = b->len / b->s;
	FileOff mid = (lv + rv) / 2;
	uint64_t rankmid = rs_value(b, mid);

	while(lv <= rv) {
		if(rankmid < i)
			lv = mid + 1;
		else
			rv = mid - 1;

		mid = (lv + rv) / 2;
		rankmid = rs_value(b, mid);
	}

	FileOff pos = mid * b->factor;
	i -= rankmid;

	return select1_blocks(b, i, pos);
}

#ifdef RRR
static int64_t select1_rrr(BitsequenceReader* b, uint64_t i) {
	FileOff start = 0;
	FileOff end = b->sampling_len - 1;

	FileOff acc;
	FileOff mid;
	while(start < (int64_t) end - 1) {
		mid = (start + end) / 2;
		acc = get_field(b, b->offset_sampling, b->sampling_field_bits, mid);

		if(acc < i) {
			if(mid == start)
				break;
			start = mid;
		}
		else {
			if(end == 0)
				break;
			end = mid - 1;
		}
	}

	acc = get_field(b, b->offset_sampling, b->sampling_field_bits, start);
	while(start < b->block_type_len - 1 && acc == get_field(b, b->offset_sampling, b->sampling_field_bits, start + 1)) {
		start++;
	}

	acc = get_field(b, b->offset_sampling, b->sampling_field_bits, start);
	FileOff pos = start * b->sample_rate;
	FileOff super_block = get_field(b, b->offset_super_block_ptrs, b->ptr_width, start);

	FileOff s;
	while(pos < b->block_type_len) {
		s = get_field(b, b->offset_block_types, BLOCK_TYPE_BITS, pos);
		if(acc + s >= i)
			break;

		super_block += table_class_size(s);
		acc += s;
		pos++;
	}

	pos *= BITS_PER_BLOCK;

	while(acc < i) {
		int block = table_short_bitmap(s, get_bits(b, b->offset_block_ranks, super_block, table_class_size(s)));

		super_block = super_block + table_class_size(s);
		int count = 0;

		while(acc < i && count < BITS_PER_BLOCK) {
			pos++;
			count++;
			acc += (block & 1) != 0 ? 1 : 0;
			block /= 2;
		}
	}

	return pos - 1;
}
#endif

int64_t bitsequence_reader_select1(BitsequenceReader* b, uint64_t i) {
	if(i == 0 || i > b->ones)
		return -1;

	switch(b->type) {
	case BITSEQUENCE_REGULAR:
		return select1_blocks(b, i, 0);
	case BITSEQUENCE_RG:
		return select1_rg(b, i);
#ifdef RRR
	case BITSEQUENCE_RRR:
		return select1_rrr(b, i);
#endif
	default:
		return -1;
	}
}

int64_t bitsequence_reader_selectprev1(BitsequenceReader* b, uint64_t i) {
	if(bitsequence_reader_access(b, i))
		return i;

	uint64_t r = bitsequence_reader_rank1(b, i);
	if(r == 0)
		return -1;
	return bitsequence_reader_select1(b, r);
}
