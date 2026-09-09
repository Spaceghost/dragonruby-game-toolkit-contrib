#ifndef DRBZ_NATIVE_H
#define DRBZ_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native kernels do not allocate. Buffers must be valid for their lengths;
 * distinct SoA arrays must not overlap. Random callbacks must not raise Ruby
 * exceptions, mutate the input arrays or re-enter the current operation.
 * No Ruby value, mruby layout or SDK host table crosses this boundary. */
int drbz_square(int value, int *out);
int drbz_squares(const int32_t *input, int32_t *output, size_t len);
double drbz_sum_ordered(double accumulator, const double *values, size_t len);
double drbz_sum_unrolled(double accumulator, const double *values, size_t len);
typedef struct { float x, y, s; } drbz_star;
typedef float (*drbz_random)(void *context);
void drbz_stars_scalar(drbz_star *, size_t, drbz_random, void *);
void drbz_stars_soa(float *x, float *y, const float *speed, size_t, drbz_random, void *);
size_t drbz_count_scalar(const unsigned char *, size_t);
size_t drbz_count_blocked(const unsigned char *, size_t);
typedef struct { int32_t position, increment, previous; uint32_t pixels[100]; } drbz_scanner;
void drbz_scanner_reset(drbz_scanner *);
const uint32_t *drbz_scanner_frame(drbz_scanner *);
/* Caller-owned compiled storage. Failed compilation leaves the previous value
 * untouched; search requires a successful compile. Work limits count matcher
 * steps, not elapsed time. Concurrent searches need separate length outputs. */
typedef union { max_align_t alignment; unsigned char bytes[2048]; } drbz_regex;
int drbz_regex_compile(void *storage, const unsigned char *pattern, size_t len);
/* Index, -1 for no match, or -2 for work/size limit. For tiny-regex-c
 * compatibility, the length of a failed match is not necessarily zero. */
int drbz_regex_search(const void *storage, const unsigned char *text, size_t len,
                      int *length, int optimized, size_t work_limit);
#ifdef __cplusplus
}
#endif
#endif
