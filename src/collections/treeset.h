/**
 * @file treeset.h
 * @author FR
 */

#ifndef TREESET_H
#define TREESET_H

#include <treemap.h>

typedef Treemap Treeset;

static inline Treeset* treeset_init(compare_fn cmp) {
	return treemap_init(cmp);
}

static inline void treeset_destroy(Treeset* s) {
	treemap_destroy(s);
}

static inline size_t treeset_size(Treeset* s) {
	return treemap_size(s);
}

static inline void treeset_clear(Treeset* s) {
	treemap_clear(s);
}

// 0: no element replaced, 1: element replaced, -1: error occured
static inline int treeset_add(Treeset* s, const void* val, size_t len_val) {
	return treemap_put(s, val, len_val, NULL, 0) == 1;
}

static inline bool treeset_remove(Treeset* s, const void* val, size_t len_val) {
	return treemap_remove(s, val, len_val) == 1;
}

static inline void* treeset_get(Treeset* s, size_t index, size_t* len_val) {
	MapItem item;
	if(!treemap_get_item_at_index(s, index, &item)) {
		if(len_val)
			*len_val = 0;
	}

	if(len_val)
		*len_val = item.len_key;
	return (void*) item.key;
}

static inline bool treeset_contains(Treeset* s, const void* val, size_t len_val) {
	return treemap_contains_key(s, val, len_val);
}

static inline ssize_t treeset_index_of(Treeset* s, const void* val, size_t len_val) {
	return treemap_index_of(s, val, len_val);
}

typedef TreemapIterator TreesetIterator;

static inline void treeset_iter(Treeset* s, TreesetIterator* it) {
	treemap_iter(s, it);
}

static inline const void* treeset_iter_next(TreesetIterator* it, size_t* len_val) {
	return treemap_iter_next_key(it, len_val);
}

#endif
