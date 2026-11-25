#include <jni.h>
#include <android/log.h>
#include <string>

#if !defined(__aarch64__) || !defined(__ANDROID__)
#error HulkPocket native bridge must be compiled for Android ARM64
#endif

extern "C" JNIEXPORT jstring JNICALL
Java_com_hulkpocket_diagnostic_MainActivity_nativeBridgeStatus(JNIEnv* env, jclass) {
    const std::string status =
        "Ponte JNI ARM64 carregada (Android).\n"
        "Motor Thunderclap: NAO integrado.\n"
        "Jogo nativo: NAO inicializado.";
    __android_log_print(ANDROID_LOG_INFO, "HulkPocket", "%s", status.c_str());
    return env->NewStringUTF(status.c_str());
}
