#ifndef DRBZ_RIVAL_CAPSULE_H
#define DRBZ_RIVAL_CAPSULE_H

#include <stdint.h>
#include "bpf_capsule_types.h"

#define RIVAL_CAPSULE_MAX_INPUT (64u * 1024u)

struct rival_capsule_state {
    uint32_t length;
    uint32_t reserved;
    uint64_t output;
    struct capsule_result capsule;
    unsigned char input[RIVAL_CAPSULE_MAX_INPUT];
};

#endif
