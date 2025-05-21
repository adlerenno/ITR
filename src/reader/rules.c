/**
 * @file rules.c
 * @author FR
 */

#include "rules.h"

#include <stdlib.h>
#include <inttypes.h>
#include <reader.h>
#include <panic.h>
#include <edge.h>
#include <eliasfano.h>

RulesReader* rules_init(Reader* r) {
	size_t nbytes;
	uint64_t first_nt = reader_vbyte(r, &nbytes);
	FileOff off = nbytes;

	uint64_t rule_count = reader_vbyte(r, &nbytes);
	off += nbytes;

	uint64_t lentable = reader_vbyte(r, &nbytes);
	off += nbytes;

	FileOff offdata = off + lentable;

	Reader rt;
	reader_init(r, &rt, off);
	EliasFanoReader* table = eliasfano_init(&rt);
	if(!table)
		return NULL;

	RulesReader* rr = malloc(sizeof(*rr));
	if(!rr) {
		eliasfano_destroy(table);
		return NULL;
	}

	rr->r = *r;
	rr->first_nt = first_nt;
	rr->rule_count = rule_count;
	rr->table = table;
	rr->off_rules = 8 * offdata;

	return rr;
}

void rules_destroy(RulesReader* r) {
	eliasfano_destroy(r->table);
	free(r);
}

int rules_get(RulesReader* r, uint64_t nt, StEdge* e) {
	uint64_t i = nt - r->first_nt;
	if(i < 0 || i >= r->rule_count)
		panic("no rule found for non-terminal %" PRIu64, nt);

	FileOff bitoff = eliasfano_get(r->table, i);
	reader_bitpos(&r->r, r->off_rules + bitoff);

	int num_edges = reader_eliasdelta(&r->r);

	if(num_edges > MAX_RULE_SIZE) // panic because we only have memory for MAX_RULE_SIZE edges :(
		panic("rule with %d edges found but expected a maximum of %d", num_edges, MAX_RULE_SIZE);

	for(int j = 0; j < num_edges; j++)
		edge_read(&r->r, e + j);

	return num_edges;
}
