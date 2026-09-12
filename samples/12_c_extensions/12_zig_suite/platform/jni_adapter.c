#include "jni_adapter.h"
#include <stdatomic.h>
#include <stdint.h>

static _Atomic(uintptr_t) stored_vm;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    atomic_store_explicit(&stored_vm, (uintptr_t)vm, memory_order_release);
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
    (void)reserved;
    uintptr_t expected = (uintptr_t)vm;
    (void)atomic_compare_exchange_strong_explicit(
        &stored_vm,
        &expected,
        0,
        memory_order_acq_rel,
        memory_order_acquire);
}

JavaVM *drbn_jni_vm(void) {
    return (JavaVM *)atomic_load_explicit(&stored_vm, memory_order_acquire);
}

jint drbn_jni_get_env(JNIEnv **env, int *attached_here) {
    JavaVM *vm = drbn_jni_vm();
    if (!vm || !env || !attached_here) return JNI_ERR;

    *env = NULL;
    *attached_here = 0;
    jint rc = (*vm)->GetEnv(vm, (void **)env, JNI_VERSION_1_6);
    if (rc == JNI_OK) return JNI_OK;
    if (rc != JNI_EDETACHED) return rc;

#if defined(__ANDROID__)
    rc = (*vm)->AttachCurrentThread(vm, env, NULL);
#else
    rc = (*vm)->AttachCurrentThread(vm, (void **)env, NULL);
#endif
    if (rc == JNI_OK) {
        *attached_here = 1;
    }
    return rc;
}

void drbn_jni_detach_if_needed(int attached_here) {
    JavaVM *vm = drbn_jni_vm();
    if (attached_here && vm) {
        (void)(*vm)->DetachCurrentThread(vm);
    }
}
