#ifndef DRBZ_APPS_H
#define DRBZ_APPS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Reader views: 1 = number, 2 = array, anything else = unsupported.
 * Readers must not allocate, raise, execute Ruby, or mutate borrowed input. */
typedef struct { int kind; double number; const void *children; size_t length; } drbz_view;
typedef void (*drbz_reader)(void *, const void *, size_t, drbz_view *);
typedef size_t (*drbz_reader_batch)(void *, const void *, size_t, drbz_view *, size_t capacity);
/* 0 = success, 1 = unsupported/invalid reader output, 2 = depth/cycle failure.
 * Failure leaves result unchanged. Maximum active array depth is 64. */
int drbz_sum_tree(const void *, size_t, drbz_reader, void *, double *result);
/* Same semantics, but asks the adapter for up to 16 adjacent values per call.
 * The reader returns 1..capacity views. A batch may be re-read after a nested
 * child because depth-first order deliberately avoids retaining Ruby views. */
int drbz_sum_tree_batched(const void *, size_t, drbz_reader_batch, void *, double *result);
/* Parallel Odin implementations use the same host-reader contract and status
 * codes, so adapters can compare native languages without knowing Odin types. */
int drbo_sum_tree(const void *, size_t, drbz_reader, void *, double *result);
int drbo_sum_tree_batched(const void *, size_t, drbz_reader_batch, void *, double *result);
/* No overlap. The result is NUL terminated; written excludes that NUL.
 * Insufficient capacity leaves output and written untouched. */
int drbz_greeting(int goodbye, const unsigned char *, size_t,
                   unsigned char *output, size_t capacity, size_t *written);
int drbo_greeting(int goodbye, const unsigned char *, size_t,
                   unsigned char *output, size_t capacity, size_t *written);

typedef int (*drbz_entry)(void *);
typedef struct {
    void *(*create)(void *, drbz_entry, void *);
    void (*join)(void *, void *);
    void (*log)(void *);
    void (*delay)(void *, uint32_t);
} drbz_worker_host;
typedef struct { drbz_worker_host host; void *context; void *thread; int running; } drbz_worker;
/* init/start/stop are owner-thread operations. Do not re-enter them from
 * callbacks or destroy/move the worker until stop has joined its thread.
 * Host callbacks must not access the Ruby VM. The host may allocate a native
 * thread; this is not a claim that thread creation is allocation-free. */
void drbz_worker_init(drbz_worker *, const drbz_worker_host *, void *context);
int drbz_worker_start(drbz_worker *);
int drbz_worker_running(const drbz_worker *);
void drbz_worker_stop(drbz_worker *);
void drbo_worker_init(drbz_worker *, const drbz_worker_host *, void *context);
int drbo_worker_start(drbz_worker *);
int drbo_worker_running(const drbz_worker *);
void drbo_worker_stop(drbz_worker *);
#ifdef __cplusplus
}
#endif
#endif
