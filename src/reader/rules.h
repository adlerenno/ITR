/**
 * @file rules.h
 * @author FR
 */

#ifndef RULES_H
#define RULES_H

#include <reader.h>
#include <eliasfano.h>
#include <edge.h>

// We expect, that in large compressed graphs, we sometimes get inlined rules,
// so the number of edges in a rule exceeds 2.
// In own tests with LARGE graphs which took hours to compress, I got a few rules with 28 edges.
// But this value never exceeded.
// So the maximum number of edges in a rule is limited to (MAX_RANK / 2) = 64.
#define MAX_RULE_SIZE (RANK_MAX / 2)

typedef struct {
	Reader r;
	uint64_t first_nt;
	uint64_t rule_count;
	EliasFanoReader* table;
	FileOff off_rules;
} RulesReader;

RulesReader* rules_init(Reader* r);
void rules_destroy(RulesReader* r);

int rules_get(RulesReader* r, uint64_t nt, StEdge* e);

#endif
