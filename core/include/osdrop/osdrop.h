/*
 * Osdrop core — public C API.
 *
 * This is the only public header of the shared core library. It is consumed by C++ (Windows
 * engine), Swift (through the OsdropCore clang module) and C# (through P/Invoke), so it stays
 * plain C: opaque handles, POD structs and fixed-width integers only.
 */
#ifndef OSDROP_OSDROP_H
#define OSDROP_OSDROP_H

#include <stddef.h>
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

/* Wire protocol constants (docs/PROTOCOL.md). */
#define OSDROP_PROTOCOL_VERSION 1
#define OSDROP_PROTOCOL_MINOR 0
#define OSDROP_MAX_CONTROL_MESSAGE_SIZE 65536
#define OSDROP_MAX_TRANSFER_MESSAGE_SIZE 65536
#define OSDROP_MAX_INPUT_PLAINTEXT_SIZE 1184
#define OSDROP_DEVICE_ID_SIZE 16
#define OSDROP_GROUP_ID_SIZE 16
#define OSDROP_KEY_SIZE 32

/* Capabilities feature bits (PROTOCOL.md "Capabilities"). */
#define OSDROP_FEATURE_TAKES_CONTROL 0x01u
#define OSDROP_FEATURE_DRAG_SOURCE 0x02u
#define OSDROP_FEATURE_DRAG_TARGET 0x04u
#define OSDROP_FEATURE_CLIPBOARD 0x08u
#define OSDROP_FEATURE_FILES 0x10u

/*
 * Plain C enums: this header is consumed from C, Swift and C#. Each ends with a *_MAX_ENUM value
 * so the C++ side can receive any 32-bit value from a binding without undefined behaviour; it is
 * never a valid argument or result.
 */
/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */

/*
 * Result of every core call. Values are stable and documented in docs/PROTOCOL.md ("Error codes");
 * new codes are only ever appended. Platforms map them to localized messages; core never produces
 * user-facing text.
 */
typedef enum osdrop_result {
    OSDROP_OK = 0,
    OSDROP_ERROR_INVALID_ARGUMENT = 1,
    OSDROP_ERROR_BUFFER_TOO_SMALL = 2,
    OSDROP_ERROR_TRUNCATED = 3,
    OSDROP_ERROR_BAD_MAGIC = 4,
    OSDROP_ERROR_UNSUPPORTED_VERSION = 5,
    OSDROP_ERROR_UNKNOWN_TYPE = 6,
    OSDROP_ERROR_TRAILING_BYTES = 7,
    OSDROP_ERROR_VALUE_OUT_OF_RANGE = 8,
    OSDROP_ERROR_INVALID_STRING = 9,
    OSDROP_ERROR_TOO_LARGE = 10,
    OSDROP_ERROR_DECRYPTION_FAILED = 11,
    OSDROP_ERROR_HANDSHAKE_OUT_OF_ORDER = 12,
    OSDROP_ERROR_REPLAYED = 13,
    OSDROP_ERROR_NONCE_EXHAUSTED = 14,
    OSDROP_ERROR_INVALID_KEY = 15,
    OSDROP_ERROR_RANDOM_UNAVAILABLE = 16,
    OSDROP_ERROR_TIMED_OUT = 17,
    OSDROP_ERROR_CONNECTION_CLOSED = 18,
    OSDROP_ERROR_PEER_ERROR = 19,
    OSDROP_ERROR_UNEXPECTED_MESSAGE = 20,
    OSDROP_ERROR_NOT_TRUSTED = 21,
    OSDROP_ERROR_PAIRING_REJECTED = 22,
    OSDROP_ERROR_PEER_BUSY = 23,
    OSDROP_ERROR_INVALID_STATE = 24,
    OSDROP_ERROR_NETWORK = 25,
    OSDROP_ERROR_INTERNAL = 26,
    OSDROP_ERROR_PEER_SLEEPING = 27,
    OSDROP_ERROR_NOT_PERMITTED = 28,
    OSDROP_ERROR_GROUP_FULL = 29, /* the group already has the most computers allowed */
    OSDROP_ERROR_IN_ANOTHER_GROUP =
        30,                           /* the peer belongs to another group; one must leave first */
    OSDROP_ERROR_INVALID_LAYOUT = 31, /* monitors overlap, or a computer touches no other */
    OSDROP_ERROR_CANCELLED = 32,        /* a transfer was cancelled on either side */
    OSDROP_ERROR_IO = 33,               /* a file could not be read or written */
    OSDROP_ERROR_NO_SPACE = 34,         /* not enough free space for a transfer */
    OSDROP_ERROR_INTEGRITY_FAILED = 35, /* received content does not match the sender's digest */
    OSDROP_ERROR_NOT_FOUND = 36,        /* the offered item or file no longer exists */
    OSDROP_RESULT_MAX_ENUM = 0x7FFFFFFF
} osdrop_result;

typedef enum osdrop_wire_kind {
    OSDROP_WIRE_CONTROL_MESSAGE = 1,  /* one control message, without the TCP length prefix */
    OSDROP_WIRE_INPUT_DATAGRAM = 2,   /* one plaintext input datagram, without the AEAD tag */
    OSDROP_WIRE_TRANSFER_MESSAGE = 3, /* one transfer message, without the TCP length prefix */
    OSDROP_WIRE_KIND_MAX_ENUM = 0x7FFFFFFF
} osdrop_wire_kind;

typedef enum osdrop_platform {
    OSDROP_PLATFORM_MACOS = 1,
    OSDROP_PLATFORM_WINDOWS = 2,
    OSDROP_PLATFORM_MAX_ENUM = 0x7FFFFFFF
} osdrop_platform;

typedef enum osdrop_log_level {
    OSDROP_LOG_DEBUG = 1,
    OSDROP_LOG_INFO = 2,
    OSDROP_LOG_WARNING = 3,
    OSDROP_LOG_ERROR = 4,
    OSDROP_LOG_LEVEL_MAX_ENUM = 0x7FFFFFFF
} osdrop_log_level;

/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

/*
 * Cryptographically secure random bytes from the platform (SecRandomCopyBytes on macOS,
 * BCryptGenRandom on Windows). `fill` writes exactly `length` bytes and returns OSDROP_OK, or
 * returns any other code to report failure. It may be called from any thread.
 */
typedef struct osdrop_random_source {
    osdrop_result (*fill)(void* context, uint8_t* buffer, size_t length);
    void* context;
} osdrop_random_source;

/*
 * Version of the core library that is actually linked. Comparing it with the
 * OSDROP_CORE_VERSION_* macros seen at compile time detects header/library mismatches.
 * Thread-safe; may be called from any thread.
 */
osdrop_version osdrop_core_version(void);

/*
 * Protocol diagnostics. Both functions are pure and thread-safe; `data` may be NULL only when
 * `length` is 0.
 *
 * osdrop_protocol_validate decodes one message or datagram with every check the session applies
 * and writes its type (the PROTOCOL.md type value) to `out_type`.
 */
osdrop_result osdrop_protocol_validate(osdrop_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out_type);

/*
 * Decodes and re-encodes. Encoding is canonical, so a valid input comes back byte for byte.
 * Writes the encoded size to `out_length`; fails with OSDROP_ERROR_BUFFER_TOO_SMALL if `capacity`
 * is not enough.
 */
osdrop_result osdrop_protocol_reencode(osdrop_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out, size_t capacity, size_t* out_length);

/*
 * Device identity: a static X25519 key pair. Platforms store the 32-byte secret (Keychain, DPAPI);
 * core never persists it. All key buffers are OSDROP_KEY_SIZE bytes. Pure and thread-safe.
 *
 * osdrop_identity_generate creates a new key pair from `random`.
 */
osdrop_result osdrop_identity_generate(const osdrop_random_source* random, uint8_t* secret_key,
                                       uint8_t* public_key);

/* Imports a stored secret: rejects an all-zero one and derives its public key. */
osdrop_result osdrop_identity_public_key(const uint8_t* secret_key, uint8_t* public_key);

/* The device ID (OSDROP_DEVICE_ID_SIZE bytes, PROTOCOL.md "Conventions") of a public key. */
osdrop_result osdrop_identity_device_id(const uint8_t* public_key, uint8_t* device_id);

/*
 * Discovery (PROTOCOL.md "Discovery"): the Bonjour TXT record, so both platforms' DNS-SD code
 * shares one format and one set of checks. Pure and thread-safe.
 */
#define OSDROP_SERVICE_TYPE "_osdrop._tcp"
#define OSDROP_TXT_KEY_PROTOCOL_VERSION "pv"
#define OSDROP_TXT_KEY_PROTOCOL_MINOR "pm"
#define OSDROP_TXT_KEY_DEVICE_ID "id"
#define OSDROP_TXT_KEY_NAME "name"
#define OSDROP_TXT_KEY_OS "os"
#define OSDROP_TXT_KEY_GROUP_ID "grp"
#define OSDROP_TXT_VALUE_SIZE 65 /* the longest value (the 64-byte name) and its NUL */

typedef struct osdrop_advertisement {
    uint32_t protocol_version; /* OSDROP_PROTOCOL_VERSION when advertising */
    osdrop_platform platform;
    uint8_t protocol_minor; /* OSDROP_PROTOCOL_MINOR when advertising */
    uint8_t has_group;      /* 1 when group_id is set, 0 when the computer is in no group */
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    uint8_t group_id[OSDROP_GROUP_ID_SIZE]; /* never all zeros when has_group is 1 */
    char name[OSDROP_TXT_VALUE_SIZE];       /* NUL-terminated UTF-8, 1 to 64 bytes */
} osdrop_advertisement;

/* NUL-terminated TXT values; a key missing from a peer's record is an empty string. */
typedef struct osdrop_txt_record {
    char protocol_version[OSDROP_TXT_VALUE_SIZE];
    char protocol_minor[OSDROP_TXT_VALUE_SIZE];
    char device_id[OSDROP_TXT_VALUE_SIZE];
    char name[OSDROP_TXT_VALUE_SIZE];
    char os[OSDROP_TXT_VALUE_SIZE];
    char group_id[OSDROP_TXT_VALUE_SIZE];
} osdrop_txt_record;

osdrop_result osdrop_advertisement_to_txt(const osdrop_advertisement* advertisement,
                                          osdrop_txt_record* out_record);

/* A peer's record. A different protocol version is returned, not refused, so the UI can tell the
 * user to update the other computer. */
osdrop_result osdrop_advertisement_from_txt(const osdrop_txt_record* record,
                                            osdrop_advertisement* out_advertisement);

/*
 * The mesh and the group (PROTOCOL.md "Session flow" and "Group membership"): one encrypted
 * session with every member of this computer's group, on an I/O thread owned by core. Core decides
 * who is trusted from the group state the platform stores for it. Callbacks run on that thread;
 * they must return quickly and must not call osdrop_mesh_* functions themselves (post to another
 * queue instead). Every other function may be called from any thread.
 */
typedef struct osdrop_mesh osdrop_mesh;

/* A peer as its Hello and handshake describe it. */
typedef struct osdrop_peer {
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    uint8_t public_key[OSDROP_KEY_SIZE];
    osdrop_platform platform;
    uint8_t protocol_minor;
    uint8_t has_group;
    uint8_t group_id[OSDROP_GROUP_ID_SIZE];
    char name[OSDROP_TXT_VALUE_SIZE]; /* NUL-terminated UTF-8 */
} osdrop_peer;

/* What this computer accepts from one member; each field is 0 or 1. New members get all three. */
typedef struct osdrop_permissions {
    uint8_t control;   /* may control this computer */
    uint8_t files;     /* may send files to it */
    uint8_t clipboard; /* may share clipboards with it, both ways */
} osdrop_permissions;

typedef struct osdrop_member {
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    uint8_t public_key[OSDROP_KEY_SIZE];
    osdrop_platform platform;
    char name[OSDROP_TXT_VALUE_SIZE]; /* NUL-terminated UTF-8, as it was when it joined */
    osdrop_permissions permissions;
    uint8_t self;      /* 1 for this computer */
    uint8_t connected; /* 1 while a session with it is established */
} osdrop_member;

typedef struct osdrop_session_stats {
    uint32_t rtt_us;
    int64_t clock_offset_us; /* the peer's clock minus this computer's */
    uint64_t datagrams_rejected;
    uint64_t stale_motion_dropped;
} osdrop_session_stats;

/* Every callback may be NULL; `context` is passed back unchanged. */
typedef struct osdrop_mesh_callbacks {
    void* context;
    /*
     * The group state changed: store the `size` bytes at `state` in the secure store (Keychain,
     * DPAPI) and pass them to osdrop_mesh_create next time. `group_id` is the group to advertise
     * in the TXT record, NULL when in no group. Both pointers are valid during the call only.
     */
    void (*group_changed)(void* context, const uint8_t* state, size_t size,
                          const uint8_t* group_id);
    /* No longer in the group: OSDROP_OK when this computer left, NOT_TRUSTED when a member removed
     * it. */
    void (*left_group)(void* context, osdrop_result reason);
    /*
     * The arrangement or a member's screens changed: store the `size` bytes at `state` with the
     * settings (UserDefaults, JSON) and pass them to osdrop_mesh_create next time; read the new
     * arrangement with osdrop_mesh_layout. Valid during the call only.
     */
    void (*layout_changed)(void* context, const uint8_t* state, size_t size);
    /* A first pairing: show `code` (000000-999999); answer with osdrop_mesh_confirm_pairing. */
    void (*pairing_code)(void* context, uint64_t connection, uint32_t code,
                         const osdrop_peer* peer);
    void (*pairing_failed)(void* context, uint64_t connection, osdrop_result reason);
    /* A member's session is established; `features` is what it offers this computer. */
    void (*peer_connected)(void* context, const osdrop_peer* peer, uint32_t features);
    /* `reason` is OSDROP_OK for an orderly end. */
    void (*peer_disconnected)(void* context, const uint8_t* device_id, osdrop_result reason);
    /* 0: silent for 750 ms, input to and from it stops; 1: heard from again. */
    void (*peer_responsive)(void* context, const uint8_t* device_id, uint8_t responsive);
    void (*stats)(void* context, const uint8_t* device_id, const osdrop_session_stats* stats);
    /* One line, NUL-terminated; never contains keys, keystrokes or file contents. */
    void (*log)(void* context, osdrop_log_level level, const char* message);
} osdrop_mesh_callbacks;

typedef struct osdrop_mesh_config {
    const uint8_t* secret_key; /* OSDROP_KEY_SIZE bytes, from osdrop_identity_generate */
    const char* device_name;   /* NUL-terminated UTF-8, 1 to 64 bytes */
    osdrop_platform platform;
    const uint8_t* group_state; /* the bytes last given to group_changed; NULL the first time */
    size_t group_state_size;
    const uint8_t* layout_state; /* the bytes last given to layout_changed; NULL the first time */
    size_t layout_state_size;
    uint32_t features;            /* Capabilities bits (PROTOCOL.md) */
    uint8_t max_transfer_streams; /* 1 to 8 */
    uint16_t listen_port;         /* 0: any; a taken port falls back to any */
} osdrop_mesh_config;

/*
 * Starts listening and the I/O thread. `random` and `callbacks` are copied; their contexts must
 * stay valid until osdrop_mesh_destroy returns. Fails with UNSUPPORTED_VERSION for a group state
 * written by a newer Osdrop (keep it; do not overwrite it) and with another decoding error for a
 * damaged one.
 */
osdrop_result osdrop_mesh_create(const osdrop_mesh_config* config,
                                 const osdrop_random_source* random,
                                 const osdrop_mesh_callbacks* callbacks, osdrop_mesh** out_mesh);

/* Ends every session (Bye: application quit) and joins the I/O thread; NULL is ignored. */
void osdrop_mesh_destroy(osdrop_mesh* mesh);

osdrop_result osdrop_mesh_listen_port(const osdrop_mesh* mesh, uint16_t* out_port);
osdrop_result osdrop_mesh_device_id(const osdrop_mesh* mesh, uint8_t* out_device_id);
/* The group to advertise; `*out_has_group` is 0 when in no group. */
osdrop_result osdrop_mesh_group_id(osdrop_mesh* mesh, uint8_t* out_group_id,
                                   uint8_t* out_has_group);

/* From discovery: a computer advertising `group_id` (NULL: none) at `host`:`port`. */
osdrop_result osdrop_mesh_peer_discovered(osdrop_mesh* mesh, const uint8_t* device_id,
                                          const uint8_t* group_id, const char* host, uint16_t port);
osdrop_result osdrop_mesh_peer_lost(osdrop_mesh* mesh, const uint8_t* device_id);

/*
 * Dials `host`:`port` to pair; the connection identifies later callbacks. The computer in a group
 * adds the other to it; when neither is in one, they create one. Computers in different groups
 * are refused (IN_ANOTHER_GROUP), as is a full group (GROUP_FULL).
 */
osdrop_result osdrop_mesh_pair(osdrop_mesh* mesh, const char* host, uint16_t port,
                               uint64_t* out_connection);
osdrop_result osdrop_mesh_confirm_pairing(osdrop_mesh* mesh, uint64_t connection, uint8_t accepted);

/*
 * The group's members, this computer included, into `out` (room for `capacity`). `*out_count` is
 * the number of members; BUFFER_TOO_SMALL when it exceeds `capacity`.
 */
osdrop_result osdrop_mesh_members(osdrop_mesh* mesh, osdrop_member* out, size_t capacity,
                                  size_t* out_count);
/*
 * Group commands. They fail with INVALID_STATE when this computer is in no group and with
 * INVALID_ARGUMENT for this computer itself or a device that is not a member.
 *
 * osdrop_mesh_revoke removes a member from the group everywhere; it is told it was removed.
 */
osdrop_result osdrop_mesh_revoke(osdrop_mesh* mesh, const uint8_t* device_id);
/* Leaves the group: members learn it, sessions end, then group_changed and left_group. */
osdrop_result osdrop_mesh_leave_group(osdrop_mesh* mesh);
/* What this computer accepts from one member; the member is told at once. */
osdrop_result osdrop_mesh_set_permissions(osdrop_mesh* mesh, const uint8_t* device_id,
                                          const osdrop_permissions* permissions);
/* Ends every session before sleep (peers see PEER_SLEEPING) or quitting. */
osdrop_result osdrop_mesh_close_all(osdrop_mesh* mesh, uint8_t sleeping);

/*
 * The shared arrangement (PROTOCOL.md "Arrangement"). Coordinates are int32 in 1/16 of a unit:
 * logical (macOS points, Windows device-independent pixels) or native, the units the computer's
 * own cursor moves in (macOS points, Windows physical pixels).
 */
#define OSDROP_MONITOR_KEY_SIZE 16
#define OSDROP_MAX_MONITORS 16

typedef struct osdrop_rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} osdrop_rect;

typedef struct osdrop_monitor {
    uint8_t key[OSDROP_MONITOR_KEY_SIZE]; /* stable identity of the monitor on its computer */
    osdrop_rect frame;                    /* logical, in the computer's own desktop */
    osdrop_rect native;                   /* native; on macOS the same as `frame` */
    uint16_t pixel_width;
    uint16_t pixel_height;
    uint8_t primary; /* exactly one monitor of a computer is primary */
} osdrop_monitor;

/* Where a computer's logical origin sits in the arrangement. */
typedef struct osdrop_placement {
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    int32_t x;
    int32_t y;
    uint8_t detached; /* 1: kept out of crossings */
} osdrop_placement;

/* A computer of the arrangement with its monitors. */
typedef struct osdrop_layout_computer {
    uint8_t placed; /* 0: not in the arrangement yet; `placement` then holds only the device ID */
    osdrop_placement placement;
    uint8_t monitor_count; /* 1 to OSDROP_MAX_MONITORS */
    osdrop_monitor monitors[OSDROP_MAX_MONITORS];
} osdrop_layout_computer;

/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */
typedef enum osdrop_layout_problem_kind {
    OSDROP_LAYOUT_OVERLAP = 1,     /* the monitors of two computers overlap */
    OSDROP_LAYOUT_UNREACHABLE = 2, /* a computer touches no other and is not detached */
    OSDROP_LAYOUT_PROBLEM_MAX_ENUM = 0x7FFFFFFF
} osdrop_layout_problem_kind;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

typedef struct osdrop_layout_problem {
    osdrop_layout_problem_kind kind;
    uint8_t device_id[OSDROP_DEVICE_ID_SIZE];
    uint8_t other_device_id[OSDROP_DEVICE_ID_SIZE]; /* zeros for UNREACHABLE */
} osdrop_layout_problem;

/*
 * This computer's monitors, at start and after every display change. VALUE_OUT_OF_RANGE for
 * monitors a ScreenReport cannot carry (sizes, coordinates, not exactly one primary).
 */
osdrop_result osdrop_mesh_set_screens(osdrop_mesh* mesh, const osdrop_monitor* monitors,
                                      size_t count);
/*
 * The members whose screens are known, this computer included, into `out` (room for
 * `capacity`). `*out_count` is their number; BUFFER_TOO_SMALL when it exceeds `capacity`.
 */
osdrop_result osdrop_mesh_layout(osdrop_mesh* mesh, osdrop_layout_computer* out, size_t capacity,
                                 size_t* out_count);
/*
 * A new arrangement from the editor, shared with every member. INVALID_LAYOUT when
 * osdrop_layout_check finds problems with the members' screens; INVALID_ARGUMENT for a computer
 * that is not a member or appears twice.
 */
osdrop_result osdrop_mesh_set_layout(osdrop_mesh* mesh, const osdrop_placement* placements,
                                     size_t count);

/*
 * Pure and thread-safe: the problems of an arrangement, for the editor while the user drags.
 * Computers that are not placed are left out. Writes at most `capacity` problems and their number
 * to `*out_count`; BUFFER_TOO_SMALL when there are more.
 */
osdrop_result osdrop_layout_check(const osdrop_layout_computer* computers, size_t count,
                                  osdrop_layout_problem* out, size_t capacity, size_t* out_count);
/*
 * Pure and thread-safe, for Windows, where monitors with different scales share no logical
 * space: sets each monitor's frame x and y from the native rectangles and the frames' sizes (the
 * logical sizes). Monitors that touch in physical pixels touch in the result.
 */
osdrop_result osdrop_layout_derive_frames(osdrop_monitor* monitors, size_t count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSDROP_OSDROP_H */
