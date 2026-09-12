#ifndef DRBN_TEST_STEAM_API_H
#define DRBN_TEST_STEAM_API_H
extern int drbn_stub_steam_init_calls,drbn_stub_steam_callback_calls,drbn_stub_steam_shutdown_calls;
static inline bool SteamAPI_Init(void){++drbn_stub_steam_init_calls;return true;}static inline void SteamAPI_RunCallbacks(void){++drbn_stub_steam_callback_calls;}static inline void SteamAPI_Shutdown(void){++drbn_stub_steam_shutdown_calls;}
#endif
