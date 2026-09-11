#ifndef DRBZ_COMPETITIVE_H
#define DRBZ_COMPETITIVE_H
#include <stddef.h>
#include <stdint.h>

/* Matched C/Zig candidates. Empty ranges may use NULL. Float arrays must be
 * distinct, valid for count elements and not modified or inspected by the RNG
 * callback. The callback consumes only its own state; x-before-y/star order is
 * retained. C uses Clang vector extensions, not portable ISO C or per-ISA asm. */
typedef float (*rival_random)(void *);
size_t drbc_count_dual(const unsigned char *, size_t);
size_t drbz_count_dual(const unsigned char *, size_t);
void drbc_stars_block(float *, float *, const float *, size_t, rival_random, void *);
void drbz_stars_block(float *, float *, const float *, size_t, rival_random, void *);
/* Exact-order scalar fallback used by the Zig tail and available to embeddings. */
void drbz_stars_scalar_fallback(float *, float *, const float *, size_t, rival_random, void *);
#endif
