/*
 * Osdrop Windows engine — public C API consumed by the WPF shell through P/Invoke.
 */
#ifndef OSDROP_ENGINE_H
#define OSDROP_ENGINE_H

#include <osdrop/osdrop.h>

#ifdef OSDROP_ENGINE_BUILDING
#define OSDROP_ENGINE_API __declspec(dllexport)
#else
#define OSDROP_ENGINE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the core library linked into the engine. Thread-safe. */
OSDROP_ENGINE_API osdrop_version osdrop_engine_core_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSDROP_ENGINE_H */
