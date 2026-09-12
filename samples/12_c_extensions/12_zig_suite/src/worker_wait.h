#ifndef DRBN_WORKER_WAIT_H
#define DRBN_WORKER_WAIT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*drbn_wait_entry)(void *);
typedef struct {
    void *(*create)(void *, drbn_wait_entry, void *);
    void (*join)(void *, void *);
    void (*log)(void *);
    void (*wait)(void *, uint32_t);
    void (*wake)(void *);
} drbn_wait_host;
typedef struct { drbn_wait_host host; void *context; void *thread; int running; } drbn_wait_worker;
void drbz_wait_worker_init(drbn_wait_worker *, const drbn_wait_host *, void *);
int drbz_wait_worker_start(drbn_wait_worker *);
int drbz_wait_worker_running(const drbn_wait_worker *);
void drbz_wait_worker_stop(drbn_wait_worker *);
void drbo_wait_worker_init(drbn_wait_worker *, const drbn_wait_host *, void *);
int drbo_wait_worker_start(drbn_wait_worker *);
int drbo_wait_worker_running(const drbn_wait_worker *);
void drbo_wait_worker_stop(drbn_wait_worker *);
#ifdef __cplusplus
}
#endif
#endif
