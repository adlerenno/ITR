/**
 * @file reader.h
 * @author FR
 */

#ifndef READER_H
#define READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t FileOff;

#ifndef USE_MMAP
#define CACHE_BLOCK_SIZE 512
#define CACHE_CAPACITY 256 // 512 * 0.75 * 256 equals around 100kb maximum cache
#define BUFFER_SIZE 8128

typedef struct {
	FileOff block;
	int list_prev;
	int list_next;
	uint8_t data[CACHE_BLOCK_SIZE];
} CacheElement;
#endif

typedef struct {
	int fd;

#ifdef USE_MMAP
	uint8_t* mm; // mmap data
#else
	int cache_size;
	CacheElement cache[CACHE_CAPACITY];

	// linked list to store the last used elements
	int cache_list_head;
	int cache_list_tail;

	uint8_t read_buf[BUFFER_SIZE];
#endif

	FileOff bitlen;
	FileOff bitpos;
} FileReader;

FileReader* filereader_init(const char* path);
void filereader_close(FileReader* fr);

typedef struct {
	FileReader* r;
	FileOff bitoff;
} Reader;

void reader_initf(FileReader* fr, Reader* r, FileOff byte_off);
void reader_init(const Reader* src, Reader* dst, FileOff byte_off);

void reader_bitpos(Reader* r, FileOff pos);
#define reader_bytepos(r, pos) reader_bitpos((r), 8 * (pos)) // Using define to increase performance a bit

/**
 * Only allowed when bitoff == 0.
 * No output parameter needed, because the length is always equals with n.
 * If mmap is used, a pointer to the mmapped data at the given position is returned. Also, the data cannot be NULL and must not be freed.
 * If mmap is not used, this functions always returns the same buffer with the read data so if the data are still needed during the
 * next call of `reader_read`, the data should be copied.
 */
const uint8_t* reader_read(Reader* r, size_t n);
bool reader_readbit(Reader* r);

uint64_t reader_readint(Reader* r, int bits);
uint8_t reader_readbyte(Reader* r);
uint64_t reader_vbyte(Reader* r, size_t* bytes);
uint64_t reader_eliasdelta(Reader* r);

#endif
