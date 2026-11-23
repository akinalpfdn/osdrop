package com.akinalpfdn.osdrop.core

/** The shared core, through the JNI bridge in `src/main/cpp`. */
object NativeCore {
    init {
        System.loadLibrary("osdrop_jni")
    }

    /** The linked core's version, "major.minor.patch". */
    @JvmStatic
    external fun coreVersion(): String
}
