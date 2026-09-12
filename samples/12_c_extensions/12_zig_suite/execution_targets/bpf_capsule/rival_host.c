#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bpf_capsule_host.h"
#include "rival_capsule.h"
#include "rival.skel.h"

static uint64_t scalar_count(const unsigned char *bytes, size_t length) {
    uint64_t total = 0;
    for (size_t i = 0; i < length; ++i) total += bytes[i] == '\n';
    return total;
}

static int read_input(const char *path, unsigned char *bytes, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    size_t used = fread(bytes, 1, RIVAL_CAPSULE_MAX_INPUT + 1u, file);
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    int extra = fgetc(file);
    fclose(file);
    if (used > RIVAL_CAPSULE_MAX_INPUT || extra != EOF) {
        errno = EFBIG;
        return -1;
    }
    *length = used;
    return 0;
}

static uint64_t program_runtime_ns(int fd) {
    struct bpf_prog_info info = {0};
    unsigned int length = sizeof(info);
    return bpf_prog_get_info_by_fd(fd, &info, &length) ? 0 : info.run_time_ns;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s INPUT\n", argv[0]);
        return 2;
    }

    unsigned char input[RIVAL_CAPSULE_MAX_INPUT];
    size_t input_length = 0;
    if (read_input(argv[1], input, &input_length)) {
        perror("input");
        return 2;
    }
    const uint64_t expected = scalar_count(input, input_length);

    struct rival *skeleton = rival__open();
    if (!skeleton) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    struct bpf_capsule capsule = {0};
    const struct bpf_capsule_config config = {
        .fiber_count = 1,
        .heap_bytes = 0,
    };
    int stats_fd = -1;
    int exit_code = 1;

    if (bpf_capsule_configure(&capsule, skeleton->obj, config) ||
        bpf_object__load_skeleton(skeleton->skeleton) ||
        bpf_capsule_initialize(&capsule)) {
        fprintf(stderr, "could not load and initialize Capsule object\n");
        goto cleanup;
    }

    volatile struct rival_capsule_state *state = &skeleton->data_rival->rival_state;
    state->length = (uint32_t)input_length;
    state->output = 0;
    memcpy((void *)state->input, input, input_length);

    stats_fd = bpf_enable_stats(BPF_STATS_RUN_TIME);
    struct bpf_test_run_opts options = {.sz = sizeof(options)};
    const int run_fd = bpf_program__fd(skeleton->progs.rival_count_run);
    const int drain_fd = bpf_program__fd(skeleton->progs.rival_count_drain);

    if (bpf_prog_test_run_opts(run_fd, &options)) {
        perror("run");
        goto cleanup;
    }

    unsigned int drains = 0;
    while (state->capsule.status == CAPSULE_PENDING) {
        if (++drains > 100000u) {
            fprintf(stderr, "Capsule exceeded continuation limit\n");
            goto cleanup;
        }
        if (bpf_prog_test_run_opts(drain_fd, &options)) {
            perror("drain");
            goto cleanup;
        }
    }

    if (state->capsule.status != CAPSULE_OK || state->output != expected) {
        fprintf(stderr,
                "semantic mismatch: status=%s output=%" PRIu64 " expected=%" PRIu64 "\n",
                bpf_capsule_status_string(state->capsule.status),
                state->output,
                expected);
        goto cleanup;
    }

    printf("{\"bytes\":%zu,\"count\":%" PRIu64
           ",\"continuations\":%u,\"kernel_ns\":%" PRIu64 "}\n",
           input_length,
           state->output,
           drains,
           program_runtime_ns(run_fd) + program_runtime_ns(drain_fd));
    exit_code = 0;

cleanup:
    if (stats_fd >= 0) close(stats_fd);
    (void)bpf_capsule_release(&capsule);
    rival__destroy(skeleton);
    return exit_code;
}
