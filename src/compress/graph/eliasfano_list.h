/**
 * @file eliasfano_list.h
 * @author FR
 */

#ifndef ELIASFANO_LIST_H
#define ELIASFANO_LIST_H

#include <writer.h>

int eliasfano_write(const uint64_t* list, size_t n, BitWriter* w, const BitsequenceParams* p);

#endif
