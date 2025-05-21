/**
 * @file cgraphw.c
 * @author FR
 */

#include <cgraph.h>

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <hgraph.h>
#include <treemap.h>
#include <hashmap.h>
#include <hashset.h>
#include <bitarray.h>
#include <constants.h>
#include <slhr_grammar.h>
#include <repair.h>
#include <bitsequence.h>
#include <writer.h>
#include <slhr_grammar_writer.h>

typedef struct {
	bool compressed;
	CGraphCParams params;

	size_t nodes;
	size_t terminals;

	union {
		// Only needed before compression:
		struct {
			//Hashset* edges; // The edges are stored in a set because duplicate edges are not allowed
            HGraph* edges;
		};
		// Only needed after compression:
		struct {
			SLHRGrammar* grammar;
		};
	};
} GraphWriterImpl;

static int cmp_size_cb(const void* k1, size_t l1, const void* k2, size_t l2) {
	assert(l1 == sizeof(size_t) && l2 == sizeof(size_t));

	const size_t* s1 = k1;
	const size_t* s2 = k2;
	return CMP(*s1, *s2);
}

static Hash hash_size_cb(const void* k, size_t l) {
	assert(l == sizeof(size_t));

	const size_t* s = k;
	return HASH(*s);
}

static int cmp_edge_sort(const void* v1, const void* v2) {
	const HEdge** e1 = (const HEdge**) v1;
	const HEdge** e2 = (const HEdge**) v2;
	return hedge_cmp(*e1, *e2);
}

static int cmp_edge_cb(const void* k1, size_t l1, const void* k2, size_t l2) {
	const HEdge* e1 = k1;
	const HEdge* e2 = k2;

	assert(l1 == hedge_sizeof(e1->rank) && l2 == hedge_sizeof(e2->rank));

	return hedge_cmp(e1, e2);
}

static Hash hash_edge_cb(const void* k, size_t l) {
	const HEdge* e = k;

	assert(l == hedge_sizeof(e->rank));

	Hash h = HASH(e->label);
	for(size_t i = 0; i < e->rank; i++)
		HASH_COMBINE(h, HASH(e->nodes[i]));
	return h;
}

CGraphW* cgraphw_init() {
	Treemap* dict_ve = treemap_init(NULL); // using default comparator
	if(!dict_ve)
		return NULL;

	Hashmap* dict_rev = hashmap_init(cmp_size_cb, hash_size_cb);
	if(!dict_rev)
		goto err_0;

	HGraph * edges = hgraph_init(RANK_NONE);
	if(!edges)
		goto err_1;

	GraphWriterImpl* g = malloc(sizeof(*g));
	if(!g)
		goto err_2;

	// Only init attributes needed before compression
	g->compressed = false;

	g->params.max_rank = DEFAULT_MAX_RANK;
	g->params.monograms = DEFAULT_MONOGRAMS;
	g->params.factor = DEFAULT_FACTOR;
	g->params.nt_table = DEFAULT_NT_TABLE;
#ifdef RRR
	g->params.rrr = DEFAULT_RRR;
#endif
	g->nodes = 0;
	g->terminals = 0;

	g->edges = edges;

	return (CGraphW*) g;

err_2:
	hgraph_destroy(edges);
err_1:
	hashmap_destroy(dict_rev);
err_0:
	treemap_destroy(dict_ve);
	return NULL;
}

void cgraphw_destroy(CGraphW* g) {
	GraphWriterImpl* gi = (GraphWriterImpl*) g;

	if(!gi->compressed) {
		hgraph_destroy(gi->edges);
	}
	else {

		slhr_grammar_destroy(gi->grammar);
	}

	free(gi);
}

typedef enum {
	OCC_NODE,
	OCC_EDGE,
	OCC_BOTH
} ElementOccurence;

int cgraphw_add_edge(CGraphW* g, const CGraphRank rank, CGraphRank label, const CGraphNode* nodes) {
    GraphWriterImpl* gi = (GraphWriterImpl*) g;

    // cannot add new edges if the graph is compressed
    if(gi->compressed)
        return -1;

    // The memory of the edge is allocated via `alloca` because `hashmap` copies the data internally
    // and then manages the memory itself.
    // Furthermore, a short call to `alloca` is more efficient than `malloc` + `free`.
    HEdge* edge = malloc(hedge_sizeof(rank));
    if(!edge)
        return -1;
    edge->rank = rank;

    edge->label = (CGraphEdgeLabel) label;
    gi->terminals = gi->terminals > edge->label + 1 ? gi->terminals : edge->label + 1;

    CGraphNode max_node = gi->nodes; //Should be 1 higher than the highest node.
    for (size_t i = 0; i < rank; i++)
    {
        edge->nodes[i] = (CGraphNode) nodes[i];
        max_node = max_node > edge->nodes[i]+1 ? max_node : edge->nodes[i]+1;
    }
    gi->nodes = max_node;

    if(hgraph_add_edge(gi->edges, edge) < 0)
        return -1;

    return 0;
}

void cgraphw_set_params(CGraphW* g, const CGraphCParams* p) {
	if(!p)
		return;

	GraphWriterImpl* gi = (GraphWriterImpl*) g;

	if(p->max_rank > 0)
		gi->params.max_rank = p->max_rank;
	if(gi->params.max_rank > LIMIT_MAX_RANK)
		gi->params.max_rank = LIMIT_MAX_RANK;
	gi->params.monograms = p->monograms;
	if(p->factor > 0)
		gi->params.factor = p->factor;
	gi->params.nt_table = p->nt_table;
#ifdef RRR
	gi->params.rrr = p->rrr;
#endif
}

static HGraph* cgraphw_sort_edges(GraphWriterImpl* g) {
	HGraph* gr = hgraph_init(RANK_NONE);
	if(!gr)
		goto err_1;

//	HashsetIterator it;
//	hashset_iter(g->edges, &it);
	const HEdge* edge;
//	//while((edge = hashset_iter_next(&it, NULL)) != NULL) {
    for (size_t i = 0; i < hgraph_len(g->edges); i++) {
        edge = hgraph_edge_get(g->edges, i);
		HEdge* e = malloc(hedge_sizeof(edge->rank));
		if(!e)
			goto err_2;

        e->label = edge->label;

        // determine the value in the edge label dict
        e->rank = edge->rank;

        for (size_t j = 0; j < edge->rank; j++) {
            e->nodes[j] = edge->nodes[j]; // determine the value in the node label dict
        }

        if(hgraph_add_edge(gr, e) < 0) {
            free(e);
            goto err_2;
        }
    }

	// sorting the edges enhances the compression
	qsort(gr->edges, gr->len, sizeof(HEdge*), cmp_edge_sort);

	return gr;

err_2:
	hgraph_destroy(gr);
err_1:
	return NULL;
}

int cgraphw_compress(CGraphW* g) {
	GraphWriterImpl* gi = (GraphWriterImpl*) g;

	if(gi->compressed)
		return -1;
	if(hgraph_len(gi->edges) == 0) // empty graph is not supported
		return -1;

	HGraph* start_symbol = cgraphw_sort_edges(gi);
	if(!start_symbol)
		goto err_0;

	SLHRGrammar* gr = repair(start_symbol, gi->nodes, gi->terminals, gi->params.max_rank, gi->params.monograms);
	if(!gr)
		goto err_0;
	// destroying `start_symbol` not needed because the memory is managed by `repair` or the grammar `gr`

	// destroy the data if the repair-compression fully succeeded
	hgraph_destroy(gi->edges);

	gi->compressed = true;
	gi->grammar = gr;

	return 0;

err_0:
	return -1;
}

int cgraphw_write(CGraphW* g, const char* path, bool verbose) {
	GraphWriterImpl* gi = (GraphWriterImpl*) g;

	if(!gi->compressed)
		return -1;

	BitWriter w;
	if(bitwriter_init(&w, path) < 0)
		return -1;

	BitWriter w0;
	bitwriter_init(&w0, NULL);

	BitsequenceParams p;
	p.factor = gi->params.factor;
#ifdef RRR
	p.rrr = gi->params.rrr;
#endif

	if(slhr_grammar_write(gi->grammar, gi->nodes, gi->terminals, gi->params.nt_table, &w0, &p) < 0)
		goto err_0;
    if (verbose)
        printf("  Writing magic\n");
	if(bitwriter_write_bytes(&w, MAGIC_GRAPH, strlen(MAGIC_GRAPH) + 1) < 0)
		goto err_1;
    if (verbose)
        printf("  Writing meta\n");
	if(bitwriter_write_vbyte(&w, bitwriter_bytelen(&w0)) < 0)
		goto err_1;
    if (verbose)
        printf("  Writing grammar\n");
	if(bitwriter_write_bitwriter(&w, &w0) < 0)
		goto err_1;
	if(bitwriter_close(&w0) < 0)
		goto err_0;
    if (verbose) {
        printf("    Grammar Size is %llu byte\n", bitwriter_bytelen(&w0));
    }
	if(bitwriter_close(&w) < 0)
		return -1;
    if (verbose)
        printf("  Writing finished\n");

	return 0;

err_1:
	bitwriter_close(&w0);
err_0:
	bitwriter_close(&w);
	return -1;
}
