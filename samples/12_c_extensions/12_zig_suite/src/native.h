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

/* Parallel Odin C ABI. These deliberately mirror the portable Zig operations so
 * one C/Ruby host adapter can compare languages without duplicating SDK glue. */
int32_t drbo_square(int32_t value, int32_t *out);
int32_t drbo_squares(const int32_t *input, int32_t *output, size_t len);
double drbo_sum_ordered(double accumulator, const double *values, size_t len);
double drbo_sum_unrolled(double accumulator, const double *values, size_t len);
void drbo_stars_scalar(drbz_star *, size_t, drbz_random, void *);
void drbo_stars_soa(float *x, float *y, const float *speed, size_t, drbz_random, void *);
size_t drbo_count_scalar(const unsigned char *, size_t);
size_t drbo_count_blocked(const unsigned char *, size_t);
void drbo_scanner_reset(drbz_scanner *);
const uint32_t *drbo_scanner_frame(drbz_scanner *);

/* Persistent starfield state lives entirely in caller-owned storage. A frame
 * does one native update, one pack into borrowed sprite records, then one sink
 * call. Storage may be replaced only between frames. path_id=1 is the sample's
 * shared tiny-star asset; an SDK adapter maps it to its renderer representation. */
typedef struct { float x, y, w, h; uintptr_t path_id; } drbz_packed_sprite;
typedef struct {
    float *x, *y, *speed;
    drbz_packed_sprite *sprites;
    size_t len;
    uint64_t rng_state;
} drbz_starfield;
typedef void (*drbz_sprite_batch_sink)(void *, const drbz_packed_sprite *, size_t);
size_t drbz_starfield_storage_bytes(size_t count);
int drbz_starfield_init(void *storage, size_t storage_bytes, size_t count, uint64_t seed, drbz_starfield *out);
void drbz_starfield_update(drbz_starfield *);
void drbz_starfield_pack(drbz_starfield *);
void drbz_starfield_update_pack(drbz_starfield *);
void drbz_starfield_frame(drbz_starfield *, drbz_sprite_batch_sink, void *);
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
