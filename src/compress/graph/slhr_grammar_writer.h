/**
 * @file slhr_grammar_writer.h
 * @author FR
 */

#ifndef SLHR_GRAMMAR_WRITER_H
#define SLHR_GRAMMAR_WRITER_H

#include <slhr_grammar.h>
#include <writer.h>

int slhr_grammar_write(SLHRGrammar* g, size_t node_count, size_t terminals, bool nt_table, BitWriter* w, const BitsequenceParams* params);

#endif
