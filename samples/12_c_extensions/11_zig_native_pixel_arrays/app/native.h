#ifndef DRB_ZIG_NATIVE_H
#define DRB_ZIG_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DRB_ZIG_DIMENSION = 10, DRB_ZIG_PIXEL_COUNT = 100 };

/* Scanner calls are main-thread-only; reset starts again at row zero. */
void drb_zig_scanner_reset(void);
/* pixels must point to at least 100 writable, uint32_t-aligned elements. */
void drb_zig_scanner_frame(uint32_t *pixels);
/* data must contain len readable bytes. NULL is allowed only when len == 0.
   No pointer is retained, and no allocation or Ruby callback is performed. */
size_t drb_zig_count_newlines(const unsigned char *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif
