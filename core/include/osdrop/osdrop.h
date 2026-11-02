/*
 * Osdrop core — public C API.
 *
 * This is the only public header of the shared core library. It is consumed by C++ (Windows
 * engine), Swift (through the OsdropCore clang module) and C# (through P/Invoke), so it stays
 * plain C: opaque handles, POD structs and fixed-width integers only.
 */
#ifndef OSDROP_OSDROP_H
#define OSDROP_OSDROP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single source of truth for the core version; CMake parses these lines. */
#define OSDROP_CORE_VERSION_MAJOR 0
#define OSDROP_CORE_VERSION_MINOR 1
#define OSDROP_CORE_VERSION_PATCH 0

typedef struct osdrop_version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} osdrop_version;

/*
 * Version of the core library that is actually linked. Comparing it with the
 * OSDROP_CORE_VERSION_* macros seen at compile time detects header/library mismatches.
 * Thread-safe; may be called from any thread.
 */
osdrop_version osdrop_core_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSDROP_OSDROP_H */
