/**
 * @file rule_creator.c
 * @author FR
 */

#include "rule_creator.h"

#include <stdlib.h>

static HEdge* digram_build_edge(uint64_t label, size_t connection_type, size_t rank_of_rule, size_t node_offset) {
	HEdge* edge = malloc(hedge_sizeof(rank_of_rule));
	if(!edge)
		return NULL;

	edge->label = label;
	edge->rank = rank_of_rule;

	uint64_t v;
	for(size_t i = 0; i < rank_of_rule; i++) {
		if(i < connection_type)
			v = node_offset + i;
		else if(i == connection_type)
			v = 0;
		else
			v = node_offset + i - 1;

		edge->nodes[i] = v;
	}

	return edge;
}

int rule_creator_digram_init(RuleCreator* c, SLHRGrammar* g, const Digram* digram) {
	uint64_t label[2];
	size_t rank[2];
	HEdge* edges[2];

	label[0] = digram->adj0.label;
	label[1] = digram->adj1.label;

	rank[0] = slhr_grammar_rank_of_rule(g, label[0]);
	rank[1] = slhr_grammar_rank_of_rule(g, label[1]);

	edges[0] = digram_build_edge(label[0], digram->adj0.conn_type, rank[0], 1);
	if(!edges[0])
		return -1;

	edges[1] = digram_build_edge(label[1], digram->adj1.conn_type, rank[1], rank[0]);
	if(!edges[1])
		goto err_0;

	HGraph* graph = hgraph_init(rank[0] + rank[1] - 1);
	if(!graph)
		goto err_1;

	if(hgraph_add_edge(graph, edges[0]) < 0)
		goto err_2;
	if(hgraph_add_edge(graph, edges[1]) < 0)
		goto err_2;

	c->digram = digram;
	c->rule = graph;
	c->rule_name = slhr_grammar_unused_nt(g);
	c->free = true;

	return 0;

err_2:
	hgraph_destroy(graph);
err_1:
	free(edges[1]);
err_0:
	free(edges[0]);
	return -1;
}

static HEdge* monogram_build_edge(uint64_t label, size_t connection_type_1, size_t connection_type_2, size_t rank_of_rule) {
	HEdge* edge = malloc(hedge_sizeof(rank_of_rule));
	if(!edge)
		return NULL;

	edge->label = label;
	edge->rank = rank_of_rule;

	uint64_t v;
	for(size_t i = 0; i < rank_of_rule; i++) {
		if(i < connection_type_2)
			v = i;
		else if(i == connection_type_2)
			v = connection_type_1;
		else
			v = i - 1;

		edge->nodes[i] = v;
	}

	return edge;
}

int rule_creator_monogram_init(RuleCreator* c, SLHRGrammar* g, const Monogram* monogram) {
	size_t rank = slhr_grammar_rank_of_rule(g, monogram->label);

	HEdge* edge = monogram_build_edge(monogram->label, monogram->conn0, monogram->conn1, rank);
	if(!edge)
		return -1;

	HGraph* graph = hgraph_init(rank - 1);
	if(!graph)
		goto err_0;

	if(hgraph_add_edge(graph, edge) < 0)
		goto err_1;

	c->monogram = monogram;
	c->rule = graph;
	c->rule_name = slhr_grammar_unused_nt(g);
	c->free = true;

	return 0;

err_1:
	hgraph_destroy(graph);
err_0:
	free(edge);
	return -1;
}

void rule_creator_destroy(RuleCreator* c) {
	if(c->rule && c->free) {
		hgraph_destroy(c->rule);
		c->free = false; // set it to false because this function may be called directly after
	}
}

HEdge* rule_creator_digram_new_edge(RuleCreator* c, HEdge* edge_1, HEdge* edge_2) {
	uint64_t shared_node = edge_1->nodes[c->digram->adj0.conn_type];

	size_t rank = edge_1->rank + edge_2->rank - 1;
	HEdge* edge = malloc(hedge_sizeof(rank));
	if(!edge)
		return NULL;

	edge->label = c->rule_name;
	edge->rank = rank;
	edge->nodes[0] = shared_node;

	size_t i, index = 1;

	size_t conn_type = c->digram->adj0.conn_type;
	for(i = 0; i < edge_1->rank; i++)
		if(i != conn_type)
			edge->nodes[index++] = edge_1->nodes[i];

	conn_type = c->digram->adj1.conn_type;
	for(i = 0; i < edge_2->rank; i++)
		if(i != conn_type)
			edge->nodes[index++] = edge_2->nodes[i];

	return edge;
}

HEdge* rule_creator_monogram_new_edge(RuleCreator* c, HEdge* old_edge) {
	size_t new_rank = old_edge->rank - 1;

	HEdge* edge = malloc(hedge_sizeof(new_rank));
	if(!edge)
		return NULL;

	edge->label = c->rule_name;
	edge->rank = new_rank;

	uint64_t v;
	for(size_t i = 0; i < new_rank; i++) {
		if(i < c->monogram->conn1)
			v = old_edge->nodes[i];
		else
			v = old_edge->nodes[i + 1];

		edge->nodes[i] = v;
	}

	return edge;
}

int rule_inserter_edges_for_hyperedge(const HGraph* rule_to_insert, HGraph* rule, HEdge* hyperedge, size_t index) {
	int res = -1;
	size_t count = hgraph_len(rule_to_insert);

	size_t i;
	for(i = 0; i < count; i++) {
		HEdge* e = hgraph_edge_get(rule_to_insert, i);

		HEdge* edge = malloc(hedge_sizeof(e->rank));
		if(!edge)
			goto exit;

		edge->label = e->label;
		edge->rank = e->rank;
		for(size_t j = 0; j < e->rank; j++)
			edge->nodes[j] = hyperedge->nodes[e->nodes[j]];

		if(i == 0)
			// no setting the edge instead of replacing it because it would free `hyperedge` despite it is still needed
			hgraph_edge_set(rule, index, edge);
		else {
			if(hgraph_add_edge(rule, edge) < 0) {
				free(edge);
				goto exit;
			}
		}
	}

	res = 0;

exit:
	if(i > 0) // only free if the first edge has been replaced
		free(hyperedge);
	return res;
}
