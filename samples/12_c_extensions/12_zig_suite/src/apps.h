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
/* 0 = success, 1 = unsupported value, 2 = depth/cycle failure.
 * Failure leaves result unchanged. Maximum active array depth is 64. */
int drbz_sum_tree(const void *, size_t, drbz_reader, void *, double *result);
/* No overlap. The result is NUL terminated; written excludes that NUL.
 * Insufficient capacity leaves output and written untouched. */
int drbz_greeting(int goodbye, const unsigned char *, size_t,
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
#ifdef __cplusplus
}
#endif
#endif
