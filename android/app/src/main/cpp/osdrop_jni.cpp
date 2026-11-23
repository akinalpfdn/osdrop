// The JNI bridge between the Kotlin app and core's C API (docs/ARCHITECTURE.md, "Android").
#include <osdrop/osdrop.h>

#include <jni.h>

#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_com_akinalpfdn_osdrop_core_NativeCore_coreVersion(JNIEnv* env, jclass /*unused*/) {
    const osdrop_version version = osdrop_core_version();
    const std::string text = std::to_string(version.major) + "." + std::to_string(version.minor) +
                             "." + std::to_string(version.patch);
    return env->NewStringUTF(text.c_str());
}
