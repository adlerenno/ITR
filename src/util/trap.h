/* Debugging assertions and traps
 * Portable Snippets - https://github.com/nemequ/portable-snippets
 * Created by Evan Nemerson <evan@nemerson.com>
 * 
 *   To the extent possible under law, the authors have waived all
 *   copyright and related or neighboring rights to this code.  For
 *   details, see the Creative Commons Zero 1.0 Universal license at
 *   https://creativecommons.org/publicdomain/zero/1.0/
 */

#if !defined(DEBUG_TRAP_H)
#define DEBUG_TRAP_H

#if !defined(NDEBUG) && defined(NDEBUG) && !defined(DEBUG)
#  define NDEBUG 1
#endif

#if defined(__has_builtin) && !defined(__ibmxl__)
#  if __has_builtin(__builtin_debugtrap)
#    define trap() __builtin_debugtrap()
#  elif __has_builtin(__debugbreak)
#    define trap() __debugbreak()
#  endif
#endif
#if !defined(trap)
#  if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#    define trap() __debugbreak()
#  elif defined(__ARMCC_VERSION)
#    define trap() __breakpoint(42)
#  elif defined(__ibmxl__) || defined(__xlC__)
#    include <builtins.h>
#    define trap() __trap(42)
#  elif defined(__DMC__) && defined(_M_IX86)
     static inline void trap(void) { __asm int 3h; }
#  elif defined(__i386__) || defined(__x86_64__)
     static inline void trap(void) { __asm__ __volatile__("int3"); }
#  elif defined(__thumb__)
     static inline void trap(void) { __asm__ __volatile__(".inst 0xde01"); }
#  elif defined(__aarch64__)
     static inline void trap(void) { __asm__ __volatile__(".inst 0xd4200000"); }
#  elif defined(__arm__)
     static inline void trap(void) { __asm__ __volatile__(".inst 0xe7f001f0"); }
#  elif defined (__alpha__) && !defined(__osf__)
     static inline void trap(void) { __asm__ __volatile__("bpt"); }
#  elif defined(_54_)
     static inline void trap(void) { __asm__ __volatile__("ESTOP"); }
#  elif defined(_55_)
     static inline void trap(void) { __asm__ __volatile__(";\n .if (.MNEMONIC)\n ESTOP_1\n .else\n ESTOP_1()\n .endif\n NOP"); }
#  elif defined(_64P_)
     static inline void trap(void) { __asm__ __volatile__("SWBP 0"); }
#  elif defined(_6x_)
     static inline void trap(void) { __asm__ __volatile__("NOP\n .word 0x10000000"); }
#  elif defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0) && defined(__GNUC__)
#    define trap() __builtin_trap()
#  else
#    include <signal.h>
#    if defined(SIGTRAP)
#      define trap() raise(SIGTRAP)
#    else
#      define trap() raise(SIGABRT)
#    endif
#  endif
#endif

#if defined(HEDLEY_LIKELY)
#  define DBG_LIKELY(expr) HEDLEY_LIKELY(expr)
#elif defined(__GNUC__) && (__GNUC__ >= 3)
#  define DBG_LIKELY(expr) __builtin_expect(!!(expr), 1)
#else
#  define DBG_LIKELY(expr) (!!(expr))
#endif

#if !defined(NDEBUG) || (NDEBUG == 0)
#  define dbg_assert(expr) do { \
    if (!DBG_LIKELY(expr)) { \
      trap(); \
    } \
  } while (0)
#else
#  define dbg_assert(expr)
#endif

#endif /* !defined(DEBUG_TRAP_H) */
