#include "sdl_worker.h"
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#ifndef W_INIT
#define W_INIT drbz_wait_worker_init
#define W_START drbz_wait_worker_start
#define W_RUNNING drbz_wait_worker_running
#define W_STOP drbz_wait_worker_stop
#define W_LANGUAGE "zig"
#endif
struct meter{atomic_uint logs;};static void counted(void *raw){atomic_fetch_add_explicit(&((struct meter *)raw)->logs,1,memory_order_relaxed);}static double ms(uint64_t a,uint64_t b){return(double)(b-a)*1000.0/(double)SDL_GetPerformanceFrequency();}
int main(void){assert(SDL_Init(SDL_INIT_TIMER)==0);struct meter m;atomic_init(&m.logs,0);drbn_sdl_wait_context c;assert(drbn_sdl_wait_context_init(&c,counted,&m)==0);drbn_wait_host host=drbn_sdl_wait_host();drbn_wait_worker w;W_INIT(&w,&host,&c);double worst=0;for(unsigned i=0;i<100;++i){assert(W_START(&w)==0);assert(W_RUNNING(&w)==1);if(!(i&1))SDL_Delay(2);uint64_t a=SDL_GetPerformanceCounter();W_STOP(&w);uint64_t b=SDL_GetPerformanceCounter();double elapsed=ms(a,b);if(elapsed>worst)worst=elapsed;assert(W_RUNNING(&w)==0);assert(elapsed<250.0);}assert(atomic_load_explicit(&m.logs,memory_order_relaxed)>0);drbn_sdl_wait_context_destroy(&c);SDL_Quit();printf("SDL_WAIT_PROOF {\"backend\":\"%s\",\"lifetimes\":100,\"worst_stop_ms\":%.3f,\"lost_wake\":false}\n",W_LANGUAGE,worst);return 0;}
