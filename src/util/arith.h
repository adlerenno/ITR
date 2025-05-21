/**
 * @file arith.h
 * @author FR
 */

#ifndef ARITH_H
#define ARITH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Determine, if this processor supports unaligned memory access
#if defined(__sparc__)
// Unaligned access will crash your app on a SPARC
#define UNALIGN_ACCESS 0
#elif defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC)
// Unaligned access is too slow on a PowerPC (maybe?)
#define UNALIGN_ACCESS 0
#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
// x86 / x64 are fairly forgiving
#define UNALIGN_ACCESS 1
#elif defined(__ARM_FEATURE_UNALIGNED)
// ARM exists and has unaligned support
#define UNALIGN_ACCESS 1
#else
// unknown architecture
#define UNALIGN_ACCESS 0
#endif

#ifndef __APPLE__
#include <endian.h>
#else
// Using the byte-swap functions on macOS as Macro

#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)
#endif

// min and max functinon
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

// fast integer div up
#define DIVUP(a, b) (((a) + (b) - 1) / (b))
#define BYTE_LEN(n) DIVUP(n, 8)

#define _BIT_LEN(n) ((int) sizeof(uint64_t) * 8 - __builtin_clzll((uint64_t) (n)))
#define BIT_LEN(n) ((n) == 0 ? 0 : _BIT_LEN(n))
#define BITS_NEEDED(n) ((n) == 0 ? 1 : _BIT_LEN(n))

// Compare function
#define CMP(x, y) (((x) > (y)) - ((x) < (y)))

// New length for data structures
#define NEW_LEN(old_cap, min_grow, pref_grow) ((old_cap) + MAX((min_grow), (pref_grow)))

// using gcc's builtin popcnt to enable the popcnt instruction if available
// (popcnt for a byte, 32 bits and 64 bits):
#define POPCNT8(b) (__builtin_popcount((uint8_t) (b)))
#define POPCNT32(b) (__builtin_popcount(b))
#define POPCNT64(b) (__builtin_popcountll(b))

size_t popcnt(const uint8_t* data, size_t size);

uint8_t byte_reverse(uint8_t n);
bool power_of(uint64_t x, uint64_t n);

#ifdef __BMI2__ // Optimize for BMI2 instruction set
#include <x86intrin.h>

#define select_bit(value, n) _tzcnt_u32(_pdep_u32(1U << (n), (value)))
#else
unsigned int select_bit(uint32_t value, unsigned int n);
#endif

#endif
