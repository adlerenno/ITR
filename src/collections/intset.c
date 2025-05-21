/**
 * @file intset.c
 * @author FR
 */

#include "intset.h"

#include <stdlib.h>
#include <arith.h>

#define LOAD_FACTOR 0.75f
#define DEFAULT_CAPACITY 16

#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

typedef struct {
	uint64_t key;
	uint64_t value;
} TableElement;

static inline uint8_t intset_value_encoding(int64_t v) {
	if(v < INT32_MIN || v > INT32_MAX)
		return INTSET_ENC_INT64;
	else if(v < INT16_MIN || v > INT16_MAX)
		return INTSET_ENC_INT32;
	else
		return INTSET_ENC_INT16;
}

static inline void intset_get_encoded(uint8_t* table, size_t pos, uint8_t enc, TableElement* e) {
	if(enc == INTSET_ENC_INT64) {
		e->key = ((uint64_t*) table)[2 * pos + 0];
		e->value = ((uint64_t*) table)[2 * pos + 1];
	}
	else if(enc == INTSET_ENC_INT32) {
		e->key = ((uint32_t*) table)[2 * pos + 0];
		e->value = ((uint32_t*) table)[2 * pos + 1];
	}
	else {
		e->key = ((uint16_t*) table)[2 * pos + 0];
		e->value = ((uint16_t*) table)[2 * pos + 1];
	}
}

static inline void intset_set_encoded(uint8_t* table, size_t pos, uint8_t enc, TableElement* e) {
	if(enc == INTSET_ENC_INT64) {
		((uint64_t*) table)[2 * pos + 0] = e->key;
		((uint64_t*) table)[2 * pos + 1] = e->value;
	}
	else if(enc == INTSET_ENC_INT32) {
		((uint32_t*) table)[2 * pos + 0] = e->key;
		((uint32_t*) table)[2 * pos + 1] = e->value;
	}
	else {
		((uint16_t*) table)[2 * pos + 0] = e->key;
		((uint16_t*) table)[2 * pos + 1] = e->value;
	}
}

static inline void intset_or_encoded_value(uint8_t* table, size_t pos, uint8_t enc, uint64_t v) {
	if(enc == INTSET_ENC_INT64)
		((uint64_t*) table)[2 * pos + 1] |= v;
	else if(enc == INTSET_ENC_INT32)
		((uint32_t*) table)[2 * pos + 1] |= v;
	else
		((uint16_t*) table)[2 * pos + 1] |= v;
}

void intset_init(Intset* s) {
	s->key_count = 0;
	s->threshold = 0;
	s->encoding = INTSET_ENC_INT16;
	s->key_table = NULL;
	s->table_length = 0;
}

void intset_destroy(Intset* s) {
	if(s->key_table)
		free(s->key_table);
}

static inline size_t place(Intset* s, uint64_t item) {
	return (item * 0x9E3779B97F4A7C15L) >> __builtin_clzll(s->table_length - 1);
}

static bool intset_locate_key(Intset* s, uint64_t key, size_t* pos) {
	for(size_t i = place(s, key);; i = (i + 1) & (s->table_length - 1)) {
		TableElement e;
		intset_get_encoded(s->key_table, i, s->encoding, &e);
		if(e.value == 0) { // entry is unused if value is 0
			if(pos)
				*pos = i;
			return false;
		}
		if(e.key == key) {
			if(pos)
				*pos = i;
			return true;
		}
	}
}

static void intset_add_resize(Intset* s, TableElement* e) {
	for(size_t i = place(s, e->key);; i = (i + 1) & (s->table_length - 1)) {
		TableElement tmp;
		intset_get_encoded(s->key_table, i, s->encoding, &tmp);
		if(tmp.value == 0) { // entry is unused if value is 0
			intset_set_encoded(s->key_table, i, s->encoding, e);
			return;
		}
		if(tmp.key == e->key) {
			intset_or_encoded_value(s->key_table, i, s->encoding, e->value);
			return;
		}
	}
}

static int intset_resize(Intset* s, size_t new_size, uint8_t encoding) {
	uint8_t* new_key_table = calloc(new_size, 2 * encoding);
	if(!new_key_table)
		return -1;

	uint8_t old_encoding = s->encoding;
	uint8_t* old_key_table = s->key_table;
	size_t old_capacity = s->table_length;

	s->threshold = new_size * LOAD_FACTOR;
	s->encoding = encoding;
	s->key_table = new_key_table;
	s->table_length = new_size;

	if(old_key_table && s->key_count > 0) {
		for(size_t i = 0; i < old_capacity; i++) {
			TableElement e;
			intset_get_encoded(old_key_table, i, old_encoding, &e);
			if(e.value != 0) {
				if(s->encoding != old_encoding) { // modify key and value
					// only does work if new encoding is duplicate of `old_encoding`
					uint64_t shift = (e.key % (s->encoding / old_encoding)) * (8 * old_encoding);

					e.key = (e.key * old_encoding) / s->encoding;
					e.value = e.value << shift;
				}

				intset_add_resize(s, &e);
			}
		}
	}

	free(old_key_table);
	return 0;
}

int intset_add(Intset* s, uint64_t v) {
	size_t bucket_size = 8 * s->encoding;
	uint64_t key = v / bucket_size;

	uint8_t enc = intset_value_encoding(key);

	if(!s->key_table || s->table_length == 0 || enc > s->encoding) {
		size_t new_size = MAX(s->table_length << 1, DEFAULT_CAPACITY);
		uint8_t old_encoding = s->encoding;
		uint8_t new_encoding = MAX(enc, old_encoding);

		if(intset_resize(s, new_size, new_encoding) < 0)
			return -1;

		if(old_encoding != new_encoding) { // recalculation needed
			bucket_size = 8 * new_encoding;
			key = v / bucket_size;
		}
	}

	uint64_t bucket_bits = 1 << (v % bucket_size);

	size_t i;
	if(intset_locate_key(s, key, &i)) {
		// if the bucket already exists: just set the bit of the value to 1
		intset_or_encoded_value(s->key_table, i, s->encoding, bucket_bits);
		return 0;
	}

	if(s->key_count > s->threshold) {
		if(intset_resize(s, s->table_length << 1, s->encoding) < 0)
			return -1;

		// No recalculation of bucket key needed because the encoding did not change.
		// Relocate the bucket because it probably has changed after resizing.
		intset_locate_key(s, key, &i);
	}

	TableElement e;
	e.key = key;
	e.value = bucket_bits;

	intset_set_encoded(s->key_table, i, s->encoding, &e);
	s->key_count++;

	return 0;
}

bool intset_contains(Intset* s, uint64_t v) {
	if(!s->key_table || s->table_length == 0)
		return false;

	size_t bucket_size = 8 * s->encoding;
	uint64_t key = v / bucket_size;

	if(intset_value_encoding(key) > s->encoding)
		return false;

	size_t i;
	TableElement e;
	if(intset_locate_key(s, key, &i)) {
		intset_get_encoded(s->key_table, i, s->encoding, &e);

		uint64_t bucket_bit = (1 << (v % bucket_size));
		return (e.value & bucket_bit) != 0;
	}

	return false;
}
