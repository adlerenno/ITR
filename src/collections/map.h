/**
 * @file map.h
 * @author FR
 */

#ifndef MAP_H
#define MAP_H

#include <stddef.h>

typedef struct {
	const void* key;
	size_t len_key;

	void* val;
	size_t len_val;
} MapItem;

typedef int (*compare_fn) (const void*, size_t, const void*, size_t);

int map_default_cmp(const void* k1, size_t l1, const void* k2, size_t l2);

#endif
