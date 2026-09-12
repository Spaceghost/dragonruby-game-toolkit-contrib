#include "apple_main.h"
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <pthread.h>
int drbn_apple_is_main_thread(void){return pthread_main_np()!=0;}void drbn_apple_dispatch_main(drbn_main_fn fn,void *ctx){dispatch_async_f(dispatch_get_main_queue(),ctx,fn);}
#else
int drbn_apple_is_main_thread(void){return 0;}void drbn_apple_dispatch_main(drbn_main_fn fn,void *ctx){(void)fn;(void)ctx;}
#endif
