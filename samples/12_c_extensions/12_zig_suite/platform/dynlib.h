#ifndef DRBN_DYNLIB_H
#define DRBN_DYNLIB_H
#ifdef __cplusplus
extern "C" {
#endif
void *drbn_dynlib_open(const char *path);
void *drbn_dynlib_symbol(void *library, const char *name);
void drbn_dynlib_close(void *library);
#ifdef __cplusplus
}
#endif
#endif
