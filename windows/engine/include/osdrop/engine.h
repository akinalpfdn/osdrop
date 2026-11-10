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
    OSDROP_ENGINE_LAYOUT_CHANGED = 16,        /* the arrangement changed: read it again */
    OSDROP_ENGINE_INPUT_CHANGED = 17,         /* the input state changed: read it again */
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

/* Where this PC's mouse and keyboard are, and whether other computers can control it. */
typedef struct osdrop_engine_input_state {
    uint8_t reachable;  /* 1 while OsdropInput answers; 0: the service is stopped or missing */
    uint8_t locked;     /* "lock input here" is on */
    uint8_t controlled; /* 1 while the member `controller_id` controls this PC */
    uint8_t controller_id[OSDROP_DEVICE_ID_SIZE];
    uint8_t capturing; /* 1 while the cursor is on the member `target_id` */
    uint8_t target_id[OSDROP_DEVICE_ID_SIZE];
    /* How the cursor last came back, as osdrop_mesh_callbacks.capture_ended reports it; 0 while
       nothing is worth telling (cleared by osdrop_engine_dismiss_input_notice). */
    uint8_t notice_reason;
    uint8_t notice_refused;
    uint8_t notice_device_id[OSDROP_DEVICE_ID_SIZE];
} osdrop_engine_input_state;

/* A chord: a key by its scan code, with modifiers held (1 Ctrl, 2 Alt, 4 Shift, 8 Windows; Ctrl,
   Alt or Windows among them). The key must not be a modifier. */
typedef struct osdrop_engine_hotkey {
    uint8_t modifiers;
    uint16_t scan_code;
    uint8_t extended;
} osdrop_engine_hotkey;

/* What the shell chose; read when the engine starts. */
typedef struct osdrop_engine_config {
    const char* device_name; /* NUL-terminated UTF-8; NULL or empty: this PC's own name */
    uint8_t shares_clipboard;
    uint64_t clipboard_files_limit; /* bytes */
    osdrop_input_settings input;
    osdrop_engine_hotkey return_hotkey; /* brings the cursor home */
    osdrop_engine_hotkey lock_hotkey;   /* "lock input here" */
    uint8_t
        respects_cursor_clip; /* 1: no crossing while an application (a game) holds the cursor */
} osdrop_engine_config;

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
OSDROP_ENGINE_API osdrop_result osdrop_engine_create(const osdrop_engine_config* config,
                                                     osdrop_engine_callback callback, void* context,
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
/* The user's clipboard settings. Turned off, this PC's copy is withdrawn and copies are neither
   offered nor fetched. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_set_clipboard(osdrop_engine* engine, uint8_t shared,
                                                            uint64_t files_limit);
/* As osdrop_mesh_set_permissions; GROUP_CHANGED follows. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_set_permissions(
    osdrop_engine* engine, const uint8_t* device_id, const osdrop_permissions* permissions);
/* As osdrop_mesh_layout and osdrop_mesh_set_layout; LAYOUT_CHANGED follows a change. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_layout(osdrop_engine* engine,
                                                     osdrop_layout_computer* out, size_t capacity,
                                                     size_t* out_count);
OSDROP_ENGINE_API osdrop_result osdrop_engine_set_layout(osdrop_engine* engine,
                                                         const osdrop_placement* placements,
                                                         size_t count);
/* As osdrop_mesh_set_input_settings and osdrop_mesh_set_pointer_speed. */
OSDROP_ENGINE_API osdrop_result
osdrop_engine_set_input_settings(osdrop_engine* engine, const osdrop_input_settings* settings);
OSDROP_ENGINE_API osdrop_result osdrop_engine_set_pointer_speed(osdrop_engine* engine,
                                                                const uint8_t* device_id,
                                                                uint32_t permille);
/* The chords; INVALID_ARGUMENT for a chord the hook cannot match, or the same one twice. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_set_hotkeys(osdrop_engine* engine,
                                                          const osdrop_engine_hotkey* return_hotkey,
                                                          const osdrop_engine_hotkey* lock_hotkey);
/* Whether no crossing happens while an application (a game) holds the cursor in a rectangle. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_set_respects_cursor_clip(osdrop_engine* engine,
                                                                       uint8_t respects);
/* Developer tool (debug builds of the shell): a drag dropped at one of the PC's edges comes back as
   an Osdrop drag at the left edge of the same monitor (DEVPLAN Phase 4). Results are LOG events. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_set_drag_loopback(osdrop_engine* engine,
                                                                uint8_t enabled);
/* "Lock input here" (also toggled by the lock hotkey); INPUT_CHANGED follows. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_lock_input(osdrop_engine* engine, uint8_t locked);
OSDROP_ENGINE_API osdrop_result osdrop_engine_get_input_state(osdrop_engine* engine,
                                                              osdrop_engine_input_state* out_state);
/* Clears the input state's notice. */
OSDROP_ENGINE_API osdrop_result osdrop_engine_dismiss_input_notice(osdrop_engine* engine);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSDROP_ENGINE_H */
