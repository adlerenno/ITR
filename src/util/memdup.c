/**
 * @file memdup.c
 * @author FR
 */

#include <stdlib.h>
#include <string.h>

// duplicating the memory at the address
void* memdup(const void* mem, size_t size) { 
	void* out = malloc(size);
	if(out)
		memcpy(out, mem, size);

	return out;
}
