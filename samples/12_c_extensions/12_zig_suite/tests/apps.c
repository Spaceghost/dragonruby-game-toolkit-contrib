#include "apps.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void read_view(void *context, const void *source, size_t index, drbz_view *view) {
    (void)context;
    *view = ((const drbz_view *)source)[index];
}
struct read_stats { size_t calls, values; };
static size_t read_views(void *raw, const void *source, size_t index, drbz_view *views, size_t capacity) {
    struct read_stats *stats = raw;
    ++stats->calls; stats->values += capacity;
    memcpy(views, (const drbz_view *)source + index, capacity * sizeof *views);
    return capacity;
}

struct host {
    pthread_t thread;
    drbz_entry entry;
    void *argument;
    atomic_uint logs;
    unsigned creates, joins;
    int fail;
};
static void *thread_main(void *raw) {
    struct host *host = raw;
    assert(host->entry(host->argument) == 0);
    return NULL;
}
static void *create(void *raw, drbz_entry entry, void *argument) {
    struct host *host = raw;
    ++host->creates;
    if (host->fail) return NULL;
    host->entry = entry;
    host->argument = argument;
    if (pthread_create(&host->thread, NULL, thread_main, host) != 0) return NULL;
    return &host->thread;
}
static void join(void *raw, void *thread) {
    struct host *host = raw;
    assert(thread == &host->thread);
    assert(pthread_join(*(pthread_t *)thread, NULL) == 0);
    ++host->joins;
}
static void log_message(void *raw) {
    struct host *host = raw;
    atomic_fetch_add_explicit(&host->logs, 1, memory_order_relaxed);
}
static void delay(void *raw, uint32_t milliseconds) {
    (void)raw;
    assert(milliseconds == 1000);
    struct timespec duration = { 0, 1000000 };
    nanosleep(&duration, NULL);
}

int main(void) {
    drbz_view nested[] = { {1, 1, NULL, 0}, {1, -1e16, NULL, 0} };
    drbz_view root[] = { {1, 1e16, NULL, 0}, {2, 0, nested, 2}, {1, 3, NULL, 0} };
    double result = 777;
    assert(drbz_sum_tree(root, 3, read_view, NULL, &result) == 0 && result == 3);
    struct read_stats stats = {0};
    result = 777;
    assert(drbz_sum_tree_batched(root, 3, read_views, &stats, &result) == 0 && result == 3);
    assert(stats.calls == 3 && stats.values == 6); /* trailing root sibling is deliberately re-decoded */
    drbz_view flat[64];
    for (size_t i=0;i<64;++i) flat[i]=(drbz_view){1,(double)(i+1),NULL,0};
    stats=(struct read_stats){0}; result=777;
    assert(drbz_sum_tree_batched(flat,64,read_views,&stats,&result)==0 && result==2080);
    assert(stats.calls==4 && stats.values==64);
    drbz_view cycle = {2, 0, NULL, 1};
    cycle.children = &cycle;
    result = 777;
    assert(drbz_sum_tree(&cycle, 1, read_view, NULL, &result) == 2 && result == 777);
    stats=(struct read_stats){0}; result=777;
    assert(drbz_sum_tree_batched(&cycle,1,read_views,&stats,&result)==2 && result==777);
    unsigned char output[64];
    size_t length = 0;
    assert(drbz_greeting(0, (const unsigned char *)"Zig", 3, output, sizeof output, &length) == 0);
    assert(length == 10 && strcmp((const char *)output, "Hello Zig!") == 0);
    const drbz_worker_host callbacks = {create, join, log_message, delay};
    struct host host = {0};
    atomic_init(&host.logs, 0);
    drbz_worker worker;
    drbz_worker_init(&worker, &callbacks, &host);
    drbz_worker_stop(&worker);
    host.fail = 1;
    assert(drbz_worker_start(&worker) == 1);
    assert(drbz_worker_running(&worker) == 0);
    host.fail = 0;
    for (unsigned lifetime = 0; lifetime < 100; ++lifetime) {
        assert(drbz_worker_start(&worker) == 0);
        assert(drbz_worker_start(&worker) == 0);
        assert(drbz_worker_running(&worker) == 1);
        drbz_worker_stop(&worker);
        drbz_worker_stop(&worker);
        assert(drbz_worker_running(&worker) == 0);
    }
    assert(host.creates == 101 && host.joins == 100);
    printf("APPS_PROOF {\"native_thread_lifetimes\":100,\"batched_flat_reader_calls\":4,\"failed_create_recovered\":true,\"ruby_vm_used_on_worker\":false}\n");
    return 0;
}
