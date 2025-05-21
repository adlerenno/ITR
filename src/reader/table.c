/**
 * @file table.c
 * @author FR
 */

#include "table.h"
#include "table_data.h"

uint8_t table_class_size(uint8_t n) {
	return class_sizes[n];
}

uint16_t table_compute_offset(uint16_t v) {
	return rev_offset[v];
}

uint16_t table_short_bitmap(uint8_t class_offset, uint16_t inclass_offset) {
	if(class_offset == 0)
		return 0;
	if(class_offset == BITS_PER_BLOCK)
		return (1 << BITS_PER_BLOCK) - 1;
	return short_bitmaps[offset_class[class_offset] + inclass_offset];
}
