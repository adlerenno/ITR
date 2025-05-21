/**
 * @file hgraph.c
 * @author FR
 */

#include "hgraph.h"

#include <stdlib.h>
#include <assert.h>
#include <arith.h>

HGraph* hgraph_init(int rank) {
	HGraph* g = malloc(sizeof(*g));
	if(!g)
		return NULL;

	g->len = 0;
	g->cap = 0;
	g->edges = NULL;
	g->rank = rank;
	return g;
}

void hgraph_destroy(HGraph* g) {
	if(g->edges) {
		for(size_t i = 0; i < g->len; i++)
			free(g->edges[i]);
		free(g->edges);
	}

	free(g);
}

static int hgraph_increase_capacity(HGraph* g, size_t min_cap) {
	size_t old_cap, new_cap;
	HEdge** edges_new;

	if((old_cap = g->cap) == 0)
		min_cap = MAX(min_cap, 8);

	new_cap = NEW_LEN(old_cap, min_cap - old_cap, min_cap >> 1);

	edges_new = realloc(g->edges, new_cap * sizeof(HEdge*));
	if(!edges_new)
		return -1;

	g->cap = new_cap;
	g->edges = edges_new;

	return 0;
}

int hgraph_add_edge(HGraph* g, HEdge* e) {
	if(g->len == g->cap) {
		if(hgraph_increase_capacity(g, g->len + 1) < 0)
			return -1;
	}

	g->edges[g->len++] = e;
	return 0;
}

HEdge* hgraph_edge_get(const HGraph* g, size_t i) {
	assert(i < g->len);

	return g->edges[i];
}

void hgraph_edge_set(HGraph* g, size_t i, HEdge* e) {
	assert(i < g->len);

	g->edges[i] = e;
}

void hgraph_edge_replace(HGraph* g, size_t i, HEdge* e) {
	assert(i < g->len);

	free(g->edges[i]);
	g->edges[i] = e;
}

void hgraph_edge_free(HGraph* g, size_t i) {
	assert(i < g->len);

	free(g->edges[i]);
	g->edges[i] = NULL;
}

void hgraph_fill_holes(HGraph* g) {
	size_t dst = 0;
	size_t len = g->len;
	for(size_t src = 0; src < len; src++) {
		if(g->edges[src] != NULL)
			g->edges[dst++] = g->edges[src];
	}
	g->len = dst;
}

int hedge_cmp(const HEdge* e1, const HEdge* e2) {
	if(e1->label != e2->label)
		return CMP(e1->label, e2->label);

	size_t min_rank = MIN(e1->rank, e2->rank);
	for(size_t i = 0; i < min_rank; i++) {
		if(e1->nodes[i] != e2->nodes[i])
			return CMP(e1->nodes[i], e2->nodes[i]);
	}

	return CMP(e1->rank, e2->rank);
}
