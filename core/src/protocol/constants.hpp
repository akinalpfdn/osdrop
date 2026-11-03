#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Wire constants from docs/PROTOCOL.md. Changing any of these changes the protocol.
namespace osdrop::protocol {

inline constexpr std::array<std::uint8_t, 2> kMagic{'O', 'D'};
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint8_t kMinorVersion = 0;

inline constexpr std::size_t kHeaderSize = 4; // magic, version, type
inline constexpr std::size_t kMaxControlMessageSize = 65536;
inline constexpr std::size_t kMaxTransferMessageSize = 65536;
inline constexpr std::size_t kAeadTagSize = 16;

inline constexpr std::size_t kMaxDatagramSize = 1200;
inline constexpr std::size_t kInputHeaderSize = 12; // magic, version, type, sequence
inline constexpr std::size_t kMaxInputPlaintextSize = kMaxDatagramSize - kAeadTagSize;

inline constexpr std::size_t kMaxStringBytes = 64;
inline constexpr std::size_t kMaxTypeIdBytes = 128;
inline constexpr std::size_t kMaxNameBytes = 255;
inline constexpr std::size_t kMaxPathBytes = 1024;

inline constexpr std::size_t kIdSize = 16; // device, group and monitor IDs, tokens
inline constexpr std::size_t kPublicKeySize = 32;
inline constexpr std::size_t kDigestSize = 32;
inline constexpr std::size_t kMaxHandshakePayload = 1024;

inline constexpr std::int32_t kMaxCoord = 1 << 28; // |coord| limit, in 1/16 point
inline constexpr std::uint32_t kKnownFeatures = 0x1FU;
inline constexpr std::uint8_t kMaxTransferStreams = 8;
inline constexpr std::size_t kMaxMembers = 64;
inline constexpr std::size_t kMaxRevocations = 255;
inline constexpr std::size_t kMaxMonitors = 16;
inline constexpr std::int32_t kMinMonitorSize = 16;         // one point, in 1/16 point
inline constexpr std::int32_t kMaxMonitorSize = 16 * 32768; // in 1/16 point
inline constexpr std::uint16_t kMaxPixelSize = 32768;
inline constexpr std::size_t kMaxPlacements = 64;
inline constexpr std::uint8_t kMaxButton = 31;
inline constexpr std::size_t kMaxDragItems = 1024;
inline constexpr std::size_t kMaxClipboardFormats = 5;
inline constexpr std::size_t kMaxManifestEntries = 4096;
inline constexpr std::size_t kContentChunkFieldsSize = 16; // request_id, offset, data_length
inline constexpr std::size_t kMaxChunkData = 65504;
static_assert(kHeaderSize + kContentChunkFieldsSize + kMaxChunkData <= kMaxTransferMessageSize,
              "documented in PROTOCOL.md");

} // namespace osdrop::protocol
