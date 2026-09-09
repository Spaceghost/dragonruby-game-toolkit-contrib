/* Scalar-source controls. These are NOT original repository implementations.
 * The compiler may autovectorize them: baseline C gets the same optimization
 * level and CPU target as Zig, without disabling its optimizer. */
#include "native.h"

double control_c_sum(double accumulator, const double *values, size_t len) {
    for (size_t i = 0; i < len; ++i) accumulator += values[i];
    return accumulator;
}
size_t control_c_count(const unsigned char *bytes, size_t len) {
    size_t total = 0;
    for (size_t i = 0; i < len; ++i) total += bytes[i] == '\n';
    return total;
}
