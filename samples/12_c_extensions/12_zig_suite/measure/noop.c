#include <stdint.h>
/* Separately compiled, no LTO. This control is reported, never subtracted. */
uint64_t drbz_measure_noop(uint64_t value) { return value; }
