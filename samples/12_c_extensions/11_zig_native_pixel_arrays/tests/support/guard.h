/* A writable page between two inaccessible pages, with OS-enforced bounds. */
#ifndef DRB_ZIG_GUARD_H
#define DRB_ZIG_GUARD_H
#include <assert.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
struct guarded_page { unsigned char *base, *data; size_t size; };
static struct guarded_page guard_create(void) {
    struct guarded_page page;
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    page.size = info.dwPageSize;
    page.base = VirtualAlloc(NULL, 3 * page.size, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    assert(page.base);
    page.data = page.base + page.size;
    DWORD old;
    assert(VirtualProtect(page.data, page.size, PAGE_READWRITE, &old));
#else
    long size = sysconf(_SC_PAGESIZE);
    assert(size > 0);
    page.size = (size_t)size;
    page.base = mmap(NULL, 3 * page.size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page.base != MAP_FAILED);
    page.data = page.base + page.size;
    assert(mprotect(page.data, page.size, PROT_READ | PROT_WRITE) == 0);
#endif
    return page;
}
static void guard_readonly(struct guarded_page page) {
#ifdef _WIN32
    DWORD old;
    assert(VirtualProtect(page.data, page.size, PAGE_READONLY, &old));
#else
    assert(mprotect(page.data, page.size, PROT_READ) == 0);
#endif
}
static void guard_destroy(struct guarded_page page) {
#ifdef _WIN32
    assert(VirtualFree(page.base, 0, MEM_RELEASE));
#else
    assert(munmap(page.base, 3 * page.size) == 0);
#endif
}
#endif
