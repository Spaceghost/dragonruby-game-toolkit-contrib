#include "dynlib.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
void *drbn_dynlib_open(const char *path){return(void *)LoadLibraryA(path);}void *drbn_dynlib_symbol(void *lib,const char *name){return(void *)(uintptr_t)GetProcAddress((HMODULE)lib,name);}void drbn_dynlib_close(void *lib){if(lib)FreeLibrary((HMODULE)lib);}
#else
#include <dlfcn.h>
void *drbn_dynlib_open(const char *path){return dlopen(path,RTLD_NOW|RTLD_LOCAL);}void *drbn_dynlib_symbol(void *lib,const char *name){return dlsym(lib,name);}void drbn_dynlib_close(void *lib){if(lib)dlclose(lib);}
#endif
