/**
 * @file repair_types.h
 * @author FR
 */

#ifndef REPAIR_TYPES_H
#define REPAIR_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint64_t label;
	size_t conn_type;
} AdjacencyType; // adjancency type

typedef struct {
	AdjacencyType adj0;
	AdjacencyType adj1;
} Digram; // digram type

typedef struct {
	uint64_t label;
	size_t conn0;
	size_t conn1;
} Monogram;

#endif
