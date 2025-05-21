/**
 * @file slhr_grammar.h
 * @author FR
 */

#ifndef SLHR_GRAMMAR_H
#define SLHR_GRAMMAR_H

#include <stddef.h>
#include <stdbool.h>
#include <hgraph.h>

#define START_SYMBOL ((uint64_t) 0)

typedef struct {
	uint64_t min_nt;
	HGraph* start_symbol;
    size_t* rank_of_terminal;

	size_t rule_max; // eventuell entfernen?
	size_t rules_cap;
	HGraph** rules;
} SLHRGrammar;

SLHRGrammar* slhr_grammar_init(HGraph* graph, uint64_t terminals);
void slhr_grammar_destroy(SLHRGrammar* g);

// Used to iterate over the rules
bool slhr_grammar_next_rule(SLHRGrammar* g, uint64_t* next, uint64_t* rule);

// For the following two methods, the rule of symbol has to exist in the grammar
HGraph* slhr_grammar_rule_get(SLHRGrammar* g, uint64_t symbol);
void slhr_grammar_rule_del(SLHRGrammar* g, uint64_t symbol);

int slhr_grammar_rule_add(SLHRGrammar* g, uint64_t symbol, HGraph* graph);

#define slhr_grammar_is_terminal(g, symbol) ((symbol) < (g)->min_nt)
int slhr_grammar_rank_of_rule(const SLHRGrammar* g, uint64_t symbol);
size_t slhr_grammar_size_of_rule(const SLHRGrammar* g, uint64_t symbol);
uint64_t slhr_grammar_unused_nt(const SLHRGrammar* g);

#endif
