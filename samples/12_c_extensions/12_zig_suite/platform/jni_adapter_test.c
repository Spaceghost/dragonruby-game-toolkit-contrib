#include "jni_adapter.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
extern jint JNI_OnLoad(JavaVM *,void *);extern void JNI_OnUnload(JavaVM *,void *);
static void *thread_main(void *raw){(void)raw;JNIEnv *env=NULL;int attached=0;assert(drbn_jni_get_env(&env,&attached)==JNI_OK);assert(env&&attached==1);drbn_jni_detach_if_needed(attached);return NULL;}
int main(void){JavaVMOption opt={.optionString=(char *)"-Djava.class.path=."};JavaVMInitArgs args;memset(&args,0,sizeof args);args.version=JNI_VERSION_1_6;args.nOptions=1;args.options=&opt;args.ignoreUnrecognized=JNI_TRUE;JavaVM *vm=NULL;JNIEnv *env=NULL;assert(JNI_CreateJavaVM(&vm,(void **)&env,&args)==JNI_OK);assert(JNI_OnLoad(vm,NULL)==JNI_VERSION_1_6);assert(drbn_jni_vm()==vm);JNIEnv *same=NULL;int attached=-1;assert(drbn_jni_get_env(&same,&attached)==JNI_OK&&same==env&&attached==0);pthread_t t;assert(pthread_create(&t,NULL,thread_main,NULL)==0);assert(pthread_join(t,NULL)==0);JNI_OnUnload(vm,NULL);assert(drbn_jni_vm()==NULL);assert((*vm)->DestroyJavaVM(vm)==JNI_OK);puts("JNI_PROOF {\"real_vm\":true,\"main_already_attached\":true,\"worker_attach_detach\":true,\"onload_onunload_state\":true}");return 0;}
