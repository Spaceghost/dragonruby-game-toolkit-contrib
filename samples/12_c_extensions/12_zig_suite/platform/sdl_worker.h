#ifndef DRBN_SDL_WORKER_H
#define DRBN_SDL_WORKER_H
#include "../src/worker_wait.h"
#include <stdint.h>
typedef struct { void *mutex; void *condition; uint64_t generation,consumed_generation; void (*log)(void *); void *user; } drbn_sdl_wait_context;
int drbn_sdl_wait_context_init(drbn_sdl_wait_context *,void (*log)(void *),void *user);void drbn_sdl_wait_context_destroy(drbn_sdl_wait_context *);drbn_wait_host drbn_sdl_wait_host(void);
#endif
