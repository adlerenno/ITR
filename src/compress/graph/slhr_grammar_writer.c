/**
 * @file slhr_grammar_writer.c
 * @author FR
 */

#include "slhr_grammar_writer.h"

#include <stdlib.h>
#include <assert.h>
#include <memdup.h>
#include <treeset.h>
#include <arith.h>
#include <bitarray.h>
#include <k2_writer.h>
#include <eliasfano_list.h>

static int cmp_hedge_cb(const void* v1, const void* v2) {
	const HEdge* e1 = *((HEdge**) v1);
	const HEdge* e2 = *((HEdge**) v2);

	return hedge_cmp(e1, e2);
}

typedef struct {
	size_t rank;
	size_t elements[0];
} IndexFunction;

#define index_function_sizeof(rank) (sizeof(IndexFunction) + (rank) * sizeof(size_t))

static int cmp_uint_cb(const void* v1, size_t l1, const void* v2, size_t l2) {
	assert(l1 == sizeof(uint64_t) && l2 == sizeof(uint64_t));

	const uint64_t* n1 = v1;
	const uint64_t* n2 = v2;
	return CMP(*n1, *n2);
}

static int cmp_index_function_cb(const void* v1, size_t l1, const void* v2, size_t l2) {
	const IndexFunction* i1 = v1;
	const IndexFunction* i2 = v2;

	assert(l1 == index_function_sizeof(i1->rank) && l2 == index_function_sizeof(i2->rank));

	size_t min_rank = MIN(i1->rank, i2->rank);
	for(size_t i = 0; i < min_rank; i++) {
		if(i1->elements[i] != i2->elements[i])
			return CMP(i1->elements[i], i2->elements[i]);
	}

	return CMP(i1->rank, i2->rank);
}

static IndexFunction* index_function(uint64_t* nodes, size_t rank) {
	// This function stores the sorted set of nodes in a treeset, because it turned
	// out to be much more efficient than an array sorted with qsort and searched
	// with binary search.

	Treeset* nodes_sorted = treeset_init(cmp_uint_cb);
	if(!nodes_sorted)
		return NULL;

	size_t i;
	for(i = 0; i < rank; i++) {
		if(treeset_add(nodes_sorted, &nodes[i], sizeof(*nodes)) < 0) {
			treeset_destroy(nodes_sorted);
			return NULL;
		}
	}

	IndexFunction* indxf = malloc(index_function_sizeof(rank));
	if(!indxf)
		goto err_0;

	indxf->rank = rank;
	for(i = 0; i < rank; i++) {
		ssize_t index = treeset_index_of(nodes_sorted, &nodes[i], sizeof(*nodes));
		assert(index >= 0);

		indxf->elements[i] = index;
	}

	treeset_destroy(nodes_sorted);
	return indxf;

err_0:
	treeset_destroy(nodes_sorted);
	return NULL;
}

typedef struct {
	K2Edge* data;
	size_t len;
	size_t cap;
} K2EdgeList;

static void k2_edgelist_init(K2EdgeList* l) {
	l->data = NULL;
	l->len = 0;
	l->cap = 0;
}

static void k2_edgelist_destroy(K2EdgeList* l) {
	if(l->data)
		free(l->data);
}

static int k2_edgelist_append(K2EdgeList* l, size_t x, size_t y) {
	size_t cap = l->cap;

	if(cap == l->len) {
		cap = !cap ? 16 : (cap + (cap >> 1)); // default cap is 16
		K2Edge* data = realloc(l->data, cap * sizeof(*data));
		if(!data)
			return -1;

		l->cap = cap;
		l->data = data;
	}

	l->data[l->len].xval = x;
	l->data[l->len].yval = y;
	l->len++;

	return 0;
}

// Create the data to serialize the start symbol
static int startsymbol_data(const HGraph* g, size_t node_count, K2EdgeList* p_edge_list, uint64_t** p_label_table, size_t** p_indxf_table, Treeset** p_ifs) {
	// Sort edges
	size_t edge_count = g->len;
	HEdge** edges = memdup(g->edges, edge_count * sizeof(HEdge*));
	if(!edges)
		return -1;

	qsort(edges, edge_count, sizeof(HEdge*), cmp_hedge_cb);

	// Initialize the set of index functions
	Treeset* ifs = treeset_init(cmp_index_function_cb);
	if(!ifs)
		goto err_0;

	// Because the index function of each edge is needed later, all index functions needs to be cached
	IndexFunction** edge_ifs = calloc(edge_count, sizeof(IndexFunction*)); // initialize each element with zero
	if(!edge_ifs)
		goto err_1;

	// Loop over all edges and determine its index function.
	// The index function will be added to `ifs` and `edge_ifs`.
	size_t i;
	for(i = 0; i < edge_count; i++) {
		HEdge* edge = edges[i];
		IndexFunction* indx = index_function(edge->nodes, edge->rank);
		if(!indx)
			goto err_2;

		if(treeset_add(ifs, indx, index_function_sizeof(indx->rank)) < 0)
			goto err_2;

		edge_ifs[i] = indx;
	}

	// Create the incidence matrix and the tables of the labels and the index functions of the edges

	K2EdgeList edge_list;
	k2_edgelist_init(&edge_list);

	uint64_t* label_table = malloc(edge_count * sizeof(*label_table));
	if(!label_table)
		goto err_2;

	size_t* indxf_table = malloc(edge_count * sizeof(*indxf_table));
	if(!label_table)
		goto err_3;

	for(i = 0; i < edge_count; i++) {
		HEdge* edge = edges[i];

		for(size_t j = 0; j < edge->rank; j++)
			if(k2_edgelist_append(&edge_list, i, edge->nodes[j]) < 0)
				goto err_4;

		label_table[i] = edge->label;
		indxf_table[i] = treeset_index_of(ifs, edge_ifs[i], index_function_sizeof(edge_ifs[i]->rank));
		assert(indxf_table[i] >= 0);
	}

	// From now on, only the following values are needed:
	// `matrix`, `label_table`, `indxf_table` and `ifs`.
	// So every other values can be freed

	free(edges);
	for(i = 0; i < edge_count; i++)
		free(edge_ifs[i]);
	free(edge_ifs);

	// Return the data via the parameters
	*p_edge_list = edge_list;
	*p_label_table = label_table;
	*p_indxf_table = indxf_table;
	*p_ifs = ifs;

	return 0;

err_4:
	free(indxf_table);
err_3:
	free(label_table);
	k2_edgelist_destroy(&edge_list);
err_2:
	if(edge_ifs) {
		for(i = 0; i < edge_count; i++) {
			if(edge_ifs[i])
				free(edge_ifs[i]);
			else
				break;
		}
		free(edge_ifs);
	}
err_1:
	treeset_destroy(ifs);
err_0:
	if(edges)
		free(edges);
	return -1;
}

static int edge_index_functions_write(size_t* ifs, size_t len, BitWriter* w) {
	size_t if_max = 0;

	size_t i;
	for(i = 0; i < len; i++)
		if(ifs[i] > if_max)
			if_max = ifs[i];

	int bits_needed = BITS_NEEDED(if_max);
	if(bitwriter_write_vbyte(w, bits_needed) < 0)
		return -1;

	for(i = 0; i < len; i++) {
		if(bitwriter_write_bits(w, ifs[i], bits_needed) < 0)
			return -1;
	}

	if(bitwriter_flush(w) < 0)
		return -1;

	return 0;
}

static inline int write_index_function(const IndexFunction* indx, BitWriter* w) {
	if(bitwriter_write_eliasdelta(w, indx->rank) < 0)
		return -1;

	for(size_t i = 0; i < indx->rank; i++) {
		if(bitwriter_write_eliasdelta(w, indx->elements[i]) < 0)
			return -1;
	}

	// no flush
	return 0;
}

static int index_functions_write(Treeset* ifs_set, BitWriter* w, const BitsequenceParams* p) {
	size_t ifs_len = treeset_size(ifs_set);

	int res = -1;

	BitWriter* ifs = malloc(ifs_len * sizeof(*ifs));
	if(!ifs)
		return res;

	TreemapIterator it;
	treeset_iter(ifs_set, &it);
	const IndexFunction* indx;

	size_t i;
	for(i = 0; (indx = treeset_iter_next(&it, NULL)) != NULL; i++) {
		bitwriter_init(&ifs[i], NULL);

		if(write_index_function(indx, &ifs[i]) < 0) {
			for(size_t j = 0; j <= i; j++) { // close all bitwriters
				bitwriter_close(&ifs[j]);
				goto exit_0;
			}
		}
	}

	uint64_t* offsets = malloc(ifs_len * sizeof(*offsets));
	if(!offsets)
		goto exit_1;

	offsets[0] = 0;
	for(i = 1; i < ifs_len; i++)
		offsets[i] = offsets[i - 1] + bitwriter_len(&ifs[i - 1]);

	BitWriter w0;
	bitwriter_init(&w0, NULL);

	if(eliasfano_write(offsets, ifs_len, &w0, p) < 0)
		goto exit_2;

	if(bitwriter_write_vbyte(w, bitwriter_bytelen(&w0)) < 0)
		goto exit_2;
	if(bitwriter_write_bitwriter(w, &w0) < 0)
		goto exit_2;
	for(i = 0; i < ifs_len; i++) {
		if(bitwriter_write_bitarray(w, &ifs[i].data) < 0) // not with `bitwriter_write_bitwriter` to prevent flushing
			goto exit_2;
	}
	if(bitwriter_flush(w) < 0)
		goto exit_2;

	res = 0;

exit_2:
	bitwriter_close(&w0);
	free(offsets);
exit_1:
	for(i = 0; i < ifs_len; i++) // close all bitwriters
		bitwriter_close(&ifs[i]);
exit_0:
	free(ifs);

	return res;
}

static int slhr_grammar_write_startsymbol(const HGraph* g, size_t node_count, BitWriter* w, const BitsequenceParams* p) {
	size_t edge_count = hgraph_len(g);

	K2EdgeList edges;
	uint64_t* label_table;
	size_t* indxf_table;
	Treeset* ifs;

	// Determine the data
	if(startsymbol_data(g, node_count, &edges, &label_table, &indxf_table, &ifs) < 0)
		return -1;

	int res = -1;

	// initialize the bitwriters to write some types to the memory
	BitWriter w0, w1, w2;
	bitwriter_init(&w0, NULL);
	bitwriter_init(&w1, NULL);
	bitwriter_init(&w2, NULL);

	// write matrix to memory
	if(k2_write(edge_count, node_count, edges.data, edges.len, &w0, p) < 0)
		goto exit;
	if(eliasfano_write(label_table, edge_count, &w1, p) < 0)
		goto exit;
	if(edge_index_functions_write(indxf_table, edge_count, &w2) < 0)
		goto exit;

	// write header of the start symbol
	if(bitwriter_write_vbyte(w, bitwriter_bytelen(&w0)) < 0) // byte length of the matrix
		goto exit;
	if(bitwriter_write_vbyte(w, bitwriter_bytelen(&w1)) < 0) // byte length of the labels
		goto exit;
	if(bitwriter_write_vbyte(w, bitwriter_bytelen(&w2)) < 0) // index function per edge
		goto exit;
	// length of all index functions not needed because it is the last segment

	if(bitwriter_write_bitwriter(w, &w0) < 0) // matrix
		goto exit;
	if(bitwriter_write_bitwriter(w, &w1) < 0) // labels
		goto exit;
	if(bitwriter_write_bitwriter(w, &w2) < 0) // index function per edge
		goto exit;
	if(index_functions_write(ifs, w, p) < 0)
		goto exit;
	if(bitwriter_flush(w) < 0)
		goto exit;

	res = 0; // success

exit:
	bitwriter_close(&w0);
	bitwriter_close(&w1);
	bitwriter_close(&w2);

	k2_edgelist_destroy(&edges);
	free(label_table);
	free(indxf_table);
	treeset_destroy(ifs);

	return res;
}

static int encode_rule(HGraph* g, BitWriter* w) {
	size_t len = hgraph_len(g);
	if(bitwriter_write_eliasdelta(w, len) < 0)
		return -1;

	for(size_t i = 0; i < len; i++) {
		HEdge* edge = hgraph_edge_get(g, i);

		if(bitwriter_write_eliasdelta(w, edge->label) < 0)
			return -1;
		if(bitwriter_write_eliasdelta(w, edge->rank) < 0)
			return -1;
		for(size_t j = 0; j < edge->rank; j++)
			if(bitwriter_write_eliasdelta(w, edge->nodes[j]) < 0)
				return -1;
	}

	return 0;
}

static int slhr_grammar_write_rules(SLHRGrammar* g, BitWriter* w, const BitsequenceParams* p) {
	size_t nt_count = g->rule_max == 0 ? 0 : (g->rule_max - g->min_nt + 1);

	BitWriter* rules_encoded = malloc(nt_count * sizeof(*rules_encoded));
	if(!rules_encoded)
		return -1;

	size_t i;
	for(i = 0; i < nt_count; i++)
		bitwriter_init(&rules_encoded[i], NULL);

	int res = -1;

	for(i = 0; i < nt_count; i++) {
		HGraph* rule = slhr_grammar_rule_get(g, g->min_nt + i);

		BitWriter* b = rules_encoded + i;
		bitwriter_init(b, NULL);
		if(encode_rule(rule, b) < 0)
			goto exit_0;
	}

	// create the offset table of the rules
	uint64_t* offsets = malloc(nt_count * sizeof(*offsets));
	if(!offsets)
		goto exit_0;

	if(nt_count > 0) {
		offsets[0] = 0;
		for(i = 1; i < nt_count; i++)
			offsets[i] = offsets[i - 1] + bitwriter_len(&rules_encoded[i - 1]);
	}

	BitWriter w0;
	bitwriter_init(&w0, NULL);

	if(eliasfano_write(offsets, nt_count, &w0, p) < 0)
		goto exit_1;

	uint64_t first_nt = nt_count > 0 ? g->min_nt : slhr_grammar_unused_nt(g);
	if(bitwriter_write_vbyte(w, first_nt) < 0) // first NT
		goto exit_1;
	if(bitwriter_write_vbyte(w, nt_count) < 0) // number of NTs (= number of rules)
		goto exit_1;
	if(bitwriter_write_vbyte(w, bitwriter_bytelen(&w0)) < 0)
		goto exit_1;
	if(bitwriter_write_bitwriter(w, &w0) < 0)
		goto exit_1;

	// write rules
	for(i = 0; i < nt_count; i++) {
		if(bitwriter_write_bitarray(w, &rules_encoded[i].data) < 0) // bitwriter_write_bitwriter because this function flushes
			goto exit_1;
	}

	if(bitwriter_flush(w) < 0)
		goto exit_1;

	res = 0;

exit_1:
	free(offsets);
	bitwriter_close(&w0);
exit_0:
	for(i = 0; i < nt_count; i++)
		bitwriter_close(&rules_encoded[i]);
	free(rules_encoded);

	return res;
}

static void get_terminals(BitArray* table, int nt_count, int terminals, int nt, BitArray* dst) {
	int table_width = terminals + nt_count;

	int i;
	for(i = 0; i < terminals; i++) {
		int index = nt * table_width + i;
		if(bitarray_get(table, index))
			bitarray_set(dst, i, true);
	}

	for(i = 0; i < nt_count; i++) {
		int index = nt * table_width + terminals + i;
		if(bitarray_get(table, index))
			get_terminals(table, nt_count, terminals, i, dst);
	}
}

static int slhr_grammar_write_nt_table(SLHRGrammar* g, size_t terminals, BitWriter* w, const BitsequenceParams* p) {
	size_t nt_count = g->rule_max == 0 ? 0 : (g->rule_max - g->min_nt + 1);
	size_t table_width = terminals + nt_count;

	// the table with size |N| x (|L| + |N|) with be represented as a bitarray
	BitArray table;
	if(bitarray_init(&table, nt_count * table_width) < 0)
		return -1;

	int res = -1;

	size_t i, j, k;
	uint64_t nt;

	for(i = 0; i < nt_count; i++) {
		HGraph* rule = slhr_grammar_rule_get(g, g->min_nt + i);

		for(j = 0; j < hgraph_len(rule); j++) {
			HEdge* e = hgraph_edge_get(rule, j);
			bitarray_set(&table, i * table_width + e->label, true);
		}
	}

	// creating the transitive closure of the table

	size_t pos_i_j, pos_i_k, pos_k_j;

	for(k = 0; k < nt_count; k++) {
		for(i = 0; i < nt_count; i++) {
			for(j = 0; j < table_width; j++) {
				pos_i_j = i * table_width + j;

				if(!bitarray_get(&table, pos_i_j)) {
					pos_i_k = i * table_width + terminals + k;
					pos_k_j = k * table_width + j;

					bool v = bitarray_get(&table, pos_i_k) && bitarray_get(&table, pos_k_j);
					bitarray_set(&table, pos_i_j, v);
				}
			}
		}
	}

	// Before, the table had a size of |N| x (|L| + |N|).
	// Because we created the transitive closure, the matrix only needs a size of |N| x |L|.

	K2EdgeList edges;
	k2_edgelist_init(&edges);

	for(i = 0; i < nt_count; i++) {
		for(j = 0; j < terminals; j++) {
			pos_i_j = i * table_width + j;

			if(bitarray_get(&table, pos_i_j)) {
				if(k2_edgelist_append(&edges, j, i) < 0) // j and i swapped
					goto exit_1;
			}
		}
	}

	// write the k2-encoded list of edges
	if(k2_write(terminals, nt_count, edges.data, edges.len, w, p) < 0)
		goto exit_1;

	res = 0;

exit_1:
	k2_edgelist_destroy(&edges);
exit_0:
	bitarray_destroy(&table);

	return res;
}

int slhr_grammar_write(SLHRGrammar* g, size_t node_count, size_t terminals, bool nt_table, BitWriter* w, const BitsequenceParams* params) {
	BitWriter w0;
	bitwriter_init(&w0, NULL);

	BitWriter w1;
	bitwriter_init(&w1, NULL);

	if(slhr_grammar_write_startsymbol(slhr_grammar_rule_get(g, START_SYMBOL), node_count, &w0, params) < 0)
		goto err_0;
	if(slhr_grammar_write_rules(g, &w1, params) < 0)
		goto err_0;

	if(bitwriter_write_vbyte(w, node_count) < 0)
		goto err_0;
	if(bitwriter_write_byte(w, nt_table ? 1 : 0) < 0)
		goto err_0;
	if(bitwriter_write_vbyte(w, bitwriter_bytelen(&w0)) < 0)
		goto err_0;
	if(nt_table && bitwriter_write_vbyte(w, bitwriter_bytelen(&w1)) < 0) // length of rules only needed if NT-table exists
		goto err_0;
	if(bitwriter_write_bitwriter(w, &w0) < 0)
		goto err_0;
	if(bitwriter_write_bitwriter(w, &w1) < 0)
		goto err_0;

	bitwriter_close(&w0);
	bitwriter_close(&w1);

	if(nt_table && slhr_grammar_write_nt_table(g, terminals, w, params) < 0)
		return -1;
	if(bitwriter_flush(w) < 0)
		return -1;

	return 0;

err_0:
	bitwriter_close(&w0);
	bitwriter_close(&w1);
	return -1;
}
