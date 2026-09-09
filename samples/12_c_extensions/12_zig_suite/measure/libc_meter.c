#include "meter.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
/* GNU --wrap observes references from the linked objects, not calls internal to
 * shared libraries or direct mmap/system calls. Timing binaries omit this file. */
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__real_aligned_alloc(size_t, size_t);
int __real_posix_memalign(void **, size_t, size_t);
static meter_stats stats;
static int active, fail_next;
static volatile unsigned char observed;
static void requested(size_t n) {
    assert(UINT64_MAX - stats.requested_bytes >= n);
    stats.requested_bytes += n;
}
void *__wrap_malloc(size_t n) {
    void *p = __real_malloc(n);
    if (active) { ++stats.malloc_calls; requested(n); stats.failed_calls += p == NULL && n != 0; }
    return p;
}
void *__wrap_calloc(size_t n, size_t size) {
    void *p = __real_calloc(n, size);
    if (active) {
        ++stats.calloc_calls;
        if (size && n > SIZE_MAX / size) ++stats.overflow_requests;
        else requested(n * size);
        stats.failed_calls += p == NULL && n != 0 && size != 0;
    }
    return p;
}
void *__wrap_realloc(void *old, size_t n) {
    void *p;
    if (active && fail_next) { fail_next = 0; errno = ENOMEM; p = NULL; }
    else p = __real_realloc(old, n);
    if (active) { ++stats.realloc_calls; requested(n); stats.failed_calls += p == NULL && n != 0; }
    return p;
}
void __wrap_free(void *p) {
    if (active && p) ++stats.free_calls;
    __real_free(p);
}
void *__wrap_aligned_alloc(size_t align, size_t n) {
    void *p = __real_aligned_alloc(align, n);
    if (active) { ++stats.aligned_calls; requested(n); stats.failed_calls += p == NULL && n != 0; }
    return p;
}
int __wrap_posix_memalign(void **p, size_t align, size_t n) {
    int rc = __real_posix_memalign(p, align, n);
    if (active) { ++stats.aligned_calls; requested(n); stats.failed_calls += rc != 0; }
    return rc;
}
void meter_begin(void) { assert(!active); memset(&stats, 0, sizeof stats); active = 1; }
meter_stats meter_end(void) { assert(active); active = 0; return stats; }
void meter_inject(void) {
    unsigned char *p = malloc(17); assert(p); p[0] = 9; observed = p[0]; free(p);
}
void meter_selftest(void) {
    meter_begin();
    unsigned char *p = malloc(16); assert(p); memset(p, 7, 16);
    unsigned char *q = calloc(2, 8); assert(q && q[0] == 0);
    p = realloc(p, 32); assert(p && p[0] == 7);
    fail_next = 1;
    void *failed = realloc(p, 128); assert(failed == NULL && p[0] == 7);
    observed = p[15];
    void *a = aligned_alloc(32, 64); assert(a && (uintptr_t)a % 32 == 0);
    void *b = NULL; assert(posix_memalign(&b, 64, 64) == 0 && (uintptr_t)b % 64 == 0);
    free(p); free(q); free(a); free(b);
    meter_stats s = meter_end();
    assert(s.malloc_calls == 1 && s.calloc_calls == 1 && s.realloc_calls == 2);
    assert(s.aligned_calls == 2 && s.free_calls == 4 && s.failed_calls == 1);
    assert(s.requested_bytes == 320 && s.overflow_requests == 0);
}
