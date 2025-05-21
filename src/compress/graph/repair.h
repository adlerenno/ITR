/**
 * @file repair.h
 * @author FR
 */

#ifndef REPAIR_H
#define REPAIR_H

#include <stdbool.h>
#include <hgraph.h>
#include <slhr_grammar.h>

SLHRGrammar* repair(HGraph* g, uint64_t nodes, uint64_t terminals, int max_rank, bool replace_monograms);

#endif
