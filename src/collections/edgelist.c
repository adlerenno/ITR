//
// Created by Enno Adler on 21.05.25.
//

#include <stddef.h>
#include <stdlib.h>
#include "edgelist.h"

EdgeList* edgelist_init()
{
    EdgeList* el = malloc(2* sizeof(size_t));
    if (!el)
        return NULL;
    el->len = 0;
    el->cap = 0;
    return el;
}

size_t edgelist_length(EdgeList* l) {
    return l->len;
}

HEdge* edgelist_get(EdgeList* l, size_t i) {
    return &(l->data[i]);
}

// low level list with capacity
int edgelist_append(EdgeList* l, HEdge* e) {
    size_t cap = l->cap;
    size_t len = l->len;

    if(cap == len) {
        cap = !cap ? 8 : (cap + (cap >> 1));
        HEdge* data = realloc(l->data, cap * sizeof(*data));
        if(!data)
            return -1;

        l->cap = cap;
        l->data = data;
    }

    l->data[l->len++] = *e;
    return 0;
}

void edgelist_destroy(EdgeList* l)
{
    free(l);
}
