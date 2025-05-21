/**
 * @file slhr_grammar.c
 * @author FR
 */

#include "slhr_grammar.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <hgraph.h>

SLHRGrammar* slhr_grammar_init(HGraph* graph, uint64_t min_nt) {
	SLHRGrammar* g = malloc(sizeof(*g));
	if(!g)
		return NULL;
    size_t *rank_of_terminal = malloc(min_nt * sizeof(size_t));
    if (!rank_of_terminal)
    {
        free(g);
        return NULL;
    }
    g->rank_of_terminal = rank_of_terminal;
    for (size_t i = 0; i < graph->len; i++)
    {
        rank_of_terminal[graph->edges[i]->label] = graph->edges[i]->rank;
    }

	g->min_nt = min_nt;
	g->start_symbol = graph;
	g->rule_max = 0;
	g->rules_cap = 0;
	g->rules = NULL;

	return g;
}

void slhr_grammar_destroy(SLHRGrammar* g) {
	hgraph_destroy(g->start_symbol);
    if (g->rank_of_terminal)
        free(g->rank_of_terminal);
	for(size_t i = 0; i < g->rules_cap; i++)
		if(g->rules[i])
			hgraph_destroy(g->rules[i]);
	free(g->rules);
	free(g);
}

#define EMPTY_NEXT ((uint64_t) -1)

bool slhr_grammar_next_rule(SLHRGrammar* g, uint64_t* next, uint64_t* rule) {
	if(*next == EMPTY_NEXT)
		return false;

	*rule = *next;

	uint64_t r = *next == START_SYMBOL ? g->min_nt : (*next + 1);
	*next = EMPTY_NEXT;

	for(; r <= g->rule_max; r++) {
		if(g->rules[r - g->min_nt] != NULL) {
			*next = r;
			break;
		}
	}

	return true;
}

HGraph* slhr_grammar_rule_get(SLHRGrammar* g, uint64_t symbol) {
	if(symbol == START_SYMBOL)
		return g->start_symbol;

	assert(symbol >= g->min_nt);
	assert(symbol <= g->rule_max);

	HGraph* rule = g->rules[symbol - g->min_nt];
	assert(rule != NULL);

	return rule;
}

void slhr_grammar_rule_del(SLHRGrammar* g, uint64_t symbol) {
	assert(symbol >= g->min_nt);
	assert(symbol <= g->rule_max);

	size_t index = symbol - g->min_nt;

	assert(g->rules[index] != NULL);
	hgraph_destroy(g->rules[index]);

	g->rules[index] = NULL;

	if(symbol == g->rule_max) { // determine the current max symbol
		while(index >= 0 && g->rules[index] == NULL) {
			if(index == 0) { // cant reduce index anymore, no rule exists anymore
				g->rule_max = 0;
				return;
			}
			index--;
		}
		g->rule_max = index + g->min_nt;
	}
}

int slhr_grammar_rule_add(SLHRGrammar* g, uint64_t symbol, HGraph* graph) {
	assert(symbol >= g->min_nt); // so the start symbol is also excluded

	size_t index = symbol - g->min_nt;

	while(g->rules_cap <= index) {
		size_t cap_new = g->rules_cap < 8 ? 8 : (g->rules_cap + (g->rules_cap >> 1));
		HGraph** tmp = realloc(g->rules, cap_new * sizeof(HGraph*));
		if(!tmp)
			return -1;

		// Clear extra allocated memory
		memset(tmp + g->rules_cap, 0, (cap_new - g->rules_cap) * sizeof(HGraph*));

		g->rules_cap = cap_new;
		g->rules = tmp;
	}

	if(symbol > g->rule_max)
		g->rule_max = symbol;

	g->rules[index] = graph;
	return 0;
}

int slhr_grammar_rank_of_rule(const SLHRGrammar* g, uint64_t symbol) {
	if(symbol < g->min_nt)
    {
        return (int) g->rank_of_terminal[symbol]; // TODO: Fix different types for rank.
    }

	assert(symbol >= g->min_nt);
	symbol -= g->min_nt;

	assert(symbol < g->rules_cap && g->rules[symbol] != NULL);

	return g->rules[symbol]->rank;
}

size_t slhr_grammar_size_of_rule(const SLHRGrammar* g, uint64_t symbol) {
	if(symbol < g->min_nt)
		return 3;

	assert(symbol >= g->min_nt);
	symbol -= g->min_nt;

	assert(symbol < g->rules_cap && g->rules[symbol] != NULL);

	HGraph* rule = g->rules[symbol];

	size_t rule_len = hgraph_len(rule);
	size_t size = rule_len; // init the size with the number of edges because this equals the number of labels of all edges
	for(size_t i = 0; i < rule_len; i++) {
		HEdge* edge = hgraph_edge_get(rule, i);
		size += edge->rank; // only adding the rank because the labels are already counted
	}

	return size;
}

uint64_t slhr_grammar_unused_nt(const SLHRGrammar* g) {
	uint64_t i = 0;
	while(i < g->rules_cap && g->rules[i] != NULL)
		i++;
	return g->min_nt + i;
}
