#include "apple_main.h"
#include <assert.h>
#include <stdio.h>
int main(void){
#ifdef __APPLE__
assert(drbn_apple_is_main_thread());puts("APPLE_MAIN_PROOF {\"main_thread\":true,\"dispatch_api_linked\":true,\"callback_execution_requires_runloop\":true}");
#else
puts("APPLE_MAIN_PROOF {\"supported\":false}");
#endif
return 0;}
