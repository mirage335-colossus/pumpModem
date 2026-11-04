#ifndef DATAPUMP_SPECULATION_H
#define DATAPUMP_SPECULATION_H

#include <stddef.h>

/* Targeted bounds-check-bypass mitigation, not a general CPU-vulnerability
 * guarantee. Keep ordinary input validation and allocation bounds checks.
 * For an array access, size must be its actual, trusted NONEMPTY extent and
 * callers must use the returned index. Zero is the safe fallback element.
 * Numeric clipping with size == 0 returns 0 but never authorizes an array
 * access; a consumer must independently provide valid nonempty storage.
 * There is no speculation-safety claim on an unsupported target/compiler.
 *
 * Design references (no imported implementation):
 * https://www.kernel.org/doc/html/latest/staging/speculation.html
 * https://documentation-service.arm.com/static/6227498d8804d00769e9b32f
 * https://learn.microsoft.com/en-us/cpp/security/developer-guidance-speculative-execution
 * CPU/firmware/OS support still matters, including dispatch-serializing LFENCE
 * on x86. These helpers do not mitigate Meltdown or every Spectre variant.
 */

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#define DATAPUMP_INDEX_NOSPEC_SUPPORTED 1
#if defined(__x86_64__) || defined(__SSE2__)
#define DATAPUMP_SPECULATION_BARRIER_SUPPORTED 1
#define DATAPUMP_SPECULATION_BACKEND "x86 register mask and LFENCE"
#else
#define DATAPUMP_SPECULATION_BARRIER_SUPPORTED 0
#define DATAPUMP_SPECULATION_BACKEND "x86 register mask only - no SSE2 barrier"
#endif
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__) && defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 8
#define DATAPUMP_INDEX_NOSPEC_SUPPORTED 1
#define DATAPUMP_SPECULATION_BARRIER_SUPPORTED 1
#define DATAPUMP_SPECULATION_BACKEND "AArch64 CSEL/CSDB and DSB/ISB"
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#include <emmintrin.h>
#define DATAPUMP_INDEX_NOSPEC_SUPPORTED 1
#define DATAPUMP_SPECULATION_BARRIER_SUPPORTED 1
#define DATAPUMP_SPECULATION_BACKEND "MSVC x86 checked LFENCE"
#else
#define DATAPUMP_INDEX_NOSPEC_SUPPORTED 0
#define DATAPUMP_SPECULATION_BARRIER_SUPPORTED 0
#define DATAPUMP_SPECULATION_BACKEND "unsupported - architectural bounds only"
#endif

/* Use after successful validation, before exposing validated data to a more
 * complex consumer. Receive DSP does not call this explicitly per sample;
 * the MSVC index-helper fallback below nevertheless fences accepted indices.
 */
static inline void datapump_speculation_barrier(void) {
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || (defined(__i386__) && defined(__SSE2__)))
    __asm__ __volatile__("lfence" ::: "memory");
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__) && defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 8
    __asm__ __volatile__("dsb sy\n\tisb" ::: "memory");
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _ReadWriteBarrier();
    _mm_lfence();
    _ReadWriteBarrier();
#elif defined(__GNUC__) || defined(__clang__)
    /* Compiler ordering only. The capability macros above explicitly say 0. */
    __asm__ __volatile__("" ::: "memory");
#endif
}

static inline size_t datapump_index_nospec(size_t index,size_t size) {
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    size_t mask;
    /* Keep comparison, mask and clipping in one opaque register dependency.
     * A C ternary or mask alone may be optimized back into a predicted branch.
     * Dialect alternatives also support callers compiled with -masm=intel.
     */
    __asm__ __volatile__(
        "cmp{ %[size], %[index] | %[index], %[size]}\n\t"
        "sbb %[mask], %[mask]\n\t"
        "and{ %[mask], %[index] | %[index], %[mask]}"
        : [index] "+r" (index),[mask] "=&r" (mask)
        : [size] "r" (size)
        : "cc");
    return index;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__) && defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 8
    size_t safe;
    __asm__ __volatile__(
        "cmp %x[index], %x[size]\n\t"
        "csel %x[safe], %x[index], xzr, lo\n\t"
        "hint #20" /* CSDB, including assemblers targeting baseline Armv8. */
        : [safe] "=r" (safe)
        : [index] "r" (index),[size] "r" (size)
        : "cc");
    return safe;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    /* MSVC x64 has no GNU inline assembly. Serialize the successful checked
     * path before returning an index to the caller's dependent memory access.
     */
    if(index<size) {
        datapump_speculation_barrier();
        return index;
    }
    return 0;
#else
    return index<size?index:0;
#endif
}

#endif
