/**
 * @file grammar.h
 * @author FR
 */

#ifndef GRAMMAR_H
#define GRAMMAR_H

#include <cgraph.h>
#include <reader.h>
#include <startsymbol.h>
#include <rules.h>

typedef struct {
	uint64_t node_count;
	StartSymbolReader* start;
	RulesReader* rules;
	K2Reader* nt_table;
} GrammarReader;

GrammarReader* grammar_init(Reader* r);
void grammar_destroy(GrammarReader* g);

typedef struct {
	bool has_next;

    CGraphRank rank;
    const CGraphNode* nodes;

	GrammarReader* g;
	StartSymbolNeighborhood start;
	RingQueue queue;
} GrammarNeighborhood;

void grammar_neighborhood(GrammarReader* g, int query_type, CGraphRank rank, const CGraphNode* nodes, GrammarNeighborhood* nb);

// return value:
// 1: next element exists
// 0: no next element exists
// -1: error occured
int grammar_neighborhood_next(GrammarNeighborhood* nb, CGraphEdge* n);
void grammar_neighborhood_finish(GrammarNeighborhood* nb); // needed if not iterated to the end

#endif
