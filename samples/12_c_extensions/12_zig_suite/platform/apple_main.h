#ifndef DRBN_APPLE_MAIN_H
#define DRBN_APPLE_MAIN_H
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*drbn_main_fn)(void *);
int drbn_apple_is_main_thread(void);
void drbn_apple_dispatch_main(drbn_main_fn fn, void *context);
#ifdef __cplusplus
}
#endif
#endif
