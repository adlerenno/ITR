/**
 * @file map.c
 * @author FR
 */

#include "map.h"

#include <string.h>

int map_default_cmp(const void* k1, size_t l1, const void* k2, size_t l2) {
	size_t len = l1 < l2 ? l1 : l2;

	int cmp = memcmp(k1, k2, len);
	if(cmp != 0 || l1 == l2)
		return cmp;

	return l1 < l2 ? -1 : 1;
}
