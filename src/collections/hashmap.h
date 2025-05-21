/**
 * @file hashmap.h
 * @author FR
 */

#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <map.h>

typedef uint32_t Hash;

#define HASH64(v) (((Hash*) &(v))[0] ^ ((Hash*) &(v))[1])
#define HASH(v) ((sizeof(v) == 8) ? HASH64(v) : ((Hash) (v)))
#define HASH_COMBINE(hash, new_hash) do { \
	(hash) = (31 * (hash) + (new_hash)); \
} while(0)

#define HASHMAP_NODE_FIELDS \
	bool tree_node; \
	Hash hash; \
	\
	void* key; \
	size_t len_key; \
	\
	void* val; \
	size_t len_val; \
	\
	struct _HashmapNode* next;

typedef struct _HashmapNode {
	HASHMAP_NODE_FIELDS
} _HashmapNode;

// Important:
// To create an enheritance, `_HashmapNode` and `_HashmapTreeNode` start with the same fields.
// This way these structs can be casted to each other.
typedef struct _HashmapTreeNode {
	HASHMAP_NODE_FIELDS

	struct _HashmapTreeNode* prev;
	struct _HashmapTreeNode* parent;
	struct _HashmapTreeNode* left;
	struct _HashmapTreeNode* right;
	bool red;
} _HashmapTreeNode;

typedef Hash (*hash_fn) (const void*, size_t);

typedef struct {
	size_t table_length;
	_HashmapNode** table;

	size_t size;
	size_t threshold;

	compare_fn cmp;
	hash_fn hash;
} Hashmap;

Hashmap* hashmap_init(compare_fn cmp, hash_fn hash);
void hashmap_destroy(Hashmap* m);

size_t hashmap_size(Hashmap* m);
void hashmap_clear(Hashmap* m);

void* hashmap_get(Hashmap* m, const void* key, size_t len_key, size_t* len_val);
bool hashmap_item(Hashmap* m, const void* key, size_t len_key, MapItem* i);

// 0: no element replaced, 1: element replaced, -1: error occured
int hashmap_put(Hashmap* m, const void* key, size_t len_key, const void* val, size_t len_val);

// 0: no element replaced, 1: element replaced
int hashmap_remove(Hashmap* m, const void* key, size_t len_key);

bool hashmap_contains_key(Hashmap* m, const void* key, size_t len_key);

typedef struct {
	Hashmap* map;
	_HashmapNode* next;
	_HashmapNode* current;
	size_t index;
} HashmapIterator;

void hashmap_iter(Hashmap* m, HashmapIterator* it);
bool hashmap_iter_next(HashmapIterator* it, MapItem* i);
const void* hashmap_iter_next_key(HashmapIterator* it, size_t* len_key);
void hashmap_iter_remove(HashmapIterator* it);

#endif
