/**
 * @file edge.c
 * @author FR
 */

#include "edge.h"

#include <stdbool.h>
#include <reader.h>

// works the same as in the Python implementation
void edge_read(Reader* r, StEdge* e) {
	e->label = reader_eliasdelta(r);
	e->rank = reader_eliasdelta(r);

	for(int i = 0; i < e->rank; i++)
		e->nodes[i] = reader_eliasdelta(r);
}
