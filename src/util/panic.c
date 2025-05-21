/**
 * @file panic.c
 * @author FR
 */

#include "panic.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <execinfo.h>
#include <string.h>
#include <trap.h>

/**
 * WARNING: Use with caution!
 * 
 * For unrecoverable, we call panic(...) which prints an error and the stack trace
 * and which sends a trap signal to the OS.
 */
void __panic(const char* file, const char* func, int line, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);

	fprintf(stderr, "panic at %s:%d(%s): ", file, line, func);
	vfprintf(stderr, fmt, args);
	if(fmt[strlen(fmt) - 1] != '\n')
		fprintf(stderr, "\n");
	fprintf(stderr, "Call stack:\n");

	void* callstack[128];
	int frames = backtrace(callstack, sizeof(callstack) / sizeof(*callstack)); // determining the stack trace
	char** strs = backtrace_symbols(callstack, frames);
	for(int i = 0; i < frames; i++)
		fprintf(stderr, "\t%s\n", strs[i]);
	free(strs);

	trap(); // trap signal - dependend on OS and architecture
	exit(1); // does not matter because we exit the application anyway
}
