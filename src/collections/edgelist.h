//
// Created by Enno Adler on 21.05.25.
//

#include "hgraph.h"

#ifndef CGRAPH_EDGELIST_H
#define CGRAPH_EDGELIST_H

typedef struct {
    size_t len;
    size_t cap;
    HEdge* data;
} EdgeList;

EdgeList* edgelist_init();
size_t edgelist_length(EdgeList* l);
HEdge* edgelist_get(EdgeList* l, size_t i);
int edgelist_append(EdgeList* l, HEdge* e);
void edgelist_destroy(EdgeList* l);

#endif //CGRAPH_EDGELIST_H
