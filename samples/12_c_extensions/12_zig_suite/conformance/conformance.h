#ifndef DRBN_CONFORMANCE_H
#define DRBN_CONFORMANCE_H
#include "../src/backend.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DRBN_ABI_VERSION 1
int drbn_conformance_json(const drbn_backend_ops *, char *output, size_t capacity, size_t *written);
#ifdef __cplusplus
}
#endif
#endif
