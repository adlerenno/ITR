/**
 * @file eliasfano.c
 * @author FR
 */

#include "eliasfano.h"
#include <stdio.h>
#include <cgraph.h>
#include <reader.h>
#include <stdlib.h>
#include <inttypes.h>
#include <panic.h>
#include <bitarray.h>
#include <bitsequence_r.h>

EliasFanoReader* eliasfano_init(Reader* r) {
    size_t nbytes;
    size_t n = reader_vbyte(r, &nbytes);
    FileOff off = nbytes;

    int lowbits = (int) reader_vbyte(r, &nbytes); // int is more than enough
    off += nbytes;

    FileOff lenlowbits = reader_vbyte(r, &nbytes);
    off += nbytes;

    Reader rt;
    reader_init(r, &rt, off + lenlowbits);
    BitsequenceReader* b = bitsequence_reader_init(&rt);
    if(!b)
        return NULL;

    EliasFanoReader* e = malloc(sizeof(*e));
    if(!e) {
        bitsequence_reader_destroy(b);
        return NULL;
    }

    e->r = *r;
    e->n = n;
    e->lowbits = lowbits;
    e->off_lo = 8 * off;
    e->hi = b;

    return e;
}

void eliasfano_destroy(EliasFanoReader* e) {
    bitsequence_reader_destroy(e->hi);
    free(e);
}

uint64_t eliasfano_get(EliasFanoReader* e, uint64_t i) {
    if(i >= e->n)
        panic("index %" PRIu64 " exceeds the length %zu", i, e->n);

    uint64_t lval = 0;
    if(e->lowbits > 0) {
        FileOff off = e->off_lo + ((FileOff) i) * ((FileOff) e->lowbits); // casting to FileOff because of possible overflow
        reader_bitpos(&e->r, off);

        lval = reader_readint(&e->r, e->lowbits);
    }

    uint64_t hval = bitsequence_reader_select1(e->hi, i + 1) - i;

    return hval << e->lowbits | lval;
}

uint64_t eliasfano_binary_search_lowest(EliasFanoReader* k, CGraphEdgeLabel to_search, uint64_t left, uint64_t right)
{
    if (eliasfano_get(k, 0) > to_search)
    { // This case avoids that we reach the lower boarder without possibility of finding a result. Avoids a panic at eliasfano_get.
        return -1;
    }
    // BinarySearch on EliasFano to find the lowest occurrence of label.
    uint64_t mid;
    while (left <= right) {
        mid = left + (right - left) / 2;
        CGraphEdgeLabel l = eliasfano_get(k, mid);
        if (l == to_search) {
            if (mid == 0 || (eliasfano_get(k, mid - 1) < l)) {
                return mid;
            } else {
                right = mid - 1;
            }
        } else if (l > to_search) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    return -1;
}

uint64_t eliasfano_binary_search_lowest_or_next_label(EliasFanoReader* k, CGraphEdgeLabel to_search, uint64_t left, uint64_t right)
{
    if (eliasfano_get(k, 0) > to_search)
    { // This case avoids that we reach the lower boarder without possibility of finding a result. Avoids a panic at eliasfano_get.
        return -1;
    }
    // BinarySearch on EliasFano to find the lowest occurrence of label.
    uint64_t mid;
    while (left <= right) {
        mid = left + (right - left) / 2;
        CGraphEdgeLabel l = eliasfano_get(k, mid);
        if (l == to_search) {
            if (mid == 0 || (eliasfano_get(k, mid - 1) < l)) {
                return mid;
            } else {
                right = mid - 1;
            }
        } else if (l > to_search) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    if (left >= k->n)
    {
        return -1;
    } else {
        return left; // ID of the edge with the next higher label.
    }
}

void eliasfano_iter(EliasFanoReader* k, CGraphEdgeLabel label, CGraphEdgeLabel first_nt, EliasFanoIterator * it) {
    it->has_next = true;
    it->k = k;
    it->label = label;
    it->first_nt = first_nt;
    if (label == eliasfano_get(k, 0)) // Quick case for all graphs with only one label and searches for the first label.
    {
        it->edge_id = 0;
        return;
    }
    it->edge_id = eliasfano_binary_search_lowest(k, label, 0, k->n-1);
    if (it->edge_id == -1)
    {
        it->edge_id = eliasfano_binary_search_lowest_or_next_label(k, first_nt, 0, k->n-1);
        if (it->edge_id == -1)
        {
            it->has_next = false;
        }
    }
}

static int eliasfano_iter_next_element(EliasFanoIterator * it, uint64_t* v) {
    if (!it->has_next)
        return 0;
    if (it->edge_id >= it->k->n)
    {
        it->has_next = false;
        return 0;
    }
    CGraphEdgeLabel l = eliasfano_get(it->k, it->edge_id);
    if (l != it->label && l < it->first_nt)
    {
        it->edge_id = eliasfano_binary_search_lowest_or_next_label(it->k, it->first_nt, it->edge_id, it->k->n-1);
        if (it->edge_id == -1)
        {
            it->has_next = false;
            return 0;
        } else {
            *v = it->edge_id++;
            return 1;
        }
    }
    if ((l == it->label || l >= it->first_nt))
    {
        *v = it->edge_id++;
        return 1;
    }
    it->has_next = false;
    return 0;
}

int eliasfano_iter_next(EliasFanoIterator * it, uint64_t* v) {
    if(!it->has_next)
        return -1;

    uint64_t n;
    int res = eliasfano_iter_next_element(it, &n);
    if(res != 1)
        eliasfano_iter_finish(it);

    *v = n;
    return res;
}

void eliasfano_iter_finish(EliasFanoIterator * it) {
    if(it->has_next) {
        it->has_next = false;
    }
}

