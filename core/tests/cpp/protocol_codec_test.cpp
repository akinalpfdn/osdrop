#include "protocol/byte_io.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/input_messages.hpp"
#include "protocol/text.hpp"
#include "protocol/transfer_messages.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

namespace protocol = osdrop::protocol;
using protocol::ControlMessage;
using protocol::Error;
using protocol::Id;
using protocol::TransferMessage;

constexpr std::int32_t kEdge = protocol::kMaxCoord;

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

Id id(std::uint8_t seed) {
    Id value{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        value.at(i) = static_cast<std::uint8_t>(seed + i);
    }
    return value;
}

std::vector<ControlMessage> boundary_messages() {
    std::vector<protocol::RosterMember> members;
    for (std::uint8_t i = 0; i < protocol::kMaxMembers; ++i) {
        members.push_back(
            {.device_id = id(i),
             .public_key = {},
             .epoch = std::numeric_limits<std::uint32_t>::max() - i,
             .platform = i % 2 == 0 ? protocol::Platform::MacOS : protocol::Platform::Windows,
             .name = std::string(protocol::kMaxStringBytes, 'm')});
    }
    std::vector<protocol::Revocation> revocations;
    for (std::size_t i = 0; i < protocol::kMaxRevocations; ++i) {
        revocations.push_back({.device_id = id(7), .epoch = static_cast<std::uint32_t>(i + 1)});
    }
    std::vector<protocol::Monitor> monitors;
    for (std::uint8_t i = 0; i < protocol::kMaxMonitors; ++i) {
        monitors.push_back({.key = id(i),
                            .x = i == 0 ? -kEdge : kEdge,
                            .y = -kEdge,
                            .width = i == 0 ? protocol::kMinMonitorSize : protocol::kMaxMonitorSize,
                            .height = protocol::kMaxMonitorSize,
                            .pixel_width = protocol::kMaxPixelSize,
                            .pixel_height = 1,
                            .primary = i == 3});
    }
    std::vector<protocol::Placement> placements;
    for (std::uint8_t i = 0; i < protocol::kMaxPlacements; ++i) {
        placements.push_back({.device_id = id(i), .x = kEdge, .y = -kEdge, .detached = i % 2 == 1});
    }
    std::vector<protocol::OfferedItem> items(
        protocol::kMaxDragItems, {.kind = protocol::ItemKind::Folder,
                                  .byte_count = std::numeric_limits<std::uint64_t>::max(),
                                  .content_type = "public.folder",
                                  .name = "Ä"});
    Id token{};
    token.fill(0xFF);
    return {
        protocol::Hello{.platform = protocol::Platform::Windows,
                        .protocol_minor = 255,
                        .group_id = token,
                        .device_name = std::string(protocol::kMaxStringBytes, 'x')},
        protocol::HandshakeMessage{
            .step = 3, .payload = std::vector<std::uint8_t>(protocol::kMaxHandshakePayload, 7)},
        protocol::PairingConfirm{.decision = protocol::PairingDecision::Rejected},
        protocol::Capabilities{.features = protocol::kKnownFeatures,
                               .max_transfer_streams = protocol::kMaxTransferStreams},
        protocol::SessionSetup{
            .udp_port = 65535, .listen_port = 1, .input_token = token, .transfer_token = id(1)},
        protocol::Ping{.ping_id = 0, .send_time_us = std::numeric_limits<std::uint64_t>::max()},
        protocol::Pong{.ping_id = 0xFFFF'FFFF,
                       .ping_send_time_us = 1,
                       .receive_time_us = 2,
                       .reply_time_us = 3},
        protocol::Heartbeat{},
        protocol::PeerError{.code = 0xFFFF},
        protocol::Bye{.reason = protocol::ByeReason::Duplicate},
        protocol::RosterState{.group_id = token, .members = members, .revocations = revocations},
        protocol::ScreenReport{.monitors = monitors},
        protocol::LayoutState{.revision = std::numeric_limits<std::uint64_t>::max(),
                              .author = token,
                              .placements = placements},
        protocol::Enter{.control_id = 0xFFFF'FFFF,
                        .x = kEdge,
                        .y = -kEdge,
                        .modifiers = 0xFF,
                        .drag_id = 0xFFFF'FFFF},
        protocol::Leave{.control_id = 1, .reason = protocol::LeaveReason::ControllerLost},
        protocol::Refuse{.control_id = 1, .reason = protocol::RefuseReason::Busy},
        protocol::KeyEvent{.control_id = 1,
                           .page = protocol::kConsumerPage,
                           .usage = 0xFFFF,
                           .action = protocol::KeyAction::Repeat},
        protocol::ButtonEvent{.control_id = 1,
                              .button = protocol::kMaxButton,
                              .action = protocol::ButtonAction::Up,
                              .click_count = 255,
                              .x = -kEdge,
                              .y = kEdge},
        protocol::ScrollEvent{.control_id = 1,
                              .unit = protocol::ScrollUnit::Points,
                              .phase = protocol::ScrollPhase::Cancelled,
                              .momentum = protocol::MomentumPhase::Ended,
                              .dx = std::numeric_limits<std::int32_t>::min(),
                              .dy = std::numeric_limits<std::int32_t>::max(),
                              .x = kEdge,
                              .y = kEdge},
        protocol::ReleaseAll{.control_id = 1},
        protocol::CursorReport{.control_id = 1, .x = -kEdge, .y = -kEdge},
        protocol::LocalTakeover{.control_id = 0xFFFF'FFFF},
        protocol::DragProbe{.probe_id = 0xFFFF'FFFF, .x = kEdge, .y = kEdge},
        protocol::DragOffer{.probe_id = 0, .drag_id = 1, .items = items},
        protocol::DragResult{.drag_id = 0xFFFF'FFFF, .outcome = protocol::DragOutcome::Failed},
        protocol::ClipboardAnnounce{
            .clip_id = std::numeric_limits<std::uint64_t>::max(),
            .formats = {{.format = protocol::ClipboardFormat::Files, .byte_count = 1},
                        {.format = protocol::ClipboardFormat::Image, .byte_count = 0},
                        {.format = protocol::ClipboardFormat::Html, .byte_count = 2},
                        {.format = protocol::ClipboardFormat::RichText, .byte_count = 3},
                        {.format = protocol::ClipboardFormat::PlainText, .byte_count = 4}}},
    };
}

TEST(ControlCodec, BoundaryValuesOfEveryTypeRoundTrip) {
    const auto messages = boundary_messages();
    ASSERT_EQ(messages.size(), std::variant_size_v<ControlMessage>);
    for (const ControlMessage& message : messages) {
        SCOPED_TRACE(static_cast<int>(protocol::type_of(message)));
        const auto encoded = protocol::encode_control(message);
        ASSERT_TRUE(encoded.has_value());
        ASSERT_LE(encoded.value().size(), protocol::kMaxControlMessageSize);
        const auto decoded = protocol::decode_control(encoded.value());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded.value(), message);
    }
}

Error encode_error(const ControlMessage& message) {
    const auto encoded = protocol::encode_control(message);
    EXPECT_FALSE(encoded.has_value());
    return encoded.has_value() ? Error::InvalidArgument : encoded.error();
}

TEST(ControlCodec, EncoderRefusesWhatTheDecoderWouldReject) {
    EXPECT_EQ(encode_error(protocol::Hello{.device_name = ""}), Error::InvalidString);
    EXPECT_EQ(encode_error(protocol::Hello{.platform = static_cast<protocol::Platform>(3),
                                           .device_name = "PC"}),
              Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::HandshakeMessage{.step = 1, .payload = {}}),
              Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::Capabilities{.features = 1U << 5U}), Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::SessionSetup{.udp_port = 1, .transfer_token = id(1)}),
              Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::PeerError{.code = 0}), Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::RosterState{.group_id = id(1)}), Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::RosterState{.group_id = id(1),
                                                 .members = {{.device_id = id(2), .name = "A"},
                                                             {.device_id = id(2), .name = "B"}}}),
              Error::ValueOutOfRange);
    EXPECT_EQ(
        encode_error(protocol::ScreenReport{
            .monitors =
                {{.key = id(1), .width = 16, .height = 16, .pixel_width = 1, .pixel_height = 1}}}),
        Error::ValueOutOfRange); // no primary monitor
    EXPECT_EQ(encode_error(protocol::LayoutState{.placements = {{.x = kEdge + 1}}}),
              Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::Enter{}), Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::KeyEvent{.control_id = 1, .usage = 4, .action = {}}),
              Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::ButtonEvent{.control_id = 1, .button = 32}),
              Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::ButtonEvent{.control_id = 1, .click_count = 0}),
              Error::ValueOutOfRange);
    EXPECT_EQ(encode_error(protocol::DragOffer{.probe_id = 0, .drag_id = 0}),
              Error::ValueOutOfRange);
    EXPECT_EQ(
        encode_error(protocol::DragOffer{
            .probe_id = 1, .drag_id = 1, .items = {{.content_type = "public.data", .name = ".."}}}),
        Error::InvalidString);
    EXPECT_EQ(encode_error(protocol::ClipboardAnnounce{
                  .clip_id = 1,
                  .formats = {{.format = protocol::ClipboardFormat::Html},
                              {.format = protocol::ClipboardFormat::Html}}}),
              Error::ValueOutOfRange);
}

TEST(ControlCodec, EncoderReportsTooLargeForAnOfferThatDoesNotFit) {
    // 1024 items with 255-byte names need more than 65536 bytes although the count is allowed.
    const protocol::DragOffer offer{
        .probe_id = 1,
        .drag_id = 1,
        .items = std::vector<protocol::OfferedItem>(
            protocol::kMaxDragItems,
            {.content_type = "public.data", .name = std::string(protocol::kMaxNameBytes, 'n')})};
    EXPECT_EQ(encode_error(offer), Error::TooLarge);
}

TEST(InputCodec, DatagramsRoundTripWithTheirSequence) {
    const std::vector<protocol::InputDatagram> datagrams{
        {.sequence = 0, .message = protocol::InputHello{.input_token = id(9)}},
        {.sequence = std::numeric_limits<std::uint64_t>::max(),
         .message =
             protocol::PointerMotion{
                 .control_id = 3, .x = -kEdge, .y = kEdge, .capture_time_us = 42}},
    };
    for (const auto& datagram : datagrams) {
        const auto encoded = protocol::encode_input(datagram);
        ASSERT_TRUE(encoded.has_value());
        EXPECT_LE(encoded.value().size(), protocol::kMaxInputPlaintextSize);
        const auto decoded = protocol::decode_input(encoded.value());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded.value(), datagram);
    }
    EXPECT_FALSE(protocol::encode_input({.message = protocol::InputHello{}}).has_value());
    EXPECT_FALSE(protocol::encode_input({.message = protocol::PointerMotion{}}).has_value());
}

TEST(TransferCodec, BoundaryValuesOfEveryTypeRoundTrip) {
    const protocol::ObjectRef files{
        .kind = protocol::ObjectKind::Clipboard, .object_id = 1, .item_index = 5};
    const std::vector<TransferMessage> messages{
        protocol::TransferHello{.transfer_token = id(4), .stream_id = 0xFFFF'FFFF},
        protocol::ManifestRequest{.request_id = 1,
                                  .object = {.kind = protocol::ObjectKind::Drag,
                                             .object_id = std::numeric_limits<std::uint64_t>::max(),
                                             .item_index = 1023}},
        protocol::Manifest{.request_id = 0xFFFF'FFFF,
                           .is_last = true,
                           .entries = {{.path = std::string(255, 'a') + "/" + std::string(255, 'b'),
                                        .kind = protocol::EntryKind::File,
                                        .executable = true,
                                        .byte_count = std::numeric_limits<std::uint64_t>::max(),
                                        .modified_us = std::numeric_limits<std::int64_t>::min()},
                                       {.path = "a", .kind = protocol::EntryKind::Folder},
                                       {.path = "a/l",
                                        .kind = protocol::EntryKind::Link,
                                        .byte_count = protocol::kMaxLinkTarget,
                                        .modified_us = std::numeric_limits<std::int64_t>::max()}}},
        protocol::ContentRequest{.request_id = 1,
                                 .object = files,
                                 .entry_index = 0xFFFF'FFFF,
                                 .offset = std::numeric_limits<std::uint64_t>::max(),
                                 .length = 1},
        protocol::ContentChunk{.request_id = 1,
                               .offset = std::numeric_limits<std::uint64_t>::max(),
                               .data = std::vector<std::uint8_t>(protocol::kMaxChunkData, 0xEE)},
        protocol::ContentEnd{.request_id = 1, .byte_count = 0, .digest = {}},
        protocol::TransferError{.request_id = 0, .code = 0xFFFF},
        protocol::TransferCancel{.request_id = 0xFFFF'FFFF},
    };
    ASSERT_EQ(messages.size(), std::variant_size_v<TransferMessage>);
    for (const TransferMessage& message : messages) {
        SCOPED_TRACE(static_cast<int>(protocol::type_of(message)));
        const auto encoded = protocol::encode_transfer(message);
        ASSERT_TRUE(encoded.has_value());
        ASSERT_LE(encoded.value().size(), protocol::kMaxTransferMessageSize);
        const auto decoded = protocol::decode_transfer(encoded.value());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded.value(), message);
    }
}

TEST(TransferCodec, EncoderRefusesWhatTheDecoderWouldReject) {
    const auto error = [](const TransferMessage& message) {
        const auto encoded = protocol::encode_transfer(message);
        EXPECT_FALSE(encoded.has_value());
        return encoded.has_value() ? Error::InvalidArgument : encoded.error();
    };
    const protocol::ObjectRef text{
        .kind = protocol::ObjectKind::Clipboard, .object_id = 1, .item_index = 1};
    EXPECT_EQ(error(protocol::TransferHello{.stream_id = 1}), Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::ManifestRequest{.request_id = 1,
                                              .object = {.object_id = 1, .item_index = 1024}}),
              Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::Manifest{.request_id = 1, .entries = {{.path = "a/./b"}}}),
              Error::InvalidString);
    EXPECT_EQ(
        error(protocol::Manifest{
            .request_id = 1,
            .entries = {{.path = "a", .kind = protocol::EntryKind::Folder, .byte_count = 1}}}),
        Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::ContentRequest{
                  .request_id = 1, .object = text, .entry_index = 1, .length = 1}),
              Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::ContentChunk{.request_id = 1}), Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::TransferError{.code = 0}), Error::ValueOutOfRange);
}

TEST(TransferCodec, EncoderReportsTooLargeForAManifestThatDoesNotFit) {
    const protocol::Manifest manifest{
        .request_id = 1,
        .entries = std::vector<protocol::ManifestEntry>(
            protocol::kMaxManifestEntries, {.path = std::string(protocol::kMaxNameBytes, 'p')})};
    const auto encoded = protocol::encode_transfer(manifest);
    ASSERT_FALSE(encoded.has_value());
    EXPECT_EQ(encoded.error(), Error::TooLarge);
}

TEST(ProtocolText, AcceptsUtf8AndRejectsControlsAndMalformedSequences) {
    EXPECT_TRUE(protocol::is_valid_text(bytes_of("")));
    EXPECT_TRUE(protocol::is_valid_text(bytes_of("Çalışma 💻 日本")));
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("tab\tseparated")));
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\x7F")));
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xC2\x85")));         // U+0085, C1 control
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xE2\x82")));         // truncated
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xF4\x90\x80\x80"))); // above U+10FFFF
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xED\xB0\x80")));     // surrogate
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xE0\x80\xAF")));     // overlong
}

TEST(ProtocolText, TypeIdentifiersNamesAndPaths) {
    EXPECT_TRUE(protocol::is_valid_type_id(bytes_of("com.adobe.pdf")));
    EXPECT_TRUE(protocol::is_valid_type_id(bytes_of("dyn.ah62d4rv4ge80w5pbsa_1-x")));
    EXPECT_FALSE(protocol::is_valid_type_id(bytes_of("")));
    EXPECT_FALSE(protocol::is_valid_type_id(bytes_of("public/data")));
    EXPECT_FALSE(protocol::is_valid_type_id(bytes_of("public.dätä")));
    EXPECT_FALSE(protocol::is_valid_type_id(bytes_of(std::string(129, 'a'))));

    EXPECT_TRUE(protocol::is_valid_name(bytes_of(".hidden")));
    EXPECT_TRUE(protocol::is_valid_name(bytes_of("...")));
    EXPECT_TRUE(protocol::is_valid_name(bytes_of(std::string(255, 'n'))));
    EXPECT_FALSE(protocol::is_valid_name(bytes_of(std::string(256, 'n'))));
    EXPECT_FALSE(protocol::is_valid_name(bytes_of(".")));
    EXPECT_FALSE(protocol::is_valid_name(bytes_of("..")));
    EXPECT_FALSE(protocol::is_valid_name(bytes_of("a/b")));

    EXPECT_TRUE(protocol::is_valid_path(bytes_of("a")));
    EXPECT_TRUE(protocol::is_valid_path(bytes_of("Photos/İstanbul/2026.heic")));
    const std::string four_full =
        std::string(255, 'a') + "/" + std::string(255, 'b') + "/" + std::string(255, 'c') + "/";
    EXPECT_TRUE(
        protocol::is_valid_path(bytes_of(four_full + std::string(254, 'd') + "/e"))); // 1024
    EXPECT_FALSE(protocol::is_valid_path(bytes_of(four_full + std::string(255, 'd') + "/e")));
    for (const char* path : {"", "/", "/a", "a/", "a//b", "a/../b", "./a", "a/."}) {
        EXPECT_FALSE(protocol::is_valid_path(bytes_of(path))) << path;
    }
}

TEST(ByteIo, WriterStopsAtTheEndAndReaderNeverReadsPastIt) {
    std::array<std::uint8_t, 3> out{};
    protocol::Writer writer(out);
    writer.u16(0x0201);
    EXPECT_TRUE(writer.ok());
    writer.u16(0x0403);
    EXPECT_FALSE(writer.ok());
    EXPECT_EQ(writer.size(), 2U);

    protocol::Reader reader(out);
    std::uint16_t value = 0;
    std::uint16_t second = 0;
    EXPECT_TRUE(reader.u16(value));
    EXPECT_EQ(value, 0x0201);
    EXPECT_FALSE(reader.u16(second));
    EXPECT_EQ(reader.remaining(), 1U);
}

TEST(ByteIo, SignedValuesUseTwosComplementLittleEndian) {
    std::array<std::uint8_t, 12> out{};
    protocol::Writer writer(out);
    writer.i32(-2);
    writer.i64(std::numeric_limits<std::int64_t>::min());
    ASSERT_TRUE(writer.ok());
    EXPECT_EQ(out[0], 0xFE);
    EXPECT_EQ(out[3], 0xFF);
    EXPECT_EQ(out[4], 0x00);
    EXPECT_EQ(out[11], 0x80);

    protocol::Reader reader(out);
    std::int32_t small = 0;
    std::int64_t large = 0;
    ASSERT_TRUE(reader.i32(small));
    ASSERT_TRUE(reader.i64(large));
    EXPECT_EQ(small, -2);
    EXPECT_EQ(large, std::numeric_limits<std::int64_t>::min());
}

} // namespace
