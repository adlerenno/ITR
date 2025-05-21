/**
 * @file k2_writer.h
 * @author FR
 */

#ifndef K2_WRITER_H
#define K2_WRITER_H

#include <stddef.h>
#include <writer.h>

typedef struct {
	size_t xval;
	size_t yval;
	size_t kval;
} K2Edge;

int k2_write(size_t width, size_t height, K2Edge* edges, size_t edge_count, BitWriter* w, const BitsequenceParams* p);

#endif
