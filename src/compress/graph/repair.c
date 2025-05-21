/**
 * @file repair.c
 * @author FR
 */

#include "repair.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <slhr_grammar.h>
#include <hgraph.h>
#include <hashmap.h>
#include <repair_types.h>
#include <rule_creator.h>
#include <arith.h>

// Comparator and hash functions

// not inline, because it is often used -- so let the compiler decide
// This function does not uses the CMP macro to compare values because by using the CMP macro,
// with `-O3`-optimization either 10 or 12 instructions are executed.
// In this implemention, either 9, 10 or 13 instructions are executed so the average is slightly lower without CMP.
static int cmp_adjacency_type(const AdjacencyType* a1, const AdjacencyType* a2) {
	if(a1->label != a2->label)
		return a1->label > a2->label ? 1 : -1;
	if(a1->conn_type != a2->conn_type)
		return a1->conn_type > a2->conn_type ? 1 : -1;
	return 0;
}

static inline Hash hash_adjacency_type(const AdjacencyType* a) {
	Hash h = 0;
	HASH_COMBINE(h, HASH(a->label));
	HASH_COMBINE(h, HASH(a->conn_type));
	return h;
}

static int cmp_adjacency_type_cb(const void* k1, size_t l1, const void* k2, size_t l2) {
	assert(l1 == sizeof(AdjacencyType) && l2 == sizeof(AdjacencyType));

	return cmp_adjacency_type(k1, k2);
}

static Hash hash_adjacency_type_cb(const void* k, size_t l) {
	assert(l == sizeof(AdjacencyType));

	return hash_adjacency_type(k);
}

static int cmp_digram_type_cb(const void* k1, size_t l1, const void* k2, size_t l2) {
	assert(l1 == sizeof(Digram) && l2 == sizeof(Digram));

	const Digram* d1 = k1;
	const Digram* d2 = k2;

	// compare first adjacancy type
	int cmp = cmp_adjacency_type(&d1->adj0, &d2->adj0);
	if(cmp != 0)
		return cmp;
	// compare second adjacency type
	return cmp_adjacency_type(&d1->adj1, &d2->adj1);
}

static Hash hash_digram_type_cb(const void* k, size_t l) {
	assert(l == sizeof(Digram));

	const Digram* d = k;

	Hash h = 0;
	HASH_COMBINE(h, hash_adjacency_type(&d->adj0));
	HASH_COMBINE(h, hash_adjacency_type(&d->adj1));
	return h;
}

static int cmp_monogram_type_cb(const void* k1, size_t l1, const void* k2, size_t l2) {
	assert(l1 == sizeof(Monogram) && l2 == sizeof(Monogram));

	const Monogram* m1 = k1;
	const Monogram* m2 = k2;

	if(m1->label != m2->label)
		return CMP(m1->label, m2->label);
	if(m1->conn0 != m2->conn0)
		return CMP(m1->conn0, m2->conn0);
	return CMP(m1->conn1, m2->conn1);
}

static Hash hash_monogram_type_cb(const void* k, size_t l) {
	assert(l == sizeof(Monogram));

	const Monogram* m = k;

	Hash h = HASH(m->label);
	HASH_COMBINE(h, HASH(m->conn0));
	HASH_COMBINE(h, HASH(m->conn1));
	return h;
}

static int cmp_uint_cb(const void* k1, size_t l1, const void* k2, size_t l2) {
	assert(l1 == sizeof(uint64_t) && l2 == sizeof(uint64_t));

	const uint64_t* n1 = k1;
	const uint64_t* n2 = k2;
	return CMP(*n1, *n2);
}

static Hash hash_uint_cb(const void* k, size_t l) {
	assert(l == sizeof(uint64_t));

	const uint64_t* v = k;
	return HASH(*v);
}

// Algorithms

typedef struct {
	size_t nodes;
	Hashmap** dict;
} NodeAdjacencyDict;

static int repair_create_node_adjacency_dict(HGraph* rule, size_t nodes, NodeAdjacencyDict* dict) {
	Hashmap** node_adjacency_dict = calloc(nodes, sizeof(Hashmap*)); // initialize with zero
	if(!node_adjacency_dict)
		return -1;

	HEdge* edge;
	AdjacencyType adj_type;
	uint64_t node;
	Hashmap* adjacency_frequency_dir;
	uint64_t count_init; // used to initialize a counter of a adjacency type

	size_t len = hgraph_len(rule);
	for(size_t i = 0; i < len; i++) {
		edge = hgraph_edge_get(rule, i);

		adj_type.label = edge->label;

		for(size_t connection_type = 0; connection_type < edge->rank; connection_type++) {
			node = edge->nodes[connection_type];

			adj_type.conn_type = connection_type;

			if(node_adjacency_dict[node] != NULL) {
				adjacency_frequency_dir = node_adjacency_dict[node];

				uint64_t* count = hashmap_get(adjacency_frequency_dir, &adj_type, sizeof(adj_type), NULL);
				if(count)
					// because a pointer to the value is returned,
					// the value at the given address can be increased.
					(*count)++;
				else {
					count_init = 1;
					hashmap_put(adjacency_frequency_dir, &adj_type, sizeof(adj_type), &count_init, sizeof(count_init));
				}
			}
			else {
				adjacency_frequency_dir = hashmap_init(cmp_adjacency_type_cb, hash_adjacency_type_cb);
				if(!adjacency_frequency_dir)
					goto err0;

				count_init = 1;
				hashmap_put(adjacency_frequency_dir, &adj_type, sizeof(adj_type), &count_init, sizeof(count_init));

				node_adjacency_dict[node] = adjacency_frequency_dir;
			}
		}
	}

	dict->nodes = nodes;
	dict->dict = node_adjacency_dict;
	return 0;

err0:
	for(size_t i = 0; i < nodes; i++)
		if(node_adjacency_dict[i])
			hashmap_destroy(node_adjacency_dict[i]);
	free(node_adjacency_dict);
	return -1;
}

// delta is signed to allow negative values
static int update_digram_count_delta(Hashmap* digram_count, Digram* digram, int64_t delta) {
	Digram inverted;

	int64_t* val; // signed value to allow negative values
	if((val = hashmap_get(digram_count, digram, sizeof(*digram), NULL))) {
		*val += delta;
		if(*val <= 0)
			hashmap_remove(digram_count, digram, sizeof(*digram));
	}
	else {
		inverted.adj0 = digram->adj1;
		inverted.adj1 = digram->adj0;

		if((val = hashmap_get(digram_count, &inverted, sizeof(inverted), NULL))) {
			*val += delta;
			if(*val <= 0)
				hashmap_remove(digram_count, &inverted, sizeof(inverted));
		}
		else if(delta > 0) {
			if(hashmap_put(digram_count, digram, sizeof(*digram), &delta, sizeof(delta)) < 0)
				return -1;
		}
	}

	return 0;
}

// In this function, we do not check for the rank of the digram so we count all available digrams,
// because this function is called before replacing the digrams.
// So all found digrams have a rank of 3.
static Hashmap* repair_count_digrams(NodeAdjacencyDict* dict) {
	Hashmap* digram_count = hashmap_init(cmp_digram_type_cb, hash_digram_type_cb);
	if(!digram_count)
		return NULL;

	// declaring all variables used in the loop
	Hashmap* adj_type_dict;
	size_t adjacency_type_count;

	AdjacencyType **tmp, **keys = NULL;
	size_t keys_cap = 0;

	HashmapIterator it;
	const AdjacencyType* adj_tmp;

	uint64_t *count_i, *count_j;
	Digram digram;
	size_t delta;

	for(size_t node = 0; node < dict->nodes; node++) {
		if((adj_type_dict = dict->dict[node]) == NULL)
			continue; // no entry for this node

		adjacency_type_count = hashmap_size(adj_type_dict);

		// create the list of keys
		if(adjacency_type_count > keys_cap) {
			// only realloc if the capacity does not match, and reuse the list of keys
			// this may lead to more memory usage but increases the performance
			// despite causing more cache misses

			tmp = realloc(keys, adjacency_type_count * sizeof(AdjacencyType*));
			if(!tmp)
				goto err;

			keys = tmp;
			keys_cap = adjacency_type_count;
		}

		hashmap_iter(adj_type_dict, &it);

		size_t i = 0;
		while((adj_tmp = hashmap_iter_next_key(&it, NULL)) != NULL)
			keys[i++] = (AdjacencyType*) adj_tmp;

		for(i = 0; i < adjacency_type_count; i++) {
			if(!(count_i = hashmap_get(adj_type_dict, keys[i], sizeof(AdjacencyType), NULL)))
				goto err;

			digram.adj0 = *keys[i];

			for(size_t j = i + 1; j < adjacency_type_count; j++) {
				if(!(count_j = hashmap_get(adj_type_dict, keys[j], sizeof(AdjacencyType), NULL)))
					goto err;

				digram.adj1 = *keys[j];

				delta = *count_i < *count_j ? *count_i : *count_j;

				if(update_digram_count_delta(digram_count, &digram, delta) < 0)
					goto err;
			}

			digram.adj1 = *keys[i];
			delta = *count_i / 2;

			if(update_digram_count_delta(digram_count, &digram, delta) < 0)
				goto err;
		}
	}

	free(keys);

	return digram_count;

err:
	if(keys)
		free(keys);
	hashmap_destroy(digram_count);
	return NULL;
}

static inline bool should_continue_replacing_digram(SLHRGrammar* grammar, const Digram* digram, uint64_t n) {
	int m = slhr_grammar_rank_of_rule(grammar, digram->adj0.label) + slhr_grammar_rank_of_rule(grammar, digram->adj1.label);
	int g = m + 2;

	return n * m + g < n * g;
}

static bool repair_digram_to_replace(SLHRGrammar* g, Hashmap* digram_count, Digram* digram) {
	HashmapIterator it;
	hashmap_iter(digram_count, &it);
	MapItem item;

	const Digram* res = NULL;
	uint64_t res_count = 0, item_count;

	while(hashmap_iter_next(&it, &item)) {
		item_count = *((uint64_t*) item.val);

		if(!res || item_count > res_count) {
			res = item.key;
			res_count = item_count;
		}
	}

	if(res == NULL || !should_continue_replacing_digram(g, res, res_count))
		return false;

	*digram = *res;
	return true;
}

typedef struct {
	bool is_map; // either a dict or just an edge
	union {
		Hashmap* map;
		size_t edge;
	};
} OccStateElement;

typedef struct {
	size_t start;
	Hashmap* map;
} OccState;

typedef struct {
	size_t len;
	size_t cap;
	size_t* data;
} OccStateList;

static int occ_state_init(OccState* s) {
	Hashmap* m = hashmap_init(cmp_uint_cb, hash_uint_cb);
	if(!m)
		return -1;

	s->start = 0;
	s->map = m;
	return 0;
}

static void occ_state_destroy_items(Hashmap* m) {
	HashmapIterator it;
	hashmap_iter(m, &it);
	MapItem item;

	while(hashmap_iter_next(&it, &item)) {
		OccStateList* l = item.val;
		if(l->data)
			free(l->data);
	}
}

static void occ_state_destroy(OccState* s) {
	HashmapIterator it;
	hashmap_iter(s->map, &it);
	MapItem item;

	while(hashmap_iter_next(&it, &item)) {
		OccStateElement* e = item.val;

		if(e->is_map) {
			Hashmap* m = e->map;
			occ_state_destroy_items(m);
			hashmap_destroy(m);
		}
	}

	hashmap_destroy(s->map);
}

static bool occ_state_contains_node(OccState* s, uint64_t node) {
	return hashmap_contains_key(s->map, &node, sizeof(node));
}

static int occ_state_list_init(OccStateList* l, size_t i) {
	size_t* data = malloc(sizeof(*data));
	if(!data)
		return -1;

	*data = i;

	l->len = 1;
	l->cap = 1;
	l->data = data;
	return 0;
}

static int occ_state_node_adj_init(OccState* s, uint64_t node, const AdjacencyType* adj, size_t i) {
	Hashmap* m = hashmap_init(cmp_adjacency_type_cb, hash_adjacency_type_cb);
	if(!m)
		return -1;

	OccStateList l;
	if(occ_state_list_init(&l, i) < 0)
		goto err0;

	if(hashmap_put(m, adj, sizeof(*adj), &l, sizeof(l)) < 0)
		goto err1;

	OccStateElement e;
	e.is_map = true;
	e.map = m;

	if(hashmap_put(s->map, &node, sizeof(node), &e, sizeof(e)) < 0)
		goto err1;

	return 0;

err1:
	free(l.data);
err0:
	hashmap_destroy(m);
	return -1;
}

// node must exist in state as a adj map!
static bool occ_state_node_contains_adj(OccState* s, uint64_t node, const AdjacencyType* adj) {
	OccStateElement* e = hashmap_get(s->map, &node, sizeof(node), NULL);
	assert(e != NULL);

	return hashmap_contains_key(e->map, adj, sizeof(*adj));
}

// node must exist in state as a adj map!
static size_t occ_state_node_len(OccState* s, uint64_t node) {
	OccStateElement* e = hashmap_get(s->map, &node, sizeof(node), NULL);
	assert(e != NULL);

	return hashmap_size(e->map);
}

// node, adj must exist in state as a adj map!
static inline OccStateList* _occ_state_node_list(OccState* s, uint64_t node, const AdjacencyType* adj) {
	OccStateElement* e = hashmap_get(s->map, &node, sizeof(node), NULL);
	assert(e != NULL);

	return hashmap_get(e->map, adj, sizeof(*adj), NULL);
}

static int occ_state_list_ensure_cap(OccStateList* l, size_t cap) {
	size_t new_cap = l->cap;
	if(cap <= new_cap)
		return 0;

	while(new_cap < cap)
		new_cap = new_cap < 8 ? 8 : (new_cap + (new_cap >> 1));

	size_t* tmp = realloc(l->data, new_cap * sizeof(*tmp));
	if(!tmp)
		return -1;

	l->cap = new_cap;
	l->data = tmp;

	return 0;
}

static int occ_state_list_append(OccStateList* l, size_t i) {
	if(occ_state_list_ensure_cap(l, l->len + 1) < 0)
		return -1;

	l->data[l->len++] = i;
	return 0;
}

// node, adj must exist in state as a adj map!
static int occ_state_node_append(OccState* s, uint64_t node, const AdjacencyType* adj, size_t i) {
	OccStateList* l = _occ_state_node_list(s, node, adj);
	return occ_state_list_append(l, i);
}

// node, adj must exist in state as a adj map!
static bool occ_state_node_adj_matches_i(OccState* s, uint64_t node, const AdjacencyType* adj, size_t i) {
	OccStateList* l = _occ_state_node_list(s, node, adj);
	return l->len == 1 && l->data[0] == i;
}

// node must exist in state but adj must not exist in map of the node!
static int occ_state_node_adj_set_i(OccState* s, uint64_t node, const AdjacencyType* adj, size_t i) {
	OccStateElement* e = hashmap_get(s->map, &node, sizeof(node), NULL);

	OccStateList l;
	if(occ_state_list_init(&l, i) < 0)
		return -1;

	if(hashmap_put(e->map, adj, sizeof(*adj), &l, sizeof(l)) < 0) {
		free(l.data);
		return -1;
	}

	return 0;
}

// node, adj must exist in state as a adj map!
static bool occ_state_node_adj_contains_i(OccState* s, uint64_t node, const AdjacencyType* adj, size_t i, size_t* index) {
	OccStateList* l = _occ_state_node_list(s, node, adj);

	for(size_t j = 0; j < l->len; j++)
		if(l->data[j] == i) {
			*index = j;
			return true;
		}
	return false;
}

// node, adj and index must exist in state as a adj map!
static void occ_state_node_adj_delete_index(OccState* s, uint64_t node, const AdjacencyType* adj, size_t index) {
	OccStateList* l = _occ_state_node_list(s, node, adj);
	assert(index < l->len);

	memmove(l->data + index, l->data + (index + 1), (l->len - index - 1) * sizeof(*l->data));
	l->len--;
}

// node, adj must exist in state as a adj map!
static size_t occ_state_node_adj_pop0(OccState* s, uint64_t node, const AdjacencyType* adj) {
	OccStateList* l = _occ_state_node_list(s, node, adj);
	assert(l->len > 0);

	size_t first = l->data[0];
	memmove(l->data, l->data + 1, (l->len - 1) * sizeof(*l->data));
	l->len--;
	return first;
}

// node, adj must exist in state as a adj map!
static size_t occ_state_node_adj_len(OccState* s, uint64_t node, const AdjacencyType* adj) {
	OccStateList* l = _occ_state_node_list(s, node, adj);
	return l->len;
}

// node, adj must exist in state as a adj map!
static void occ_state_node_adj_del(OccState* s, uint64_t node, const AdjacencyType* adj) {
	OccStateElement* e = hashmap_get(s->map, &node, sizeof(node), NULL);
	Hashmap* m = e->map;
	OccStateList* l = hashmap_get(m, adj, sizeof(*adj), NULL);

	if(l->data)
		free(l->data);

	hashmap_remove(m, adj, sizeof(*adj));
}

// node must exist in state as a adj map!
static void occ_state_node_del(OccState* s, uint64_t node) {
	OccStateElement* e = hashmap_get(s->map, &node, sizeof(node), NULL);
	if(e->is_map) {
		occ_state_destroy_items(e->map);
		hashmap_destroy(e->map);
	}
	hashmap_remove(s->map, &node, sizeof(node));
}

// node must exist in state!
static int occ_state_node_init_edge(OccState* s, uint64_t node, size_t edge) {
	OccStateElement e;
	e.is_map = false;
	e.edge = edge;

	if(hashmap_put(s->map, &node, sizeof(node), &e, sizeof(e)) != 0) // element must not exist
		return -1;

	return 0;
}

// node must exist in state as an edge!
static size_t occ_state_node_edge_get(OccState* s, uint64_t node) {
	OccStateElement* e = hashmap_get(s->map, &node, sizeof(node), NULL);
	return e->edge;
}

static int find_occurrence_of_digram(const Digram* digram_to_replace, HGraph* start_rule, OccState* state, size_t res[2]) {
	size_t len = hgraph_len(start_rule);
	for(size_t i = state->start; i < len; i++) {
		HEdge* edge = hgraph_edge_get(start_rule, i);

		// Because at the replacing of occurences of digramsthe, the removal of edges in the start rule
		// creates holes with NULL values, the edge has to be checked for NULL.
		if(!edge)
			continue;

		const AdjacencyType* adjacency_type;
		size_t connection_type;
		uint64_t node;

		size_t edge_1;

		// Check, which type of digram is searched.
		if(cmp_adjacency_type(&digram_to_replace->adj0, &digram_to_replace->adj1) != 0) {
			for(int j = 0; j <= 1; j++) { // repeat 2 times
				adjacency_type = j == 0 ? &digram_to_replace->adj0 : &digram_to_replace->adj1;

				if(edge->label == adjacency_type->label) {
					connection_type = adjacency_type->conn_type;
					node = edge->nodes[connection_type];

					const AdjacencyType* adjacency_type_2 = j != 0 ? &digram_to_replace->adj0 : &digram_to_replace->adj1;

					if(!occ_state_contains_node(state, node)) {
						if(occ_state_node_adj_init(state, node, adjacency_type, i) < 0)
							return -1;
					}
					else if(occ_state_node_contains_adj(state, node, adjacency_type) && occ_state_node_len(state, node) == 1) {
						if(occ_state_node_append(state, node, adjacency_type, i) < 0)
							return -1;
					}
					else if(occ_state_node_contains_adj(state, node, adjacency_type_2) && occ_state_node_len(state, node) == 1 && occ_state_node_adj_matches_i(state, node, adjacency_type_2, i)) {
						if(occ_state_node_adj_set_i(state, node, adjacency_type, i) < 0)
							return -1;
					}
					else {
						size_t index;
						if(occ_state_node_contains_adj(state, node, adjacency_type) && occ_state_node_adj_contains_i(state, node, adjacency_type, i, &index))
							occ_state_node_adj_delete_index(state, node, adjacency_type, index);
						if(occ_state_node_adj_contains_i(state, node, adjacency_type_2, i, &index))
							occ_state_node_adj_delete_index(state, node, adjacency_type_2, index);

						edge_1 = occ_state_node_adj_pop0(state, node, adjacency_type_2);
						uint64_t other_node;

						if(edge->label == adjacency_type_2->label) {
							size_t connection_type_2 = adjacency_type_2->conn_type;
							other_node = edge->nodes[connection_type_2];

							if(occ_state_contains_node(state, other_node) && occ_state_node_contains_adj(state, other_node, adjacency_type_2) && occ_state_node_adj_contains_i(state, other_node, adjacency_type_2, i, &index)) {
								occ_state_node_adj_delete_index(state, other_node, adjacency_type_2, index);

								if(occ_state_node_adj_len(state, other_node, adjacency_type_2) == 0) {
									occ_state_node_adj_del(state, other_node, adjacency_type_2);

									if(occ_state_node_len(state, other_node) == 0)
										occ_state_node_del(state, other_node);
								}
							}
						}

						HEdge* edge_1_val = hgraph_edge_get(start_rule, edge_1);
						if(edge_1_val->label == adjacency_type->label) {
							other_node = edge_1_val->nodes[connection_type];

							if(occ_state_contains_node(state, other_node) && occ_state_node_adj_contains_i(state, other_node, adjacency_type, edge_1, &index)) {
								occ_state_node_adj_delete_index(state, other_node, adjacency_type, index);

								if(occ_state_node_adj_len(state, other_node, adjacency_type) == 0) {
									occ_state_node_adj_del(state, other_node, adjacency_type);

									if(occ_state_node_len(state, other_node) == 0)
										occ_state_node_del(state, other_node);
								}
							}
						}

						if(occ_state_node_contains_adj(state, node, adjacency_type) && occ_state_node_adj_len(state, node, adjacency_type) == 0)
							occ_state_node_adj_del(state, node, adjacency_type);
						if(occ_state_node_adj_len(state, node, adjacency_type_2) == 0)
							occ_state_node_adj_del(state, node, adjacency_type_2);
						if(occ_state_node_len(state, node) == 0)
							occ_state_node_del(state, node);

						state->start = i;
						// if j == 0 (i, edge_1) is returned;
						// else (edge_1, i) is returned
						res[j] = i;
						res[1 - j] = edge_1;
						return 1;
					}
				}
			}
		}
		else {
			if(edge->label == digram_to_replace->adj0.label) {
				adjacency_type = &digram_to_replace->adj0;
				connection_type = adjacency_type->conn_type;
				node = edge->nodes[connection_type];

				if(!occ_state_contains_node(state, node)) {
					if(occ_state_node_init_edge(state, node, i) < 0)
						return -1;
				}
				else {
					edge_1 = occ_state_node_edge_get(state, node);
					occ_state_node_del(state, node);

					state->start = i;
					res[0] = edge_1;
					res[1] = i;
					return 1;
				}
			}
		}
	}

	return 0;
}

static inline bool digram_over_max_rank(const SLHRGrammar* g, int max_rank, const Digram* d) {
	return slhr_grammar_rank_of_rule(g, d->adj0.label) + slhr_grammar_rank_of_rule(g, d->adj1.label) - 1 > max_rank;	
}

// Because we only update the digram count for digrams, whose rank is less equal then the max rank,
// the grammar and the max rank are also needed for this function.
static int update_digram_count(const SLHRGrammar* g, int max_rank, HEdge* old_edges[2], HEdge* new_edge, NodeAdjacencyDict* node_adjacency_dict, Hashmap* digram_count) {
	// declaration of variables:
	Digram digram;

	uint64_t node;
	Hashmap* adjacency_frequency_dir;

	HashmapIterator it;
	MapItem item;
	const AdjacencyType* adjacency_type_2;
	uint64_t count_2;

	for(int i = 0; i < 2; i++) {
		HEdge* edge = old_edges[i];

		// current adjacency type: digram.adj0
		// fore slightly better performance, digram.adj0 is used as the adjacency type
		digram.adj0.label = edge->label;

		for(size_t connection_type = 0; connection_type < edge->rank; connection_type++) {
			digram.adj0.conn_type = connection_type;

			node = edge->nodes[connection_type];
			adjacency_frequency_dir = node_adjacency_dict->dict[node];

			uint64_t* count = hashmap_get(adjacency_frequency_dir, &digram.adj0, sizeof(digram.adj0), NULL);
			assert(count != NULL);

			hashmap_iter(adjacency_frequency_dir, &it);

			while(hashmap_iter_next(&it, &item)) {
				adjacency_type_2 = item.key;

				if(cmp_adjacency_type(&digram.adj0, adjacency_type_2) != 0) {
					count_2 = *((uint64_t*) item.val);

					if(*count <= count_2) {
						digram.adj1 = *adjacency_type_2;

						// Do not check for the rank of the digram because this digram was replaced, so
						// its rank does not exceeds the maximum rank.
						update_digram_count_delta(digram_count, &digram, -1);
					}
				}
			}

			if(*count % 2 == 0) {
				digram.adj1 = digram.adj0;

				// Do not check for the rank of the digram because this digram was replaced, so
				// its rank does not exceeds the maximum rank.
				update_digram_count_delta(digram_count, &digram, -1);
			}

			// Reduce the adjacency_type in the dict by 1.
			(*count)--;
			if(*count == 0)
				hashmap_remove(adjacency_frequency_dir, &digram.adj0, sizeof(digram.adj0));
		}
	}

	// current adjacency type: digram.adj0
	// fore slightly better performance, digram.adj0 is used as the adjacency type
	digram.adj0.label = new_edge->label;

	// Do it for the new edge as well.
	for(size_t connection_type = 0; connection_type < new_edge->rank; connection_type++) {
		digram.adj0.conn_type = connection_type;

		node = new_edge->nodes[connection_type];
		adjacency_frequency_dir = node_adjacency_dict->dict[node];

		uint64_t count;
		uint64_t* count_ptr = hashmap_get(adjacency_frequency_dir, &digram.adj0, sizeof(digram.adj0), NULL);
		if(count_ptr == NULL) {
			count = 1;
			hashmap_put(adjacency_frequency_dir, &digram.adj0, sizeof(digram.adj0), &count, sizeof(count));
		}
		else
			count = ++(*count_ptr); // preincrement

		hashmap_iter(adjacency_frequency_dir, &it);

		while(hashmap_iter_next(&it, &item)) {
			adjacency_type_2 = item.key;

			if(cmp_adjacency_type(&digram.adj0, adjacency_type_2) != 0) {
				count_2 = *((uint64_t*) item.val);

				if(count <= count_2) {
					digram.adj1 = *adjacency_type_2;

					// update the digram count only, if the rank of the digram does not exceeds the max rank.
					if(!digram_over_max_rank(g, max_rank, &digram))
						update_digram_count_delta(digram_count, &digram, +1);
				}
			}
		}

		if(count % 2 == 0) {
			digram.adj1 = digram.adj0;

			// update the digram count only, if the rank of the digram does not exceeds the max rank.
			if(!digram_over_max_rank(g, max_rank, &digram))
				update_digram_count_delta(digram_count, &digram, +1);
		}
	}

	return 0;
}

static int repair_replace_digrams(SLHRGrammar* g, size_t nodes, int max_rank) {
	int result = -1;

	HGraph* start_rule = slhr_grammar_rule_get(g, START_SYMBOL);

	NodeAdjacencyDict adj_dict;
	if(repair_create_node_adjacency_dict(start_rule, nodes, &adj_dict) < 0)
		return -1;

	Hashmap* digram_count = repair_count_digrams(&adj_dict);
	if(!digram_count)
		goto free_adj_dict;

	RuleCreator new_rule;
	new_rule.rule = NULL;

	Digram digram_to_replace;
	while(repair_digram_to_replace(g, digram_count, &digram_to_replace)) {
		if(rule_creator_digram_init(&new_rule, g, &digram_to_replace) < 0)
			goto free_rule_creator;

		bool rule_created = false;

		OccState current_state_object;
		if(occ_state_init(&current_state_object) < 0)
			goto free_rule_creator;

		size_t occurrence_of_digram[2];
		int occ_res;
		while((occ_res = find_occurrence_of_digram(&digram_to_replace, start_rule, &current_state_object, occurrence_of_digram)) == 1) {
			HEdge* old_edges[2];
			old_edges[0] = hgraph_edge_get(start_rule, occurrence_of_digram[0]);
			old_edges[1] = hgraph_edge_get(start_rule, occurrence_of_digram[1]);

			HEdge* new_edge = rule_creator_digram_new_edge(&new_rule, old_edges[0], old_edges[1]);
			if(!new_edge)
				goto free_rule_creator;

			if(!rule_created) {
				// The rule is be created before updating the digram count because to determine the rank of the digram,
				// the rule needs to occur in the grammar.
				if(slhr_grammar_rule_add(g, new_rule.rule_name, new_rule.rule) < 0)
					goto free_rule_creator;

				rule_creator_no_free(&new_rule);
				rule_created = true;
			}

			// The digram count will be updated before the old edges are replaced
			update_digram_count(g, max_rank, old_edges, new_edge, &adj_dict, digram_count);

			// It does not really matter with edge will be replaced and deleted,
			// because the deleted edges will be filled after the loop.
			hgraph_edge_replace(start_rule, occurrence_of_digram[0], new_edge);
			hgraph_edge_free(start_rule, occurrence_of_digram[1]);
		}
		if(occ_res < 0) // error happened
			goto free_rule_creator;

		occ_state_destroy(&current_state_object);

		hashmap_remove(digram_count, &digram_to_replace, sizeof(digram_to_replace));
		rule_creator_destroy(&new_rule);
	}

	// removing holes with NULL edges in the grammar
	hgraph_fill_holes(start_rule);

	result = 0;

free_rule_creator:
	// destroy the rule because this is needed if the loop was terminated because of an error.
	rule_creator_destroy(&new_rule);

free_digram_count:
	hashmap_destroy(digram_count);

free_adj_dict:
	for(size_t i = 0; i < adj_dict.nodes; i++)
		if(adj_dict.dict[i])
			hashmap_destroy(adj_dict.dict[i]);
	free(adj_dict.dict);

	return result;
}

static Hashmap* repair_count_monograms(HGraph* start_rule) {
	Hashmap* monogram_dict = hashmap_init(cmp_monogram_type_cb, hash_monogram_type_cb);
	if(!monogram_dict)
		return NULL;

	// Init at first and clear after any loop
	Hashmap* nodes_connection_type_dict = hashmap_init(cmp_uint_cb, hash_uint_cb);
	if(!nodes_connection_type_dict)
		goto err_0;

	size_t len = hgraph_len(start_rule);
	for(size_t edge_id = 0; edge_id < len; edge_id++) {
		HEdge* e = hgraph_edge_get(start_rule, edge_id);

		for(size_t connection_type = 0; connection_type < e->rank; connection_type++) {
			uint64_t node = e->nodes[connection_type];

			OccStateList* l = hashmap_get(nodes_connection_type_dict, &node, sizeof(node), NULL);
			if(l) {
				// add connection type to node
				if(occ_state_list_append(l, connection_type) < 0)
					goto err_1;
			}
			else {
				// init list of connection types of current node
				OccStateList l_tmp;
				if(occ_state_list_init(&l_tmp, connection_type) < 0)
					goto err_1;

				if(hashmap_put(nodes_connection_type_dict, &node, sizeof(node), &l_tmp, sizeof(l_tmp)) < 0) {
					free(l_tmp.data);
					goto err_1;
				}
			}
		}

		HashmapIterator it;
		hashmap_iter(nodes_connection_type_dict, &it);
		MapItem item;

		Monogram monogram;
		monogram.label = e->label;

		while(hashmap_iter_next(&it, &item)) {
			OccStateList* connection_type_list = item.val;

			if(connection_type_list->len > 1) {
				for(size_t i = 0; i < connection_type_list->len; i++) {
					monogram.conn0 = connection_type_list->data[i];

					for(size_t j = i + 1; j < connection_type_list->len; j++) {
						monogram.conn1 = connection_type_list->data[j];

						uint64_t* count = hashmap_get(monogram_dict, &monogram, sizeof(monogram), NULL);
						if(!count) {
							uint64_t count_tmp = 1;
							hashmap_put(monogram_dict, &monogram, sizeof(monogram), &count_tmp, sizeof(count_tmp));
						}
						else
							(*count)++;
					}
				}
			}
		}

		occ_state_destroy_items(nodes_connection_type_dict);
		hashmap_clear(nodes_connection_type_dict);
	}

	hashmap_destroy(nodes_connection_type_dict);

	return monogram_dict;

err_1:
	occ_state_destroy_items(nodes_connection_type_dict);
	hashmap_destroy(nodes_connection_type_dict);
err_0:
	hashmap_destroy(monogram_dict);
	return NULL;
}

static inline bool should_continue_replacing_monogram(SLHRGrammar* grammar, const Monogram* monogram, uint64_t n) {
	int m = slhr_grammar_rank_of_rule(grammar, monogram->label);
	int g = m + 1;

	return n * m + g < n * g;
}

static bool repair_monogram_to_replace(SLHRGrammar* g, Hashmap* monogram_count, Monogram* monogram) {
	HashmapIterator it;
	hashmap_iter(monogram_count, &it);
	MapItem item;

	const Monogram* res = NULL;
	uint64_t res_count = 0, item_count;

	while(hashmap_iter_next(&it, &item)) {
		item_count = *((uint64_t*) item.val);

		if(!res || item_count > res_count) {
			res = item.key;
			res_count = item_count;
		}
	}

	if(res == NULL || !should_continue_replacing_monogram(g, res, res_count))
		return false;

	*monogram = *res;
	return true;
}

// state: starting position when read, last occurence when written
static inline bool find_occurrence_of_monogram(const Monogram* monogram_to_replace, HGraph* start_rule, size_t* state) {
	size_t len = hgraph_len(start_rule);
	for(size_t i = *state; i < len; i++) {
		HEdge* edge = hgraph_edge_get(start_rule, i);

		if(edge->label == monogram_to_replace->label && edge->nodes[monogram_to_replace->conn0] == edge->nodes[monogram_to_replace->conn1]) {
			*state = i;
			return true;
		}
	}

	return false;
}

static void update_monogram_dict(HGraph* start_rule, size_t index, HEdge* old_edge, HEdge* new_edge, Hashmap* monogram_dict, const Monogram* replaced_monogram) {
	// In Enno Adler's implementation, a new list is created and is iterated over for the monograms,
	// because `monogram_dict` will be modified in the loop.
	// Since it is assumed that `old_edge->label != new_edge->label`, the changed / new monograms in
	// `monogram_dict` are ignored in this loop and it is sufficient to iterate over the entire map.

	assert(old_edge->label != new_edge->label); // needs to be true to choose this efficient implementation of `update_monogram_dict`

	HashmapIterator it;
	hashmap_iter(monogram_dict, &it);
	MapItem item;

	size_t i = 0;
	while(hashmap_iter_next(&it, &item)) {
		const Monogram* monogram = item.key;

		if(monogram->label == old_edge->label && old_edge->nodes[monogram->conn0] == old_edge->nodes[monogram->conn1]) {
			uint64_t* count = item.val;

			(*count)--;

			// Could be possible that monogram gets deleted so connection types will be saved for later
			size_t conn0 = monogram->conn0;
			size_t conn1 = monogram->conn1;

			// because `monogram` and `count` are not needed anymore, the element can be removed without problems
			if(*count == 0)
				hashmap_iter_remove(&it);

			if(conn0 != replaced_monogram->conn1 && conn1 != replaced_monogram->conn1) {
				Monogram new_mono;
				new_mono.conn0 = conn0 - (conn0 <= replaced_monogram->conn1 ? 0 : 1);
				new_mono.conn1 = conn1 - (conn1 <= replaced_monogram->conn1 ? 0 : 1);

				if(new_mono.conn0 < new_mono.conn1) {
					new_mono.label = new_edge->label;

					count = hashmap_get(monogram_dict, &new_mono, sizeof(new_mono), NULL);
					if(count)
						(*count)++;
					else {
						uint64_t count_tmp = 1;
						hashmap_put(monogram_dict, &new_mono, sizeof(new_mono), &count_tmp, sizeof(count_tmp));
					}
				}
			}
		}
	}
}

static int repair_replace_monograms(SLHRGrammar* g) {
	int result = -1; // return result

	HGraph* start_rule = slhr_grammar_rule_get(g, START_SYMBOL);

	Hashmap* monogram_count = repair_count_monograms(start_rule);
	if(!monogram_count)
		return -1;

	RuleCreator new_rule;
	new_rule.rule = NULL;

	Monogram monogram_to_replace;
	while(repair_monogram_to_replace(g, monogram_count, &monogram_to_replace)) {
		if(rule_creator_monogram_init(&new_rule, g, &monogram_to_replace) < 0)
			goto free_digram_count;

		bool add_rule = false;

		size_t state = 0;
		while(find_occurrence_of_monogram(&monogram_to_replace, start_rule, &state)) {
			size_t index = state++; // getting occurence and increase the starting position

			HEdge* old_edge = hgraph_edge_get(start_rule, index);
			HEdge* new_edge = rule_creator_monogram_new_edge(&new_rule, old_edge);

			hgraph_edge_set(start_rule, index, new_edge); // set and not replace because the old edge is already needed

			update_monogram_dict(start_rule, index, old_edge, new_edge, monogram_count, &monogram_to_replace);
			free(old_edge);

			add_rule = true;
		}

		if(add_rule) {
			if(slhr_grammar_rule_add(g, new_rule.rule_name, new_rule.rule) < 0)
				goto free_rule_creator;

			rule_creator_no_free(&new_rule);
		}

		hashmap_remove(monogram_count, &monogram_to_replace, sizeof(monogram_to_replace));
		rule_creator_destroy(&new_rule);
	}

	result = 0;

free_rule_creator:
	rule_creator_destroy(&new_rule);

free_digram_count:
	hashmap_destroy(monogram_count);

	return result;
}

static Hashmap* repair_count_rules(SLHRGrammar* grammar) {
	Hashmap* rule_dict = hashmap_init(cmp_uint_cb, hash_uint_cb);
	if(!rule_dict)
		return NULL;

	uint64_t next = START_SYMBOL;
	uint64_t i;
	while(slhr_grammar_next_rule(grammar, &next, &i)) {
		HGraph* rule = slhr_grammar_rule_get(grammar, i);

		size_t len = hgraph_len(rule);
		for(size_t j = 0; j < len; j++) {
			HEdge* edge = hgraph_edge_get(rule, j);

			uint64_t* count = hashmap_get(rule_dict, &edge->label, sizeof(edge->label), NULL);
			if(count)
				(*count)++;
			else {
				uint64_t count_tmp = 1;
				hashmap_put(rule_dict, &edge->label, sizeof(edge->label), &count_tmp, sizeof(count_tmp));
			}
		}
	}

	HashmapIterator it;
	hashmap_iter(rule_dict, &it);
	const uint64_t* rule_name;

	while((rule_name = hashmap_iter_next_key(&it, NULL)) != NULL) {
		if(slhr_grammar_is_terminal(grammar, *rule_name))
			hashmap_iter_remove(&it);
	}

	return rule_dict;
}

static inline bool should_continue_inserting_rules(SLHRGrammar* grammar, uint64_t rule_name_to_insert, uint64_t count) {
	int used_size = slhr_grammar_rank_of_rule(grammar, rule_name_to_insert) + 1;
	int rule_size = slhr_grammar_size_of_rule(grammar, rule_name_to_insert);

	return count * used_size + rule_size > count * rule_size;
}

static bool repair_rule_to_insert(SLHRGrammar* g, Hashmap* rule_dict, uint64_t* rule) {
	HashmapIterator it;
	hashmap_iter(rule_dict, &it);
	MapItem item;

	int64_t res = -1;
	uint64_t res_count = UINT64_MAX, item_count;

	while(hashmap_iter_next(&it, &item)) {
		item_count = *((uint64_t*) item.val);

		if(res < 0 || item_count <= res_count) {
			res = *((int64_t*) item.key);
			res_count = item_count;
		}
	}

	if(res < 0 || !should_continue_inserting_rules(g, res, res_count))
		return false;

	*rule = res;
	return true;
}

static void repair_update_rule_dict(Hashmap* rule_dict, HGraph* rule_to_insert, int count_real_replacements, SLHRGrammar* grammar) {
	size_t edge_count = hgraph_len(rule_to_insert);
	for(size_t i = 0; i < edge_count; i++) {
		HEdge* edge = hgraph_edge_get(rule_to_insert, i);

		if(!slhr_grammar_is_terminal(grammar, edge->label)) {
			uint64_t* count = hashmap_get(rule_dict, &edge->label, sizeof(edge->label), NULL);
			*count += count_real_replacements - 1;
		}
	}
}

static int repair_prune(SLHRGrammar* g) {
	Hashmap* rule_dict = repair_count_rules(g);
	if(!rule_dict)
		return -1;

	int res = -1;

	uint64_t rule_name_to_insert;
	while(repair_rule_to_insert(g, rule_dict, &rule_name_to_insert)) {
		int count_real_replacements = 0;

		HGraph* rule_to_insert = slhr_grammar_rule_get(g, rule_name_to_insert);

		uint64_t next = START_SYMBOL;
		uint64_t i;
		while(slhr_grammar_next_rule(g, &next, &i)) {
			HGraph* rule = slhr_grammar_rule_get(g, i);

			for(size_t index = 0; index < hgraph_len(rule); index++) {
				HEdge* edge = hgraph_edge_get(rule, index);

				if(edge->label == rule_name_to_insert) {
					// In contrast to the Python implementation, this function
					// replaces the edges in `rule` inline
					if(rule_inserter_edges_for_hyperedge(rule_to_insert, rule, edge, index) < 0)
						goto exit;

					// `edge` is already freed in `rule_inserter_edges_for_hyperedge`
					count_real_replacements++;
				}
			}
		}

		repair_update_rule_dict(rule_dict, rule_to_insert, count_real_replacements, g);
		hashmap_remove(rule_dict, &rule_name_to_insert, sizeof(rule_name_to_insert));

		slhr_grammar_rule_del(g, rule_name_to_insert);
	}

	res = 0;

exit:
	hashmap_destroy(rule_dict);
	return res;
}

static int repair_normalize(SLHRGrammar* g) {
	if(g->rule_max == 0) // no rules exists
		return 0;

	uint64_t min_nt = g->min_nt;
	size_t max_nt_count = g->rule_max - min_nt + 2; // +2 because the start symbol is included

	uint64_t* nts = malloc(max_nt_count * sizeof(*nts));
	if(!nts)
		return -1;

	size_t count = 0;

	uint64_t next = START_SYMBOL;
	uint64_t i;
	while(slhr_grammar_next_rule(g, &next, &i))
		nts[count++] = i;

	if(count == 1) { // only start symbol - return
		free(nts);
		return 0;
	}

	// start with i = 1 to skip the start symbol
	for(size_t k = 1; k < count; k++) {
		uint64_t nt = nts[k];
		uint64_t expected_nt = min_nt + k - 1;

		if(nt != expected_nt) {
			uint64_t index_nt = nt - min_nt;
			uint64_t index_expected = expected_nt - min_nt;

			g->rules[index_expected] = g->rules[index_nt];
			g->rules[index_nt] = NULL;

			next = START_SYMBOL;
			while(slhr_grammar_next_rule(g, &next, &i)) { // replace graph in all rules
				HGraph* graph = slhr_grammar_rule_get(g, i);

				size_t edge_count = hgraph_len(graph);
				for(size_t j = 0; j < edge_count; j++) {
					HEdge* edge = hgraph_edge_get(graph, j);
					if(edge->label == nt)
						edge->label = expected_nt;
				}
			}
		}
	}

	g->rule_max = min_nt + count - 2;

	free(nts);
	return 0;
}

SLHRGrammar* repair(HGraph* g, uint64_t nodes, uint64_t terminals, int max_rank, bool replace_monograms) {
	SLHRGrammar* gr = slhr_grammar_init(g, terminals);
	if(!gr) {
		hgraph_destroy(g); // destroy the graph on errors, because the memory of the graph is fully managed in this function
		return NULL;
	}

	// only replace digrams if the max rank is greater than 2
	if(max_rank > 2 && repair_replace_digrams(gr, nodes, max_rank) < 0)
		goto repair_err;
	if(replace_monograms && repair_replace_monograms(gr) < 0)
		goto repair_err;
	if(repair_prune(gr) < 0)
		goto repair_err;
	if(repair_normalize(gr) < 0)
		goto repair_err;

	return gr;

repair_err:
	slhr_grammar_destroy(gr);
	return NULL;
}
