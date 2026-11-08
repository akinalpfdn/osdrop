/*
 * Osdrop Windows engine — public C API consumed by the WPF shell through P/Invoke.
 *
 * The engine runs this PC's mesh and group (core), finds other computers over DNS-SD, stores the
 * identity and group with DPAPI under %APPDATA%\Osdrop, and reports this PC's monitors.
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

typedef struct osdrop_engine osdrop_engine;

/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */
typedef enum osdrop_engine_event_kind {
    OSDROP_ENGINE_GROUP_CHANGED = 1,     /* the group or a member changed: read members again */
    OSDROP_ENGINE_NEARBY_CHANGED = 2,    /* the nearby computers changed: read them again */
    OSDROP_ENGINE_LEFT_GROUP = 3,        /* reason: OK when this PC left, NOT_TRUSTED if removed */
    OSDROP_ENGINE_PAIRING_CODE = 4,      /* connection, code, device_id, name */
    OSDROP_ENGINE_PAIRING_FAILED = 5,    /* connection, reason */
    OSDROP_ENGINE_PEER_CONNECTED = 6,    /* device_id, name */
    OSDROP_ENGINE_PEER_DISCONNECTED = 7, /* device_id, reason (OK for an orderly end) */
    OSDROP_ENGINE_PEER_RESPONSIVE = 8,   /* device_id, responsive */
    OSDROP_ENGINE_STATS = 9,             /* device_id, stats */
    OSDROP_ENGINE_LOG = 10,              /* level, message */
    OSDROP_ENGINE_CLIPBOARD_UNAVAILABLE = 11, /* device_id, name: its copy could not be fetched */
    OSDROP_ENGINE_CLIPBOARD_TOO_LARGE = 12,   /* device_id, name: its copy is over the limits */
    OSDROP_ENGINE_CLIPBOARD_FETCHING = 13,    /* device_id, name, bytes_done, bytes_total: its
                                                 copied files on their way, as they come */
    OSDROP_ENGINE_CLIPBOARD_FETCH_ENDED = 14, /* copied files are no longer on their way */
    OSDROP_ENGINE_CLIPBOARD_READY = 15,       /* device_id, name: its copied files can be pasted */
    OSDROP_ENGINE_EVENT_KIND_MAX_ENUM = 0x7FFFFFFF
} osdrop_engine_event_kind;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

/* One event; fields not listed for its kind are zero. */
typedef struct osdrop_engine_event {
    osdrop_engine_event_kind kind;
    osdrop_result reason;
    osdrop_log_level level;
    uint32_t code; /* the pairing code, 000000-999999 */
    uint64_t connection;
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    uint8_t responsive;
    char name[OSDROP_TXT_VALUE_SIZE]; /* NUL-terminated UTF-8 */
    osdrop_session_stats stats;
    const char* message; /* LOG: NUL-terminated UTF-8, valid during the call; NULL otherwise */
    uint64_t bytes_done;
    uint64_t bytes_total;
} osdrop_engine_event;

/*
 * Runs on the engine's and the system's threads: return quickly and do not call the engine from
 * inside it (post to the UI or a worker instead).
 */
typedef void (*osdrop_engine_callback)(void* context, const osdrop_engine_event* event);

typedef struct osdrop_engine_info {
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    uint8_t group_id[OSDROP_GROUP_ID_SIZE];
    uint8_t has_group;
    uint16_t listen_port;
} osdrop_engine_info;

/* A computer nearby that is not a member of this PC's group. */
typedef struct osdrop_nearby_computer {
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    osdrop_platform platform;
    uint8_t in_another_group; /* pairing is refused until one of the two leaves its group */
    uint8_t needs_update;     /* another protocol version */
    char name[OSDROP_TXT_VALUE_SIZE];
} osdrop_nearby_computer;

/*
 * Starts the engine: loads or creates the identity, starts the mesh and DNS-SD, and reports this
 * PC's monitors. UNSUPPORTED_VERSION: the stored group was written by a newer Osdrop (it is kept).
 * `context` must stay valid until osdrop_engine_destroy returns.
 */
OSDROP_ENGINE_API osdrop_result osdrop_engine_create(osdrop_engine_callback callback, void* context,
                                                     osdrop_engine** out_engine);
/* Stops DNS-SD and ends every session (Bye: application quit); NULL is ignored. */
OSDROP_ENGINE_API void osdrop_engine_destroy(osdrop_engine* engine);

/* Everything else may be called from any thread except from inside the callback. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_get_info(osdrop_engine* engine,
                                                       osdrop_engine_info* out_info);
/* As osdrop_mesh_members. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_members(osdrop_engine* engine, osdrop_member* out,
                                                      size_t capacity, size_t* out_count);
/* `*out_count` nearby computers; BUFFER_TOO_SMALL when they do not fit in `capacity`. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_nearby(osdrop_engine* engine,
                                                     osdrop_nearby_computer* out, size_t capacity,
                                                     size_t* out_count);
/* Pairs with a nearby computer; the connection identifies the pairing's events. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_pair(osdrop_engine* engine, const uint8_t* device_id,
                                                   uint64_t* out_connection);
OSDROP_ENGINE_API osdrop_result osdrop_engine_confirm_pairing(osdrop_engine* engine,
                                                              uint64_t connection,
                                                              uint8_t accepted);
OSDROP_ENGINE_API osdrop_result osdrop_engine_revoke(osdrop_engine* engine,
                                                     const uint8_t* device_id);
/* As osdrop_mesh_block, osdrop_mesh_unblock and osdrop_mesh_blocked; GROUP_CHANGED follows. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_block(osdrop_engine* engine,
                                                    const uint8_t* device_id);
OSDROP_ENGINE_API osdrop_result osdrop_engine_unblock(osdrop_engine* engine,
                                                      const uint8_t* device_id);
OSDROP_ENGINE_API osdrop_result osdrop_engine_blocked(osdrop_engine* engine,
                                                      osdrop_blocked_device* out, size_t capacity,
                                                      size_t* out_count);
OSDROP_ENGINE_API osdrop_result osdrop_engine_leave_group(osdrop_engine* engine);
/* The displays changed: the engine reads and reports them again. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_screens_changed(osdrop_engine* engine);
/* Before sleep (peers see PEER_SLEEPING) or quitting. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_close_all(osdrop_engine* engine, uint8_t sleeping);
/* After waking: browses again, so members are found and dialled again. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_resume(osdrop_engine* engine);
/* Stops fetching copied files from another computer; the clipboard keeps what it had. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_cancel_clipboard_fetch(osdrop_engine* engine);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSDROP_ENGINE_H */
