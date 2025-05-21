/**
 * @file grammar.c
 * @author FR
 */

#include "grammar.h"

#include <string.h>
#include <stdlib.h>
#include <reader.h>
#include <cgraph.h>
#include <edge.h>
#include <startsymbol.h>
#include <rules.h>
#include <k2.h>
#include <hgraph.h>
#include <ringqueue.h>
#include <arith.h>

GrammarReader* grammar_init(Reader* r) {
	size_t nbytes;
	uint64_t node_count = reader_vbyte(r, &nbytes);
	FileOff off = nbytes;

	bool with_nt_table = reader_readbyte(r);
	off++;

	uint64_t lenstart = reader_vbyte(r, &nbytes);
	off += nbytes;

	uint64_t lenrules; // len of rules only exists, if NT table exists
	if(with_nt_table) {
		lenrules = reader_vbyte(r, &nbytes);
		off += nbytes;
	}

	FileOff offrules = off + lenstart;
	FileOff offnts;
	if(with_nt_table)
		offnts = offrules + lenrules;

	Reader rt;
	reader_init(r, &rt, off);
	StartSymbolReader* start = startsymbol_init(&rt);
	if(!start)
		return NULL;

	reader_init(r, &rt, offrules);
	RulesReader* rules = rules_init(&rt);
	if(!rules)
		goto err0;

	K2Reader* nt_table;
	if(with_nt_table) {
		reader_init(r, &rt, offnts);
		nt_table = k2_init(&rt);
		if(!nt_table)
			goto err1;
	}
	else
		nt_table = NULL;

	start->nt_table = nt_table;
	start->terminals = rules->first_nt;

	GrammarReader* g = malloc(sizeof(*g));
	if(!g)
		goto err2;

	g->node_count = node_count;
	g->start = start;
	g->rules = rules;
	g->nt_table = nt_table;

	return g;

err2:
	if(with_nt_table)
		k2_destroy(nt_table);
err1:
	rules_destroy(rules);
err0:
	startsymbol_destroy(start);
	return NULL;
}

void grammar_destroy(GrammarReader* g) {
	startsymbol_destroy(g->start);
	rules_destroy(g->rules);
	if(g->nt_table)
		k2_destroy(g->nt_table);
	free(g);
}

void grammar_neighborhood(GrammarReader* g, int query_type, CGraphRank rank, const CGraphNode* nodes, GrammarNeighborhood* nb) {
//	if(label != CGRAPH_LABELS_ALL && label >= g->rules->first_nt) { // label does not exists as a terminal so no neighbors exists
//		nb->has_next = false;
//		return;
//	}

	nb->has_next = true;
    nb->rank = rank;
    nb->nodes = nodes;
	nb->g = g;

	startsymbol_neighborhood(g->start, query_type, rank, nodes, &nb->start);
	ringqueue_init(&nb->queue, 0);
}

static bool hedge_contains(HEdge* e, uint64_t n) {
	for(int i = 0; i < e->rank; i++)
		if(e->nodes[i] == n)
			return true;
	return false;
}

// This function works by only allocating to the stack.
// No malloc is used so no memory must be freed.
static int decompress(GrammarNeighborhood* nb, HEdge* e, CGraphEdge* res) {

	uint64_t first_nt;
	if (e->label < (first_nt = nb->g->rules->first_nt)) { // terminal found
//		if(nb->label != CGRAPH_LABELS_ALL && e->label != nb->label) // specific edges wanted and label does not match
//			return 0;
        if(nb->rank != CGRAPH_NODES_ALL && nb->start.query_type == CGRAPH_EXACT_QUERY && nb->rank != e->rank)
            return 0;
//        if (nb->start.query_type == CGRAPH_EXACT_QUERY || nb->start.query_type == CGRAPH_CONTAINS_QUERY) { //TODO: This checks only exact order.
//            for (int i = 0; i < nb->rank; i++) {
//                if (nb->nodes[i] != CGRAPH_NODES_ALL && e->nodes[i] != nb->nodes[i])
//                    return 0;
//            }
//        }

        if(res) { // res may be NULL
            res->rank = e->rank;
            res->label = e->label;
            memcpy(res->nodes, e->nodes, e->rank * sizeof (CGraphNode));
        }

        return 1;
	}

//	K2Reader* nt_table;
//	if(nb->label != CGRAPH_LABELS_ALL && (nt_table = nb->g->nt_table)) { // specific edges wanted and we have the nt table
//		if(!k2_get(nt_table, e->label - first_nt, nb->label))
//			return 0;
//	}

    // Check if the edge is adjacent to the destination node.
    for (int i=0; i<nb->rank; i++)
    {
        if(nb->nodes[i] != CGRAPH_NODES_ALL && !hedge_contains(e, nb->nodes[i]))
            return 0;
    }

    StEdge rule[MAX_RULE_SIZE]; // Rule if the non-terminal
	size_t rlen = rules_get(nb->g->rules, e->label, rule); // load the rule to variable `rule` - already on stack

	for(size_t i = 0; i < rlen; i++) {
		StEdge* ei = rule + i;

		HEdge* enew = malloc(hedge_sizeof(ei->rank));
		if(!enew)
			return -1;

		enew->label = ei->label;
		enew->rank = ei->rank;

		for(int j = 0; j < ei->rank; j++)
			enew->nodes[j] = e->nodes[ei->nodes[j]];

		// adding all new edges to the queue
		if(ringqueue_enqueue(&nb->queue, enew) < 0)
			return -1;
	}

	return 0;
}

static int grammar_neighborhood_next_enqueue(GrammarNeighborhood* nb) {
	StEdge e;
	if(!startsymbol_neighborhood_next(&nb->start, &e))
		return 0;

	HEdge* edge = malloc(hedge_sizeof(e.rank));
	if(!edge)
		return -1;

	edge->label = e.label;
	edge->rank = e.rank;
	memcpy(edge->nodes, e.nodes, e.rank * sizeof(uint64_t));

	if(ringqueue_enqueue(&nb->queue, edge) < 0)
		return -1;

	return 1;
}

int grammar_neighborhood_next(GrammarNeighborhood* nb, CGraphEdge* n) {
	if(!nb->has_next)
		return 0;

	for(;;) {
		if(ringqueue_empty(&nb->queue)) {
			// determine the next edge from the startsymbol
			switch(grammar_neighborhood_next_enqueue(nb)) {
			case 0: // no further neighbors exist
				grammar_neighborhood_finish(nb);
				return 0;
			case 1:
				break;  // Found one possible edge in the start rule, process it.
			default:  // Error case, it should not occur.
				return -1;
			}
		}

		// Do the decompression
		while(!ringqueue_empty(&nb->queue)) {
			HEdge* edge = ringqueue_dequeue(&nb->queue);

			int res = decompress(nb, edge, n);
			free(edge);

			switch(res) {
			case 0:
				break;
			case 1:
				return 1;
			default:
				return -1;
			}
		}
	}
}

void grammar_neighborhood_finish(GrammarNeighborhood* nb) {
	if(nb->has_next) {
		startsymbol_neighborhood_finish(&nb->start);

		while(!ringqueue_empty(&nb->queue))
			free(ringqueue_dequeue(&nb->queue));
		ringqueue_destroy(&nb->queue);

		nb->has_next = false;
	}
}
