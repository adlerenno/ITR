/**
 * @file intset.h
 * @author FR
 */

#ifndef INTSET_H
#define INTSET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
	size_t key_count;
	size_t threshold;
	uint8_t encoding;
	uint8_t* key_table;
	size_t table_length;
} Intset;

void intset_init(Intset* s);
void intset_destroy(Intset* s);

int intset_add(Intset* s, uint64_t v);
bool intset_contains(Intset* s, uint64_t v);

#endif
