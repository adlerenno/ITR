/**
 * @file panic.h
 * @author FR
 */

#ifndef PANIC_H
#define PANIC_H

#define panic(fmt, ...) __panic(__FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

// enable compiler warning for a wrong format parameter:
__attribute__((format(printf, 4, 5)))
void __panic(const char* file, const char* func, int line, const char* fmt, ...);

#endif
