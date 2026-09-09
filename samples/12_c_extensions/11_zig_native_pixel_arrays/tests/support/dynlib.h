#ifndef DRB_ZIG_DYNLIB_H
#define DRB_ZIG_DYNLIB_H
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
typedef HMODULE test_library;
static test_library library_open(const char *path) {
    test_library lib = LoadLibraryA(path);
    if (!lib) fprintf(stderr, "LoadLibrary failed: %lu\n", (unsigned long)GetLastError());
    assert(lib);
    return lib;
}
#define library_symbol(lib, name) GetProcAddress(lib, name)
static void library_close(test_library lib) { assert(FreeLibrary(lib)); }
#else
#include <dlfcn.h>
typedef void *test_library;
static test_library library_open(const char *path) {
    test_library lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) fprintf(stderr, "%s\n", dlerror());
    assert(lib);
    return lib;
}
#define library_symbol(lib, name) dlsym(lib, name)
static void library_close(test_library lib) { assert(dlclose(lib) == 0); }
#endif
#endif
