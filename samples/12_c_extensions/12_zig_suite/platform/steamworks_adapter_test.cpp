#include "steamworks_adapter.h"
#include <cassert>
#include <cstdio>
int drbn_stub_steam_init_calls,drbn_stub_steam_callback_calls,drbn_stub_steam_shutdown_calls;
int main(){assert(drbn_steam_init()==0);drbn_steam_run_callbacks();drbn_steam_run_callbacks();drbn_steam_shutdown();assert(drbn_stub_steam_init_calls==1&&drbn_stub_steam_callback_calls==2&&drbn_stub_steam_shutdown_calls==1);std::puts("STEAM_ADAPTER_PROOF {\"test_stub_only\":true,\"init\":1,\"callbacks\":2,\"shutdown\":1,\"real_sdk_runtime_pending\":true}");}
