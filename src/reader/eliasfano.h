/**
 * @file eliasfano.h
 * @author FR
 */

#ifndef ELIASFANO_H
#define ELIASFANO_H

#include <bitsequence_r.h>
#include <cgraph.h>

typedef struct {
	Reader r;

	size_t n;
	int lowbits;
	FileOff off_lo;
	BitsequenceReader* hi;
} EliasFanoReader;

EliasFanoReader* eliasfano_init(Reader* r);
void eliasfano_destroy(EliasFanoReader* e);

uint64_t eliasfano_get(EliasFanoReader* e, uint64_t i);

typedef struct {
    EliasFanoReader * k;
    uint64_t edge_id;
    CGraphEdgeLabel label;
    CGraphEdgeLabel first_nt;
    bool has_next;
} EliasFanoIterator;

void eliasfano_iter(EliasFanoReader * k, CGraphEdgeLabel label, CGraphEdgeLabel first_nt, EliasFanoIterator* it);

// return value:
// 1: next element exists
// 0: no next element exists
// -1: error occured
int eliasfano_iter_next(EliasFanoIterator* it, uint64_t* v);
void eliasfano_iter_finish(EliasFanoIterator* it); // needed if the EliasFanoIterator was not iterated to the end

#endif
