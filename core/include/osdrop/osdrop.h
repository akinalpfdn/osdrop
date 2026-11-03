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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSDROP_OSDROP_H */
