/**
 * @file table.h
 * @author FR
 */

#ifndef TABLE_H
#define TABLE_H

#include <stdint.h>

// Values for bit sequences based on RRR.
// Do not change!
#define BITS_PER_BLOCK 15
#define BLOCK_TYPE_BITS 4 // bit width of BITS_PER_BLOCK

uint8_t table_class_size(uint8_t n);
uint16_t table_compute_offset(uint16_t v);
uint16_t table_short_bitmap(uint8_t class_offset, uint16_t inclass_offset);

#endif
