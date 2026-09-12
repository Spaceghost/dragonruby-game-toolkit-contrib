#ifndef DRBZ_METER_H
#define DRBZ_METER_H
#include <stddef.h>
#include <stdint.h>
/* Single-threaded measurement process only. Attempts, not Ruby object counts. */
typedef struct {
    uint64_t malloc_calls, calloc_calls, realloc_calls, free_calls, aligned_calls;
    uint64_t failed_calls, requested_bytes, overflow_requests;
    uint64_t live_before, live_after, peak_live;
    int live_supported;
} meter_stats;
void meter_begin(void);
meter_stats meter_end(void);
void meter_selftest(void);
void meter_inject(void);
#endif
