#include "steamworks_adapter.h"
#include <steam/steam_api.h>
extern "C" int drbn_steam_init(void){return SteamAPI_Init()?0:1;}extern "C" void drbn_steam_run_callbacks(void){SteamAPI_RunCallbacks();}extern "C" void drbn_steam_shutdown(void){SteamAPI_Shutdown();}
