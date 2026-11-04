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
    OSDROP_ERROR_CANCELLED = 32,      /* a transfer was cancelled on either side */
    OSDROP_ERROR_IO = 33,             /* a file could not be read or written */
    OSDROP_ERROR_NO_SPACE = 34,       /* not enough free space for a transfer */
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

/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */
typedef enum osdrop_object_kind {
    OSDROP_OBJECT_DRAG = 1,      /* object_id: the drag ID; item_index: the item in the DragOffer */
    OSDROP_OBJECT_CLIPBOARD = 2, /* object_id: the clip ID; item_index: the format code */
    OSDROP_OBJECT_KIND_MAX_ENUM = 0x7FFFFFFF
} osdrop_object_kind;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

/* One item a transfer moves (PROTOCOL.md "Transfer streams"). */
typedef struct osdrop_object_ref {
    osdrop_object_kind kind;
    uint64_t object_id; /* not 0 */
    uint32_t item_index;
} osdrop_object_ref;

/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */
typedef enum osdrop_clipboard_format {
    OSDROP_CLIPBOARD_TEXT = 1,  /* plain text, UTF-8 */
    OSDROP_CLIPBOARD_RTF = 2,   /* rich text */
    OSDROP_CLIPBOARD_HTML = 3,  /* HTML, UTF-8 */
    OSDROP_CLIPBOARD_IMAGE = 4, /* PNG */
    OSDROP_CLIPBOARD_FILES = 5, /* files and folders, fetched with osdrop_mesh_receive */
    OSDROP_CLIPBOARD_FORMAT_MAX_ENUM = 0x7FFFFFFF
} osdrop_clipboard_format;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

/* One format of a copy on this computer, with its bytes. */
typedef struct osdrop_clipboard_content {
    osdrop_clipboard_format format; /* TEXT, RTF, HTML or IMAGE */
    const uint8_t* data;
    size_t size;
} osdrop_clipboard_content;

/* One format of a copy on another member, as announced. */
typedef struct osdrop_announced_format {
    osdrop_clipboard_format format;
    uint64_t byte_count; /* for FILES, the total of the files */
} osdrop_announced_format;

/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */
typedef enum osdrop_input_kind {
    OSDROP_INPUT_POINTER = 1, /* physical: x, y where this computer's cursor is, dx, dy the
                                 movement; to inject: move the cursor to x, y */
    OSDROP_INPUT_KEY = 2,
    OSDROP_INPUT_BUTTON = 3,
    OSDROP_INPUT_SCROLL = 4,
    OSDROP_INPUT_KIND_MAX_ENUM = 0x7FFFFFFF
} osdrop_input_kind;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

/*
 * One input event: physical input this computer captured, or input another computer sends to be
 * injected here. Positions are native, in 1/16 point (docs/PROTOCOL.md, "Conventions"); buttons
 * and scrolls to inject carry the position they happen at.
 */
typedef struct osdrop_input {
    osdrop_input_kind kind;
    int32_t x;
    int32_t y;
    int32_t dx; /* pointer: 1/16 point; scroll: 1/256 of a unit, positive moves content
                   right and down */
    int32_t dy;
    uint16_t page;        /* key: 7 keyboard, 12 consumer */
    uint16_t usage;       /* key: the HID usage */
    uint8_t action;       /* key: 1 down, 2 up, 3 repeat; button: 1 down, 2 up */
    uint8_t button;       /* 0 left, 1 right, 2 middle, 3-31 others */
    uint8_t click_count;  /* button: 1 or more, from this computer's double-click settings */
    uint8_t scroll_unit;  /* 1 lines (wheels), 2 points (trackpads, continuous wheels) */
    uint8_t scroll_phase; /* 0 none, 1 may begin, 2 began, 3 changed, 4 ended, 5 cancelled */
    uint8_t momentum;     /* 0 none, 1 began, 2 changed, 3 ended */
} osdrop_input;

typedef struct osdrop_transfer_progress {
    uint64_t transfer_id;
    uint64_t bytes_done;
    uint64_t bytes_total;
    uint64_t files_done;
    uint64_t files_total;
} osdrop_transfer_progress;

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
    /*
     * An offered item was scanned: OSDROP_OK with the bytes of its files, its entries and the
     * entries that cannot travel (left out), or why it cannot be sent.
     */
    void (*offer_ready)(void* context, const osdrop_object_ref* item, osdrop_result result,
                        uint64_t total_bytes, uint64_t entry_count, uint64_t skipped);
    /* At most every 100 ms while a transfer this computer receives makes progress. */
    void (*transfer_progress)(void* context, const osdrop_transfer_progress* progress);
    /*
     * The end of a transfer this computer receives. OSDROP_OK: `items` are the item's top-level
     * files and folders as created (absolute UTF-8 paths, NUL-terminated, valid during the call),
     * `skipped_links` the links not recreated. Otherwise the reason, after everything the
     * transfer created was removed.
     */
    void (*transfer_finished)(void* context, uint64_t transfer_id, osdrop_result result,
                              const char* const* items, size_t item_count, uint64_t skipped_links);
    /*
     * Another member's copy is now the newest (the latest copy wins): `formats` (valid during
     * the call) are what can be fetched with item {OSDROP_OBJECT_CLIPBOARD, clip_id, format}.
     */
    void (*clipboard_changed)(void* context, const uint8_t* device_id, uint64_t clip_id,
                              const osdrop_announced_format* formats, size_t count);
    /* The end of osdrop_mesh_fetch_content: OSDROP_OK with the content (valid during the call),
     * or the reason. */
    void (*content_received)(void* context, uint64_t transfer_id, osdrop_result result,
                             const uint8_t* data, size_t size);
    /*
     * The cursor went to the member `device_id`: capture this computer's input and hide its
     * cursor. The keys in `release_here` (KEY events with action 2) went down on this computer;
     * release them here, since their release now goes to the other computer.
     */
    void (*capture_began)(void* context, const uint8_t* device_id, const osdrop_input* release_here,
                          size_t count);
    /*
     * The cursor came back from `device_id`: give the input back and show the cursor at `x`, `y`.
     * `reason`: 1 pushed back, 2 hotkey, 3 capture lost, 4 refused (`refused`: 1 locked,
     * 2 asleep, 3 not permitted, 4 busy; otherwise 0), 5 someone used the other computer, 6 the
     * other computer stopped answering, left or stopped accepting control.
     */
    void (*capture_ended)(void* context, const uint8_t* device_id, int32_t x, int32_t y,
                          uint8_t reason, uint8_t refused);
    /* The member `controller_id` controls this computer now; NULL when the control ended. */
    void (*controlled)(void* context, const uint8_t* controller_id);
    /* Input from the controller to apply here as if this computer's own devices produced it. */
    void (*inject)(void* context, const osdrop_input* input);
    /* While this computer is controlled: motion latency from the controller's capture to here. */
    void (*input_latency)(void* context, const uint8_t* controller_id, uint32_t samples,
                          uint32_t median_us, uint32_t p99_us);
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
 * Transfers (DEVPLAN, Phase 13). File work runs on a thread of its own; results arrive through the
 * offer_ready, transfer_progress and transfer_finished callbacks.
 *
 * osdrop_mesh_offer offers local files and folders (`path_count` absolute UTF-8 paths) as one
 * item to the members in `audience` (`audience_count` device IDs of OSDROP_DEVICE_ID_SIZE bytes,
 * one after another). Links inside folders travel as links; a path that is itself a link is
 * followed. It replaces an earlier offer of the same item, and scanning starts at once.
 */
osdrop_result osdrop_mesh_offer(osdrop_mesh* mesh, const osdrop_object_ref* item,
                                const char* const* paths, size_t path_count,
                                const uint8_t* audience, size_t audience_count);
/* Withdraws every item of an object; members fetching them get CANCELLED. */
osdrop_result osdrop_mesh_withdraw(osdrop_mesh* mesh, osdrop_object_kind kind, uint64_t object_id);
/*
 * Fetches an item a member offered into the folder `destination` (absolute UTF-8). Names the
 * folder already holds are numbered ("name (2)"). The transfer ends with TOO_LARGE when its files
 * add up to more than `max_bytes` (UINT64_MAX: no limit). NOT_PERMITTED when the member may not
 * send this computer files, INVALID_STATE when it is not connected.
 */
osdrop_result osdrop_mesh_receive(osdrop_mesh* mesh, const uint8_t* device_id,
                                  const osdrop_object_ref* item, const char* destination,
                                  uint64_t max_bytes, uint64_t* out_transfer_id);
/* Ends a transfer with CANCELLED; what it created is removed. Unknown IDs are ignored. */
osdrop_result osdrop_mesh_cancel_transfer(osdrop_mesh* mesh, uint64_t transfer_id);

/*
 * The shared clipboard (DEVPLAN, Phase 15). osdrop_mesh_clipboard_copied reports a copy on this
 * computer: the contents of its text, RTF, HTML and PNG formats (`count` of them, each format at
 * most once) and the absolute UTF-8 paths of copied files (`file_count`). It is announced to the
 * members this computer shares clipboards with, formats and sizes only (files once scanned), and
 * its contents are served from here. Writes the new clip ID to `*out_clip_id`.
 */
osdrop_result osdrop_mesh_clipboard_copied(osdrop_mesh* mesh,
                                           const osdrop_clipboard_content* contents, size_t count,
                                           const char* const* files, size_t file_count,
                                           uint64_t* out_clip_id);
/*
 * Fetches one format (TEXT, RTF, HTML or IMAGE) of another member's copy into memory; the result
 * arrives through content_received. It ends with TOO_LARGE beyond `max_bytes`.
 */
osdrop_result osdrop_mesh_fetch_content(osdrop_mesh* mesh, const uint8_t* device_id,
                                        const osdrop_object_ref* item, uint64_t max_bytes,
                                        uint64_t* out_transfer_id);

/*
 * Physical input on this computer, captured `age_us` microseconds ago: all of it, whether the
 * cursor is here or on another computer. Physical input while another computer controls this one
 * ends that control at once. Does not wait for the I/O thread.
 */
osdrop_result osdrop_mesh_input(osdrop_mesh* mesh, const osdrop_input* input, uint64_t age_us);
/* Brings the cursor home to where it left: `input_lost` 0 for the user's hotkey, 1 when the
 * platform lost its capture. */
osdrop_result osdrop_mesh_return_home(osdrop_mesh* mesh, uint8_t input_lost);
/* "Lock input here": while `locked` is 1, the cursor stays on the computer it is on. */
osdrop_result osdrop_mesh_lock_input(osdrop_mesh* mesh, uint8_t locked);
/*
 * Whether this computer can be controlled: 0 yes, 1 its screen is locked, 2 it is going to sleep.
 * Other computers are refused with that reason, and a computer controlling it is released.
 */
osdrop_result osdrop_mesh_set_unavailable(osdrop_mesh* mesh, uint8_t reason);
/* While this computer is controlled: where its cursor really is, when something else moved it. */
osdrop_result osdrop_mesh_cursor_moved(osdrop_mesh* mesh, int32_t x, int32_t y);

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
