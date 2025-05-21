/**
 * @file hgraph.h
 * @author FR
 */

#ifndef HGRAPH_H
#define HGRAPH_H

#include <stddef.h>
#include <stdint.h>

#define RANK_NONE (-1)

typedef struct {
	uint64_t label;
	size_t rank;
	uint64_t nodes[0];
} HEdge;

// used for malloc to get the size of a edge with the given number of nodes
#define hedge_sizeof(rank) (sizeof(HEdge) + (rank) * sizeof(uint64_t))

typedef struct {
	size_t len;
	size_t cap;
	HEdge** edges;
	int rank;
} HGraph;

HGraph* hgraph_init(int rank);
void hgraph_destroy(HGraph* g);

int hgraph_add_edge(HGraph* g, HEdge* e);
#define hgraph_len(g) ((g)->len)

HEdge* hgraph_edge_get(const HGraph* g, size_t i);
void hgraph_edge_set(HGraph* g, size_t i, HEdge* e);
void hgraph_edge_replace(HGraph* g, size_t i, HEdge* e);

void hgraph_edge_free(HGraph* g, size_t i);
void hgraph_fill_holes(HGraph* g);

int hedge_cmp(const HEdge* e1, const HEdge* e2);

#endif
