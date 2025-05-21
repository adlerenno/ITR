/**
 * @file hashset.h
 * @author FR
 */

#ifndef HASHSET_H
#define HASHSET_H

#include <hashmap.h>

typedef Hashmap Hashset;

static inline Hashset* hashset_init(compare_fn cmp, hash_fn hash) {
	return hashmap_init(cmp, hash);
}

static inline void hashset_destroy(Hashset* s) {
	hashmap_destroy(s);
}

static inline size_t hashset_size(Hashset* s) {
	return hashmap_size(s);
}

static inline void hashset_clear(Hashset* s) {
	hashmap_clear(s);
}

// 0: no element replaced, 1: element replaced, -1: error occured
static inline int hashset_add(Hashset* s, const void* val, size_t len_val) {
	return hashmap_put(s, val, len_val, NULL, 0);
}

static inline bool hashset_remove(Hashset* s, const void* val, size_t len_val) {
	return hashmap_remove(s, val, len_val) == 1;
}

static inline bool hashset_contains(Hashset* s, const void* val, size_t len_val) {
	return hashmap_contains_key(s, val, len_val);
}

typedef HashmapIterator HashsetIterator;

static inline void hashset_iter(Hashset* s, HashsetIterator* it) {
	hashmap_iter(s, it);
}

static inline const void* hashset_iter_next(HashsetIterator* it, size_t* len_val) {
	return hashmap_iter_next_key(it, len_val);
}

#endif
