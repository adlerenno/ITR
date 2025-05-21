/**
 * @file treemap.h
 * @author FR
 */

#ifndef TREEMAP_H
#define TREEMAP_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <map.h>

typedef struct _TreemapEntry {
	void* key;
	size_t len_key;

	void* val;
	size_t len_val;

	struct _TreemapEntry* left;
	struct _TreemapEntry* right;
	struct _TreemapEntry* parent;
	bool color;
	size_t weight;
} _TreemapEntry;

typedef struct {
	size_t size;
	_TreemapEntry* root;
	compare_fn cmp;
} Treemap;

Treemap* treemap_init(compare_fn cmp);
void treemap_destroy(Treemap* m);

size_t treemap_size(Treemap* m);
void treemap_clear(Treemap* m);

void* treemap_get(Treemap* m, const void* key, size_t len_key, size_t* len_val);
bool treemap_item(Treemap* m, const void* key, size_t len_key, MapItem* i);

// 0: no element replaced, 1: element replaced, -1: error occured
int treemap_put(Treemap* m, const void* key, size_t len_key, const void* val, size_t len_val);

// 0: no element removed, 1: element removed
int treemap_remove(Treemap* m, const void* key, size_t len_key);

bool treemap_contains_key(Treemap* m, const void* key, size_t len_key);
ssize_t treemap_index_of(Treemap* m, const void* key, size_t len_key);
bool treemap_get_item_at_index(Treemap* m, size_t index, MapItem* i);

typedef struct {
	_TreemapEntry* next;
} TreemapIterator;

void treemap_iter(Treemap* m, TreemapIterator* it);
bool treemap_iter_next(TreemapIterator* it, MapItem* i);
const void* treemap_iter_next_key(TreemapIterator* it, size_t* len_key);

#endif
