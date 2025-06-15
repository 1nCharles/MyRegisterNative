#include <jni.h>
#include <string>
#include <android/log.h>
#include "fake_dlfcn.h"

int android_api = 0;
static jfieldID field_art_method = nullptr;

inline static bool IsIndexId(jmethodID mid) {
    return ((reinterpret_cast<uintptr_t>(mid) % 2) != 0);
}

static void InitArt(JNIEnv *env) {
    if (android_api >= __ANDROID_API_R__) {
        if (field_art_method != nullptr) {
            return;
        }
        jclass clazz = env->FindClass("java/lang/reflect/Executable");
        field_art_method = env->GetFieldID(clazz, "artMethod", "J");
    }
}

static void *GetArtMethod(JNIEnv *env, jclass clazz, jmethodID methodId) {
    if (__predict_false(methodId == nullptr) || __predict_false(env == nullptr)) {
        return nullptr;
    }
    if (android_api >= __ANDROID_API_R__) {
        if (IsIndexId(methodId)) {
            jobject method = env->ToReflectedMethod(clazz, methodId, true);
            if (!method) {
                return nullptr;
            }
            return reinterpret_cast<void *>(env->GetLongField(method, field_art_method));
        }
    }
    return methodId;
}

jstring strrr(JNIEnv *env, jobject thiz) {
    std::string hello = "rrrrregistered";
    return env->NewStringUTF(hello.c_str());
}

bool MyRegisterNative(JNIEnv *env, char* className, char* methodName, char* sign,uintptr_t funcptr) {
    jclass clazz = env->FindClass(className);
    jmethodID mid = env->GetMethodID(clazz,methodName,sign);
    uintptr_t *artMethod = static_cast<uintptr_t *>(GetArtMethod(env, clazz, mid));
    uint64_t *start = reinterpret_cast<uint64_t *>(artMethod);
    void *addr = FindSymbolInSymtab("/apex/com.android.art/lib64/libart.so", "art_jni_dlsym_lookup_stub");
    int jni_offset=0;
    for (int i = 0; i < 30; ++i) {
        if (reinterpret_cast<void *>(artMethod[i]) == addr) {
            jni_offset = i;
            __android_log_print(
                    ANDROID_LOG_INFO,
                    "JNI_DEBUG",
                    "found art method entrypoint jni offset: %d",
                    i
            );
        }
    }
    start[jni_offset] = reinterpret_cast<uintptr_t>(funcptr);
    return true;
}



JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    InitArt(env);
    MyRegisterNative(env, "com/britney/myregisternative/MainActivity", "str1", "()Ljava/lang/String;", reinterpret_cast<uintptr_t>(strrr));
    return JNI_VERSION_1_6;
}


static void __attribute__((constructor())) init() {
    char api_level_str[5];
    __system_property_get("ro.build.version.sdk", api_level_str);
    android_api = atoi(api_level_str);
}


