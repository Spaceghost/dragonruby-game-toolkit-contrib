#ifndef DRBN_BACKEND_H
#define DRBN_BACKEND_H
#include "native.h"
#include "apps.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct drbn_backend_ops {
    const char *name;
    int (*square)(int, int *);
    int (*squares)(const int32_t *, int32_t *, size_t);
    double (*sum_ordered)(double, const double *, size_t);
    double (*sum_unrolled)(double, const double *, size_t);
    void (*stars_scalar)(drbz_star *, size_t, drbz_random, void *);
    void (*stars_soa)(float *, float *, const float *, size_t, drbz_random, void *);
    size_t (*count_scalar)(const unsigned char *, size_t);
    size_t (*count_fast)(const unsigned char *, size_t);
    void (*scanner_reset)(drbz_scanner *);
    const uint32_t *(*scanner_frame)(drbz_scanner *);
    int (*sum_tree)(const void *, size_t, drbz_reader, void *, double *);
    int (*sum_tree_batched)(const void *, size_t, drbz_reader_batch, void *, double *);
    int (*greeting)(int, const unsigned char *, size_t, unsigned char *, size_t, size_t *);
    size_t (*starfield_storage_bytes)(size_t);
    int (*starfield_init)(void *, size_t, size_t, uint64_t, drbz_starfield *);
    void (*starfield_update)(drbz_starfield *);
    void (*starfield_pack)(drbz_starfield *);
    void (*starfield_update_pack)(drbz_starfield *);
    void (*starfield_frame)(drbz_starfield *, drbz_sprite_batch_sink, void *);
} drbn_backend_ops;

const drbn_backend_ops *drbn_backend_at(size_t index);
const drbn_backend_ops *drbn_backend_by_name(const char *name, size_t length);
size_t drbn_backend_count(void);

int drbc_square(int value, int *out);
int drbc_squares(const int32_t *input, int32_t *output, size_t len);
double drbc_sum_ordered(double accumulator, const double *values, size_t len);
double drbc_sum_unrolled(double accumulator, const double *values, size_t len);
void drbc_stars_scalar(drbz_star *, size_t, drbz_random, void *);
void drbc_stars_soa(float *, float *, const float *, size_t, drbz_random, void *);
size_t drbc_count_scalar(const unsigned char *, size_t);
void drbc_scanner_reset(drbz_scanner *);
const uint32_t *drbc_scanner_frame(drbz_scanner *);
int drbc_sum_tree(const void *, size_t, drbz_reader, void *, double *);
int drbc_sum_tree_batched(const void *, size_t, drbz_reader_batch, void *, double *);
int drbc_greeting(int, const unsigned char *, size_t, unsigned char *, size_t, size_t *);
size_t drbc_starfield_storage_bytes(size_t count);
int drbc_starfield_init(void *, size_t, size_t, uint64_t, drbz_starfield *);
void drbc_starfield_update(drbz_starfield *);
void drbc_starfield_pack(drbz_starfield *);
void drbc_starfield_update_pack(drbz_starfield *);
void drbc_starfield_frame(drbz_starfield *, drbz_sprite_batch_sink, void *);
#ifdef __cplusplus
}
#endif
#endif
