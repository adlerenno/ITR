/**
 * @file rule_creator.h
 * @author FR
 */

#ifndef RULE_CREATOR_H
#define RULE_CREATOR_H

#include <slhr_grammar.h>
#include <repair_types.h>

typedef struct {
	union { // either digram or monogram
		const Digram* digram;
		const Monogram* monogram;
	};
	uint64_t rule_name;
	HGraph* rule;
	bool free; // needed to determine, if the rule was used or not
} RuleCreator;

int rule_creator_digram_init(RuleCreator* c, SLHRGrammar* g, const Digram* digram);
int rule_creator_monogram_init(RuleCreator* c, SLHRGrammar* g, const Monogram* monogram);

#define rule_creator_no_free(c) \
do { \
	(c)->free = false; \
} while(0)
void rule_creator_destroy(RuleCreator* c);

HEdge* rule_creator_digram_new_edge(RuleCreator* c, HEdge* edge_1, HEdge* edge_2);
HEdge* rule_creator_monogram_new_edge(RuleCreator* c, HEdge* edge);

int rule_inserter_edges_for_hyperedge(const HGraph* rule_to_insert, HGraph* rule, HEdge* hyperedge, size_t index);

#endif
