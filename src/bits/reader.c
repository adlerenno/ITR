/**
 * @file reader.c
 * @author FR
 */

#include "reader.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <assert.h>

#ifdef USE_MMAP
#include <sys/mman.h>
#else
#include <string.h>

// to ensure, that the cache capacity does not exceeds the maximum of int
#include <limits.h>
#endif

#include <panic.h>
#include <arith.h>
#include <memdup.h>

#define unlikely(x) (__builtin_expect((x), 0))

// The filereader will be stored on the heap
FileReader* filereader_init(const char* path) {
	int fd = open(path, O_RDONLY);
	if(fd < 0)
		return NULL;

	struct stat st;
	if(fstat(fd, &st) < 0)
		goto err_0;

	FileOff size = st.st_size;

#ifdef USE_MMAP
	// mmapping the file to the memory - available at pointer f
	uint8_t* mm = (uint8_t*) mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(mm == MAP_FAILED)
		goto err_0;
#endif

	FileReader* r = malloc(sizeof(*r));
	if(!r)
		goto err_1;

	r->fd = fd;
#ifdef USE_MMAP
	r->mm = mm;
#else
	r->cache_size = 0;
	for(int i = 0; i < CACHE_CAPACITY; i++)
		r->cache[i].block = -1;
	r->cache_list_head = -1;
	r->cache_list_tail = -1;
#endif

	r->bitlen = 8 * size;
	r->bitpos = 0;

	return r;

err_1:
#ifdef USE_MMAP
	munmap(mm, size);
#endif
err_0:
	close(fd);
	return NULL;
}

void filereader_close(FileReader* r) {
#ifdef USE_MMAP
	munmap(r->mm, r->bitlen / 8); // unmapping the file
#endif

	close(r->fd); // closing the file descriptor
	free(r);
}

// normal readers will not be allocated via malloc
void reader_initf(FileReader* fr, Reader* dst, FileOff byte_off) {
	dst->r = fr;
	dst->bitoff = 8 * byte_off;

	reader_bitpos(dst, 0);
}

// normal readers will not be allocated via malloc
void reader_init(const Reader* src, Reader* dst, FileOff byte_off) {
	dst->r = src->r;
	dst->bitoff = src->bitoff + 8 * byte_off;

	reader_bitpos(dst, 0);
}

void reader_bitpos(Reader* r, FileOff pos) {
	pos += r->bitoff;
	if(unlikely(pos < 0 || pos >= r->r->bitlen))
		panic("illegal bit offset %" PRIu64 " with bit length %" PRIu64, pos, r->r->bitlen);

	r->r->bitpos = pos;
}

static inline void check_remaining(Reader* r, FileOff n) {
	if(unlikely(r->r->bitpos + n > r->r->bitlen))
		panic("trying to read %" PRIu64 " bits but only %" PRIu64 " are available", n, r->r->bitlen - r->r->bitpos);
}

#ifndef USE_MMAP
// check assertions of the cache size
static_assert(CACHE_CAPACITY > 0 && (CACHE_CAPACITY & (CACHE_CAPACITY - 1)) == 0, "CACHE_CAPACITY must be a power of two");
static_assert(CACHE_CAPACITY <= INT_MAX, "CACHE_CAPACITY must not exceed the maximum of int");

static inline int place(FileOff block) {
	return (block * 0x9E3779B97F4A7C15L) >> __builtin_clzll(CACHE_CAPACITY - 1);
}

static CacheElement* cache_locate(FileReader* r, FileOff block, int* first_pos, int* pos) {
	int first = place(block);
	for(int i = first;; i = (i + 1) & (CACHE_CAPACITY - 1)) {
		CacheElement* e = &r->cache[i];
		if(e->block == -1) { // entry is unused if value is -1
			*first_pos = first;
			*pos = i;
			return NULL;
		}
		if(e->block == block) {
			*first_pos = first;
			*pos = i;
			return e;
		}
	}
}

static int cache_remove_lowest(FileReader* r) {
	int tail;
	if((tail = r->cache_list_tail) == -1)
		return -1;

	CacheElement* lowest = &r->cache[tail];
	lowest->block = -1;

	int prev = lowest->list_prev;
	r->cache_list_tail = prev;

	if(prev == -1)
		r->cache_list_head = -1;
	else
		r->cache[prev].list_next = -1;

	r->cache_size--;
	return tail;
}

static const uint8_t* read_block(FileReader* r, FileOff block) {
	int f, i, lo;
	CacheElement* e = cache_locate(r, block, &f, &i);
	if(!e) {
		// to keep the runtimes low, the size of the cache must not exceed 75% of the capacity
		if(r->cache_size >= (int)(0.75f * CACHE_CAPACITY)) {
			if((lo = cache_remove_lowest(r)) >= 0) {
				// checking, if the lowest cache value was skipped in `cache_locate`
				if(f <= i) {
					if(f <= lo && lo <= i)
						i = lo;
				}
				else {
					if(f <= lo || lo <= i)
						i = lo;
				}
			}
		}

		r->cache_size++;

		e = &r->cache[i];
		e->block = block;
		if(pread(r->fd, e->data, CACHE_BLOCK_SIZE, CACHE_BLOCK_SIZE * block) < 0)
			panic("failed to read a block at position %" PRIu64, CACHE_BLOCK_SIZE * block);

		// move new element to front
		int h = r->cache_list_head;

		e->list_prev = -1;
		e->list_next = h;

		r->cache_list_head = i;
		if(h == -1)
			r->cache_list_tail = i;
		else
			r->cache[h].list_prev = i;
	}
	else {
		// move existing element to front

		if(r->cache_list_head != i) {
			// current is not at the front

			int prev = e->list_prev;
			int next = e->list_next;

			if(r->cache_list_tail == i) {
				// current element is at the end
				r->cache[prev].list_next = -1;
				r->cache_list_tail = prev;
			}
			else {
				// n is at the middle
				r->cache[prev].list_next = next;
				r->cache[next].list_prev = prev;
			}

			r->cache[r->cache_list_head].list_prev = i;
			e->list_prev = -1;
			e->list_next = r->cache_list_head;
			r->cache_list_head = i;
		}
	}

	return e->data;
}

static inline void read_bytes(Reader* r, void* data, FileOff byteindex, size_t nbytes) {
	FileReader* fr = r->r;

	// determine the offset of the current block
	FileOff block = byteindex / CACHE_BLOCK_SIZE;
	size_t block_index = byteindex % CACHE_BLOCK_SIZE;

	const uint8_t* block_buf = read_block(fr, block);

	size_t read_size = MIN(CACHE_BLOCK_SIZE - block_index, nbytes);
	memcpy(data, block_buf + block_index, read_size);

	while(read_size < nbytes) {
		block_buf = read_block(fr, ++block);

		size_t copy_size = MIN(CACHE_BLOCK_SIZE, nbytes - read_size);

		memcpy(data + read_size, block_buf, copy_size);
		read_size += copy_size;
	}
}
#endif

static inline const uint8_t* get_bytes(Reader* r, size_t byte_pos, size_t n) {
	uint8_t* data;
	#ifdef USE_MMAP
		data = r->r->mm + byte_pos;
	#else
		if(n > BUFFER_SIZE)
			panic("number of bytes (%zu) exceeds the maximum buffer size (%d)", n, BUFFER_SIZE);

		data = r->r->read_buf;
		if(n > 0)
			read_bytes(r, data, byteindex, n);
	#endif

	return data;
}

const uint8_t* reader_read(Reader* r, size_t n) {
	check_remaining(r, 8 * n);

	FileOff bitpos = r->r->bitpos;
	FileOff byteindex = bitpos / 8;
	FileOff bitoff = bitpos % 8;

	if(unlikely(bitoff > 0))
		panic("can only read bytes at bitoff == 0");

	const uint8_t* data = get_bytes(r, byteindex, n);

	r->r->bitpos += 8 * n;
	return data;
}

bool reader_readbit(Reader* r) {
	check_remaining(r, 1);

	FileOff byteindex = r->r->bitpos / 8;
	FileOff bitoff = r->r->bitpos % 8;

	bool b;
#ifdef USE_MMAP
	b = ((r->r->mm[byteindex] >> (8 - bitoff - 1)) & 1) == 1;
#else
	uint8_t byte;
	read_bytes(r, &byte, byteindex, sizeof(byte));

	b = ((byte >> (8 - bitoff - 1)) & 1) == 1;
#endif

	r->r->bitpos++;
	return b;
}

static uint64_t to_int(const uint8_t* data, int n) {
	assert(n <= 8);

	uint64_t val;

#if UNALIGN_ACCESS
	// Performance hack: if unaligned memory access is supported, use the beXXtoh-functions to convert the big endian value to the host value.
	if(n == 8)
		val = be64toh(*((uint64_t*) data));
	else {
		if(n & 0b100) {
			val = be32toh(*((uint32_t*) data));
			data += 4;
		} else
			val = 0;
		if(n & 0b010) {
			val = (val << 16) | be16toh(*((uint16_t*) data));
			data += 2;
		}
		if(n & 0b001)
			val = (val << 8) | *data;
	}
#else
	val = 0;
	// If no unaligned memory access is supported: use the slower function, that only accesses single bytes.
	for(int i = 0; i < n; i++)
		val = (val << 8) | (uint64_t) data[i];
#endif

	return val;
}

#ifdef __SIZEOF_INT128__
static inline __uint128_t to_int128(const uint8_t* data, int n) {
	__uint128_t val = 0;
	for(int i = 0; i < n; i++)
		val = val << 8 | (uint64_t) data[i];

	return val;
}
#endif

#define uint_extract(v, p, n) (((v) >> (p)) & ((((typeof(v)) 1) << (n)) - 1))

uint64_t reader_readint(Reader* r, int bits) {
	if(unlikely(bits > 8 * sizeof(uint64_t)))
		panic("number of bits (%zu) exceeds bit width (%zu)", (size_t) bits, 8 * sizeof(uint64_t));

	check_remaining(r, bits);

	FileOff pos = r->r->bitpos;
	FileOff byte_pos = pos / 8;
	int bitoff = pos % 8;
	int byte_len = BYTE_LEN(pos + bits) - byte_pos;

	const uint8_t* data = get_bytes(r, byte_pos, byte_len);
	int shift;

	uint64_t res;
	if(!bitoff) { // simplest case: bit data starts at a full byte
		if(bits < 8) // extract less than a single byte and shift its value
			res = *data >> (8 - bits);
		else if(bits == 8) // extract exactly a single byte
			res = *data;
		else {
			int bitlen = 8 * byte_len;

			if(bitlen == bits) // extract an integer of full bytes
				res = to_int(data, byte_len);
			else { // no masking needed, just shifting
				shift = bitlen - bits;
				res = to_int(data, byte_len) >> shift;
			}
		}

		r->r->bitpos = pos + bits;
		return res;
	}

	shift = 8 * byte_len - bits - bitoff;
	uint64_t val;

	if(unlikely(byte_len > sizeof(uint64_t))) {
		// Unlikely scenario:
		// The maximum number of bytes of uint64_t is 8.
		// But because the bit position of an number may not start at a full byte, it may be necessary to read more than 8 bytes.
		// In this case, the integer is first treated as an 128 bit unsigned integer, before shifting its content to fit into an uint64_t.
		// If the compiler does not support the type __uint128_t, this function panics.

#ifndef __SIZEOF_INT128__
		panic("number of bytes (%d) exceeds the maximum number of bytes (%lu)", byte_len, sizeof(uint64_t));
#else
		uint64_t mask = (((uint64_t) 1) << bits) - 1;
		val = to_int128(data, byte_len) >> shift;
		res = val & mask;
#endif
	}
	else {
		val = to_int(data, byte_len);
		res = uint_extract(val, shift, bits);
	}

	r->r->bitpos = pos + bits;
	return res;
}

uint8_t reader_readbyte(Reader* r) {
	check_remaining(r, 8);

	FileOff bitpos = r->r->bitpos;
	FileOff byteindex = bitpos / 8;
	FileOff bitoff = bitpos % 8;

	r->r->bitpos += 8;

	if(bitoff == 0) {
#ifdef USE_MMAP
		return r->r->mm[byteindex];
#else
		uint8_t byte;
		read_bytes(r, &byte, byteindex, sizeof(byte));

		return byte;
#endif
	}

#ifdef USE_MMAP
	uint8_t* data = r->r->mm + byteindex;
#else
	uint8_t data[2];
	read_bytes(r, data, byteindex, sizeof(data));
#endif

	return (data[0] << bitoff) | (data[1] >> (8 - bitoff));
}

// nbytes is returned as a pointer, so calls to this function look much better
uint64_t reader_vbyte(Reader* r, size_t* nbytes) {
	uint64_t val = 0;
	size_t n = 0;
	int shift = 0;

	uint8_t nibble;
	for(;;) {
		nibble = reader_readbyte(r);
		n++;

		val |= ((uint64_t) (nibble & 0x7f)) << shift;
		shift += 7;

		if((nibble & 0x80) > 0)
			break;
	}

	if(nbytes) // Can be NULL
		*nbytes = n;
	return val;
}

uint64_t reader_eliasdelta(Reader* r) {
	int len = 1;
	int lenoflen = 0;

	while(!reader_readbit(r))
		lenoflen++;

	int i;
	for(i = 0; i < lenoflen; i++) {
		len <<= 1;
		if(reader_readbit(r))
			len |= 1;
	}

	uint64_t n = 1;
	for(i = 0; i < len - 1; i++) {
		n <<= 1;
		if(reader_readbit(r))
			n |= 1;
	}

	return --n; // decrement by 1 to decode 0
}
