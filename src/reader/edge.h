/**
 * @file edge.h
 * @author FR
 */

#ifndef EDGE_H
#define EDGE_H

#include <stdbool.h>
#include <reader.h>

// It is assumed that the maximum rank never exceeds 128.
// This should already be limited by the Python implementation.
// Since the runtime from decompressing an edge is exponential to its rank, such a high rank should not occur anyway.
#define RANK_MAX 128

typedef struct {
	uint64_t label;
	int rank;
	uint64_t nodes[RANK_MAX]; // memory for a maximum of MAX_RANK edges
} StEdge; // struct edge - in opposite to edge id used in cgraph.h

void edge_read(Reader* r, StEdge* e); // the dst edge is given as a pointer

#endif
