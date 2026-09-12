#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#include "bpf_capsule.h"
#include "rival_capsule.h"
#include "../../src/competitive.h"

volatile struct rival_capsule_state rival_state SEC(".data.rival");

static uint64_t run_count(void) {
    return (uint64_t)drbc_count_dual(
        (const unsigned char *)rival_state.input,
        (size_t)rival_state.length
    );
}

SEC("syscall")
int rival_count_run(void) {
    if (rival_state.length > RIVAL_CAPSULE_MAX_INPUT) {
        rival_state.output = UINT64_MAX;
        return 0;
    }
    rival_state.capsule = capsule_call(&rival_state.output, run_count);
    return 0;
}

SEC("syscall")
int rival_count_drain(void) {
    rival_state.capsule = capsule_continue(
        &rival_state.output,
        rival_state.capsule.continuation
    );
    return 0;
}

char _license[] SEC("license") = "GPL";
