cmake_minimum_required(VERSION 3.28)
project(OsdropAndroid LANGUAGES C CXX)

# The shared core, built for the Android ABI; the JNI bridge is the only code on top of it.
set(OSDROP_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core core)

add_library(osdrop_jni SHARED osdrop_jni.cpp)
target_compile_features(osdrop_jni PRIVATE cxx_std_20)
target_compile_options(osdrop_jni PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(osdrop_jni PRIVATE osdrop::core android log)
