/**
 * @file rrr_writer.h
 * @author FR
 */

#ifndef RRR_WRITER_H
#define RRR_WRITER_H

#ifndef RRR
#error "RRR only available if built with -DWITH_RRR"
#endif

#include <writer.h>
#include <bitarray.h>

int bitwriter_write_bitsequence_rrr(BitWriter* w, const BitArray* b, int sample_rate);

#endif
