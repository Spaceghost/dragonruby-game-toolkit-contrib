#ifndef DRBN_JNI_ADAPTER_H
#define DRBN_JNI_ADAPTER_H
#include <jni.h>
#ifdef __cplusplus
extern "C" {
#endif
JavaVM *drbn_jni_vm(void);
jint drbn_jni_get_env(JNIEnv **env, int *attached_here);
void drbn_jni_detach_if_needed(int attached_here);
#ifdef __cplusplus
}
#endif
#endif
