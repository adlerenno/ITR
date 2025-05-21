/**
 * @file cgraph.c
 * @author FR
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cgraph.h>
#include <constants.h>
#include <reader.h>
#include <grammar.h>
#include <bitsequence_r.h>
#include "panic.h"

// Internal struct for the handler of libcgraph.
// This contains the file readers and the readers for the grammar and the dictionary.
typedef struct {
	FileReader* r;
	GrammarReader* gr;
} GraphReaderImpl;

CGraphR* cgraphr_init(const char* path) {
	// check if graph file is readable
	if(access(path, F_OK | R_OK) != 0) {
		perror(path);
		return NULL;
	}

	// open the bit reader for the graph file
	FileReader* fr = filereader_init(path);
	if(!fr)
		return NULL;

	// initialize the grammar reader with an subreader
	Reader r;
	reader_initf(fr, &r, 0);

	const uint8_t* magic = reader_read(&r, MAGIC_GRAPH_LEN);
	if(memcmp(magic, MAGIC_GRAPH, MAGIC_GRAPH_LEN) != 0)
		goto err0;

	size_t nbytes;
	FileOff lengrammar = reader_vbyte(&r, &nbytes);

	FileOff offgrammar = MAGIC_GRAPH_LEN + nbytes;

	reader_initf(fr, &r, offgrammar);
	GrammarReader* gr = grammar_init(&r);
	if(!gr)
		goto err0;

	// initialize the dict reader with an subreader

	GraphReaderImpl* g = malloc(sizeof(*g));
	if(!g)
		goto err1;

	g->r = fr;
	g->gr = gr;

	return (CGraphR*) g;

err1:
	grammar_destroy(gr);
err0:
	filereader_close(fr);
	return NULL;
}

void cgraphr_destroy(CGraphR* g) {
	GraphReaderImpl* gi = (GraphReaderImpl*) g;

	filereader_close(gi->r);
	grammar_destroy(gi->gr);
	free(gi);
}

size_t cgraphr_node_count(CGraphR* g) {
	return ((GraphReaderImpl*) g)->gr->node_count;
}

size_t cgraphr_edge_label_count(CGraphR* g) {
	return ((GraphReaderImpl*) g)->gr->rules->first_nt;
}


bool cgraphr_edges_next(CGraphEdgeIterator* it, CGraphEdge* e) {
	CGraphEdge t;
    CGraphNode nodes[LIMIT_MAX_RANK];
    t.nodes = nodes;
	switch(grammar_neighborhood_next((GrammarNeighborhood*) it, &t)) {
	case 1:
		if(e) {
			e->label = t.label;
            e->rank = t.rank;
            e->nodes = malloc(t.rank * sizeof (CGraphNode));
            if (!e->nodes)  //TODO: Introduce error case for this.
            {
                cgraphr_edges_finish(it);
                return false;
            }
            memcpy(e->nodes, t.nodes, t.rank * sizeof (CGraphNode));
		}

		return true;
	default:
		cgraphr_edges_finish(it);
		return false;
	}
}

void cgraphr_edges_finish(CGraphEdgeIterator* it) {
	grammar_neighborhood_finish((GrammarNeighborhood*) it);
	free(it);
}

bool cgraphr_edge_exists(CGraphR* g, CGraphRank rank, const CGraphNode* nodes, bool exact_query, bool no_node_order) {
	GraphReaderImpl* gi = (GraphReaderImpl*) g;

    for (int i = 0; i < rank; i++)
    {
        if (nodes[i] != CGRAPH_NODES_ALL && (nodes[i] < 0 || nodes[i] >= gi->gr->node_count))
            return false; // node[i] does not exists
    }
	//if(label < 0 || label >= gi->gr->rules->first_nt) // label does not exist
	//	return false;

	GrammarNeighborhood nb;
    if (exact_query)
    {
        grammar_neighborhood(gi->gr, CGRAPH_EXACT_QUERY, rank, nodes, &nb);
    }
    else
    {
        grammar_neighborhood(gi->gr, CGRAPH_CONTAINS_QUERY, rank, nodes, &nb);
    }

	if(grammar_neighborhood_next(&nb, NULL)) {
		grammar_neighborhood_finish(&nb);
		return true;
	}

	// not `grammar_neighborhood_finish` needed because the iterator is freed
	// because `grammar_neighborhood_finish` returned false.
	return false;
}

CGraphEdgeIterator* cgraphr_edges(CGraphR* g, CGraphRank rank, const CGraphNode* nodes, bool exact_query, bool no_node_order) {
    GraphReaderImpl* gi = (GraphReaderImpl*) g;

    for (int i=0; i < rank; i++)
    {
        if(nodes[i] != CGRAPH_NODES_ALL && (nodes[i] < 0 || nodes[i] >= gi->gr->node_count)) // node does not exist nor is wildcard.
            return NULL;
    }


    GrammarNeighborhood* nb = malloc(sizeof(*nb));
    if(!nb)
        return NULL;

    if (exact_query)
    {
        grammar_neighborhood(gi->gr, CGRAPH_EXACT_QUERY, rank, nodes, nb);
    }
    else
    {
        grammar_neighborhood(gi->gr, CGRAPH_CONTAINS_QUERY, rank, nodes, nb);
    }

    return (CGraphEdgeIterator*) nb;
}

CGraphEdgeIterator* cgraphr_edges_all(CGraphR* g) {
    GraphReaderImpl* gi = (GraphReaderImpl*) g;

    GrammarNeighborhood* nb = malloc(sizeof(*nb));
    if(!nb)
        return NULL;

    grammar_neighborhood(gi->gr, CGRAPH_DECOMPRESS_QUERY, CGRAPH_LABELS_ALL, NULL, nb);

    return (CGraphEdgeIterator*) nb;
}
