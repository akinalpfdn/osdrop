#include <osdrop/win/input_protocol.hpp>

#include <array>
#include <optional>
#include <utility>

namespace osdrop::win::input {
namespace {

// Message types: requests below 100, reports from 101.
enum class Type : std::uint8_t {
    BeginControl = 1,
    EndControl = 2,
    Move = 3,
    Press = 4,
    Scroll = 5,
    Key = 6,
    StartCapture = 7,
    StopCapture = 8,
    QueryStats = 9,
    Watch = 10,
    Heartbeat = 11,
    SetChords = 12,
    CapturedMotion = 101,
    PhysicalPress = 102,
    PhysicalScroll = 103,
    PhysicalKey = 104,
    LocalInput = 105,
    Stats = 106,
    Rejected = 107,
    WatchedMotion = 108,
    HotkeyPressed = 109,
    Released = 110,
};

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value) { little(value, 2); }
    void u32(std::uint32_t value) { little(value, 4); }
    void u64(std::uint64_t value) { little(value, 8); }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void flag(bool value) { u8(value ? 1 : 0); }

    [[nodiscard]] Bytes message(Type type) const {
        Writer out;
        out.u32(kMagic);
        out.u16(kVersion);
        out.u16(static_cast<std::uint16_t>(type));
        out.u32(static_cast<std::uint32_t>(bytes_.size()));
        out.bytes_.insert(out.bytes_.end(), bytes_.begin(), bytes_.end());
        return out.bytes_;
    }

private:
    void little(std::uint64_t value, int count) {
        for (int i = 0; i < count; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

    Bytes bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool done() const { return offset_ == bytes_.size(); }

    std::optional<std::uint64_t> little(std::size_t count) {
        if (bytes_.size() - offset_ < count) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            value |= static_cast<std::uint64_t>(bytes_[offset_ + i]) << (8 * i);
        }
        offset_ += count;
        return value;
    }
    std::optional<std::uint8_t> u8() { return narrow<std::uint8_t>(little(1)); }
    std::optional<std::uint16_t> u16() { return narrow<std::uint16_t>(little(2)); }
    std::optional<std::uint32_t> u32() { return narrow<std::uint32_t>(little(4)); }
    std::optional<std::uint64_t> u64() { return little(8); }
    std::optional<std::int32_t> i32() {
        const auto value = u32();
        return value ? std::optional(static_cast<std::int32_t>(*value)) : std::nullopt;
    }
    /// 0 or 1; anything else is out of range.
    std::optional<std::optional<bool>> flag() {
        const auto value = u8();
        if (!value) {
            return std::nullopt;
        }
        return *value <= 1 ? std::optional(*value == 1) : std::optional<bool>();
    }

private:
    template <typename T>
    static std::optional<T> narrow(std::optional<std::uint64_t> value) {
        return value ? std::optional(static_cast<T>(*value)) : std::nullopt;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

bool in_range(std::int32_t value, std::int32_t limit) {
    return value >= -limit && value <= limit;
}

void write_point(Writer& w, std::int32_t x, std::int32_t y) {
    w.i32(x);
    w.i32(y);
}

void write_press(Writer& w, Button button, bool down) {
    w.u8(static_cast<std::uint8_t>(button));
    w.flag(down);
}

void write_key(Writer& w, ScanCode scan, bool down) {
    w.u16(scan.code);
    w.flag(scan.extended);
    w.flag(down);
}

void write_chord(Writer& w, const Chord& chord) {
    w.u8(chord.modifiers);
    w.u16(chord.key.code);
    w.flag(chord.key.extended);
}

// Each reader takes the body; nullopt for a short body, ValueOutOfRange for bad values.
template <typename T>
using Field = Expected<T, DecodeError>;

struct Point {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

Field<Point> read_point(Reader& r, std::int32_t limit) {
    const auto x = r.i32();
    const auto y = r.i32();
    if (!x || !y) {
        return Field<Point>::failure(DecodeError::Truncated);
    }
    if (!in_range(*x, limit) || !in_range(*y, limit)) {
        return Field<Point>::failure(DecodeError::ValueOutOfRange);
    }
    return Point{.x = *x, .y = *y};
}

Field<std::pair<Button, bool>> read_press(Reader& r) {
    using Result = Field<std::pair<Button, bool>>;
    const auto button = r.u8();
    const auto down = r.flag();
    if (!button || !down) {
        return Result::failure(DecodeError::Truncated);
    }
    if (*button > static_cast<std::uint8_t>(Button::X2) || !*down) {
        return Result::failure(DecodeError::ValueOutOfRange);
    }
    return std::pair(static_cast<Button>(*button), **down);
}

Field<std::pair<ScanCode, bool>> read_key(Reader& r) {
    using Result = Field<std::pair<ScanCode, bool>>;
    const auto code = r.u16();
    const auto extended = r.flag();
    const auto down = r.flag();
    if (!code || !extended || !down) {
        return Result::failure(DecodeError::Truncated);
    }
    if (!*extended || !*down) {
        return Result::failure(DecodeError::ValueOutOfRange);
    }
    const ScanCode scan{.code = *code, .extended = **extended};
    if (!usage_of(scan)) {
        return Result::failure(DecodeError::ValueOutOfRange); // no key has it
    }
    return std::pair(scan, **down);
}

/// A one-byte enum whose values run from 1 to `last`.
template <typename E>
Field<E> read_code(Reader& r, E last) {
    const auto code = r.u8();
    if (!code) {
        return Field<E>::failure(DecodeError::Truncated);
    }
    if (*code < 1 || *code > static_cast<std::uint8_t>(last)) {
        return Field<E>::failure(DecodeError::ValueOutOfRange);
    }
    return static_cast<E>(*code);
}

Field<Chord> read_chord(Reader& r) {
    const auto modifiers = r.u8();
    const auto code = r.u16();
    const auto extended = r.flag();
    if (!modifiers || !code || !extended) {
        return Field<Chord>::failure(DecodeError::Truncated);
    }
    if (!*extended) {
        return Field<Chord>::failure(DecodeError::ValueOutOfRange);
    }
    return Chord{.key = {.code = *code, .extended = **extended}, .modifiers = *modifiers};
}

Field<SetChords> read_set_chords(Reader& r) {
    const auto home = read_chord(r);
    if (!home) {
        return Field<SetChords>::failure(home.error());
    }
    const auto lock = read_chord(r);
    if (!lock) {
        return Field<SetChords>::failure(lock.error());
    }
    if (!valid_pair(home.value(), lock.value())) {
        return Field<SetChords>::failure(DecodeError::ValueOutOfRange);
    }
    return SetChords{.return_home = home.value(), .lock_input = lock.value()};
}

Field<StartCapture> read_start_capture(Reader& r) {
    const auto park = read_point(r, kMaxCoordinate);
    if (!park) {
        return Field<StartCapture>::failure(park.error());
    }
    const auto home = read_point(r, kMaxCoordinate);
    if (!home) {
        return Field<StartCapture>::failure(home.error());
    }
    return StartCapture{.park_x = park.value().x,
                        .park_y = park.value().y,
                        .home_x = home.value().x,
                        .home_y = home.value().y};
}

Field<CapturedMotion> read_captured_motion(Reader& r) {
    const auto delta = read_point(r, kMaxCoordinate);
    if (!delta) {
        return Field<CapturedMotion>::failure(delta.error());
    }
    const auto ticks = r.u64();
    if (!ticks) {
        return Field<CapturedMotion>::failure(DecodeError::Truncated);
    }
    return CapturedMotion{.dx = delta.value().x, .dy = delta.value().y, .ticks = *ticks};
}

Field<WatchedMotion> read_watched_motion(Reader& r) {
    const auto position = read_point(r, kMaxCoordinate);
    if (!position) {
        return Field<WatchedMotion>::failure(position.error());
    }
    const auto delta = read_point(r, kMaxCoordinate);
    if (!delta) {
        return Field<WatchedMotion>::failure(delta.error());
    }
    const auto ticks = r.u64();
    if (!ticks) {
        return Field<WatchedMotion>::failure(DecodeError::Truncated);
    }
    return WatchedMotion{.x = position.value().x,
                         .y = position.value().y,
                         .dx = delta.value().x,
                         .dy = delta.value().y,
                         .ticks = *ticks};
}

Field<Stats> read_stats(Reader& r) {
    const auto events = r.u64();
    const auto p50 = r.u32();
    const auto p99 = r.u32();
    const auto max = r.u32();
    if (!events || !p50 || !p99 || !max) {
        return Field<Stats>::failure(DecodeError::Truncated);
    }
    return Stats{.events = *events, .p50_us = *p50, .p99_us = *p99, .max_us = *max};
}

Field<Rejected> read_rejected(Reader& r) {
    const auto type = r.u16();
    const auto reason = r.u16();
    if (!type || !reason) {
        return Field<Rejected>::failure(DecodeError::Truncated);
    }
    if (*reason < static_cast<std::uint16_t>(Refusal::Malformed) ||
        *reason > static_cast<std::uint16_t>(Refusal::NotControlling)) {
        return Field<Rejected>::failure(DecodeError::ValueOutOfRange);
    }
    return Rejected{.type = *type, .reason = static_cast<Refusal>(*reason)};
}

struct Header {
    std::uint16_t type = 0;
    std::span<const std::uint8_t> body;
};

Expected<Header, DecodeError> read_header(std::span<const std::uint8_t> message) {
    using Result = Expected<Header, DecodeError>;
    Reader r(message);
    const auto magic = r.u32();
    const auto version = r.u16();
    const auto type = r.u16();
    const auto length = r.u32();
    if (!magic || !version || !type || !length) {
        return Result::failure(DecodeError::Truncated);
    }
    if (*magic != kMagic) {
        return Result::failure(DecodeError::BadMagic);
    }
    if (*version != kVersion) {
        return Result::failure(DecodeError::UnsupportedVersion);
    }
    if (message.size() - kHeaderSize != *length) {
        return Result::failure(DecodeError::LengthMismatch);
    }
    if (*type > 0xFF) {
        return Result::failure(DecodeError::UnknownType); // every type fits a byte
    }
    return Header{.type = *type, .body = message.subspan(kHeaderSize)};
}

/// Decodes `T` from a body with `read`, which must consume all of it.
template <typename Message, typename T, typename Read>
Expected<Message, DecodeError> body(std::span<const std::uint8_t> bytes, const Read& read) {
    Reader r(bytes); // NOLINT(misc-const-correctness): read() consumes it
    Field<T> value = read(r);
    if (!value) {
        return Expected<Message, DecodeError>::failure(value.error());
    }
    if (!r.done()) {
        return Expected<Message, DecodeError>::failure(DecodeError::LengthMismatch);
    }
    return Message(std::move(value.value()));
}

template <typename Message, typename Empty>
Expected<Message, DecodeError> empty(std::span<const std::uint8_t> bytes) {
    if (!bytes.empty()) {
        return Expected<Message, DecodeError>::failure(DecodeError::LengthMismatch);
    }
    return Message(Empty{});
}

} // namespace

Bytes encode(const Request& request) {
    Writer w;
    const Type type = std::visit(Overloaded{
                                     [](const BeginControl&) { return Type::BeginControl; },
                                     [](const EndControl&) { return Type::EndControl; },
                                     [&](const Move& m) {
                                         write_point(w, m.x, m.y);
                                         return Type::Move;
                                     },
                                     [&](const Press& m) {
                                         write_press(w, m.button, m.down);
                                         return Type::Press;
                                     },
                                     [&](const Scroll& m) {
                                         write_point(w, m.vertical, m.horizontal);
                                         return Type::Scroll;
                                     },
                                     [&](const Key& m) {
                                         write_key(w, m.scan, m.down);
                                         return Type::Key;
                                     },
                                     [&](const StartCapture& m) {
                                         write_point(w, m.park_x, m.park_y);
                                         write_point(w, m.home_x, m.home_y);
                                         return Type::StartCapture;
                                     },
                                     [&](const StopCapture& m) {
                                         write_point(w, m.x, m.y);
                                         return Type::StopCapture;
                                     },
                                     [](const QueryStats&) { return Type::QueryStats; },
                                     [&](const Watch& m) {
                                         w.flag(m.on);
                                         return Type::Watch;
                                     },
                                     [](const Heartbeat&) { return Type::Heartbeat; },
                                     [&](const SetChords& m) {
                                         write_chord(w, m.return_home);
                                         write_chord(w, m.lock_input);
                                         return Type::SetChords;
                                     },
                                 },
                                 request);
    return w.message(type);
}

Bytes encode(const Report& report) {
    Writer w;
    const Type type = std::visit(Overloaded{
                                     [&](const CapturedMotion& m) {
                                         write_point(w, m.dx, m.dy);
                                         w.u64(m.ticks);
                                         return Type::CapturedMotion;
                                     },
                                     [&](const WatchedMotion& m) {
                                         write_point(w, m.x, m.y);
                                         write_point(w, m.dx, m.dy);
                                         w.u64(m.ticks);
                                         return Type::WatchedMotion;
                                     },
                                     [&](const PhysicalPress& m) {
                                         write_press(w, m.button, m.down);
                                         return Type::PhysicalPress;
                                     },
                                     [&](const PhysicalScroll& m) {
                                         write_point(w, m.vertical, m.horizontal);
                                         return Type::PhysicalScroll;
                                     },
                                     [&](const PhysicalKey& m) {
                                         write_key(w, m.scan, m.down);
                                         return Type::PhysicalKey;
                                     },
                                     [&](const HotkeyPressed& m) {
                                         w.u8(static_cast<std::uint8_t>(m.hotkey));
                                         return Type::HotkeyPressed;
                                     },
                                     [&](const Released& m) {
                                         w.u8(static_cast<std::uint8_t>(m.reason));
                                         return Type::Released;
                                     },
                                     [](const LocalInput&) { return Type::LocalInput; },
                                     [&](const Stats& m) {
                                         w.u64(m.events);
                                         w.u32(m.p50_us);
                                         w.u32(m.p99_us);
                                         w.u32(m.max_us);
                                         return Type::Stats;
                                     },
                                     [&](const Rejected& m) {
                                         w.u16(m.type);
                                         w.u16(static_cast<std::uint16_t>(m.reason));
                                         return Type::Rejected;
                                     },
                                 },
                                 report);
    return w.message(type);
}

Expected<Request, DecodeError> decode_request(std::span<const std::uint8_t> message) {
    using Result = Expected<Request, DecodeError>;
    const auto header = read_header(message);
    if (!header) {
        return Result::failure(header.error());
    }
    const std::span<const std::uint8_t> bytes = header.value().body;
    switch (static_cast<Type>(header.value().type)) {
    case Type::BeginControl:
        return empty<Request, BeginControl>(bytes);
    case Type::EndControl:
        return empty<Request, EndControl>(bytes);
    case Type::QueryStats:
        return empty<Request, QueryStats>(bytes);
    case Type::Heartbeat:
        return empty<Request, Heartbeat>(bytes);
    case Type::SetChords:
        return body<Request, SetChords>(bytes, read_set_chords);
    case Type::Watch:
        return body<Request, Watch>(bytes, [](Reader& r) {
            const auto on = r.flag();
            if (!on) {
                return Field<Watch>::failure(DecodeError::Truncated);
            }
            return *on ? Field<Watch>(Watch{.on = **on})
                       : Field<Watch>::failure(DecodeError::ValueOutOfRange);
        });
    case Type::Move:
        return body<Request, Move>(bytes, [](Reader& r) {
            const auto p = read_point(r, kMaxCoordinate);
            return p ? Field<Move>(Move{.x = p.value().x, .y = p.value().y})
                     : Field<Move>::failure(p.error());
        });
    case Type::StartCapture:
        return body<Request, StartCapture>(bytes, read_start_capture);
    case Type::StopCapture:
        return body<Request, StopCapture>(bytes, [](Reader& r) {
            const auto p = read_point(r, kMaxCoordinate);
            return p ? Field<StopCapture>(StopCapture{.x = p.value().x, .y = p.value().y})
                     : Field<StopCapture>::failure(p.error());
        });
    case Type::Scroll:
        return body<Request, Scroll>(bytes, [](Reader& r) {
            const auto p = read_point(r, kMaxScroll);
            return p ? Field<Scroll>(Scroll{.vertical = p.value().x, .horizontal = p.value().y})
                     : Field<Scroll>::failure(p.error());
        });
    case Type::Press:
        return body<Request, Press>(bytes, [](Reader& r) {
            const auto p = read_press(r);
            return p ? Field<Press>(Press{.button = p.value().first, .down = p.value().second})
                     : Field<Press>::failure(p.error());
        });
    case Type::Key:
        return body<Request, Key>(bytes, [](Reader& r) {
            const auto k = read_key(r);
            return k ? Field<Key>(Key{.scan = k.value().first, .down = k.value().second})
                     : Field<Key>::failure(k.error());
        });
    default:
        return Result::failure(DecodeError::UnknownType);
    }
}

Expected<Report, DecodeError> decode_report(std::span<const std::uint8_t> message) {
    using Result = Expected<Report, DecodeError>;
    const auto header = read_header(message);
    if (!header) {
        return Result::failure(header.error());
    }
    const std::span<const std::uint8_t> bytes = header.value().body;
    switch (static_cast<Type>(header.value().type)) {
    case Type::LocalInput:
        return empty<Report, LocalInput>(bytes);
    case Type::CapturedMotion:
        return body<Report, CapturedMotion>(bytes, read_captured_motion);
    case Type::WatchedMotion:
        return body<Report, WatchedMotion>(bytes, read_watched_motion);
    case Type::PhysicalScroll:
        return body<Report, PhysicalScroll>(bytes, [](Reader& r) {
            const auto p = read_point(r, kMaxScroll);
            return p ? Field<PhysicalScroll>(
                           PhysicalScroll{.vertical = p.value().x, .horizontal = p.value().y})
                     : Field<PhysicalScroll>::failure(p.error());
        });
    case Type::PhysicalPress:
        return body<Report, PhysicalPress>(bytes, [](Reader& r) {
            const auto p = read_press(r);
            return p ? Field<PhysicalPress>(
                           PhysicalPress{.button = p.value().first, .down = p.value().second})
                     : Field<PhysicalPress>::failure(p.error());
        });
    case Type::PhysicalKey:
        return body<Report, PhysicalKey>(bytes, [](Reader& r) {
            const auto k = read_key(r);
            return k ? Field<PhysicalKey>(
                           PhysicalKey{.scan = k.value().first, .down = k.value().second})
                     : Field<PhysicalKey>::failure(k.error());
        });
    case Type::HotkeyPressed:
        return body<Report, HotkeyPressed>(bytes, [](Reader& r) {
            const auto hotkey = read_code(r, Hotkey::LockInput);
            return hotkey ? Field<HotkeyPressed>(HotkeyPressed{.hotkey = hotkey.value()})
                          : Field<HotkeyPressed>::failure(hotkey.error());
        });
    case Type::Released:
        return body<Report, Released>(bytes, [](Reader& r) {
            const auto reason = read_code(r, ReleaseReason::Watchdog);
            return reason ? Field<Released>(Released{.reason = reason.value()})
                          : Field<Released>::failure(reason.error());
        });
    case Type::Stats:
        return body<Report, Stats>(bytes, read_stats);
    case Type::Rejected:
        return body<Report, Rejected>(bytes, read_rejected);
    default:
        return Result::failure(DecodeError::UnknownType);
    }
}

std::uint16_t type_of(std::span<const std::uint8_t> message) {
    if (message.size() < kHeaderSize) {
        return 0;
    }
    Reader r(message.subspan(6, 2));
    return r.u16().value_or(0);
}

std::uint16_t type_of(const Request& request) {
    const Type type = std::visit(Overloaded{
                                     [](const BeginControl&) { return Type::BeginControl; },
                                     [](const EndControl&) { return Type::EndControl; },
                                     [](const Move&) { return Type::Move; },
                                     [](const Press&) { return Type::Press; },
                                     [](const Scroll&) { return Type::Scroll; },
                                     [](const Key&) { return Type::Key; },
                                     [](const StartCapture&) { return Type::StartCapture; },
                                     [](const StopCapture&) { return Type::StopCapture; },
                                     [](const QueryStats&) { return Type::QueryStats; },
                                     [](const Watch&) { return Type::Watch; },
                                     [](const Heartbeat&) { return Type::Heartbeat; },
                                     [](const SetChords&) { return Type::SetChords; },
                                 },
                                 request);
    return static_cast<std::uint16_t>(type);
}

std::string_view type_name(std::uint16_t type) {
    static constexpr std::array<std::pair<Type, std::string_view>, 22> kNames{{
        {Type::BeginControl, "BeginControl"},
        {Type::EndControl, "EndControl"},
        {Type::Move, "Move"},
        {Type::Press, "Press"},
        {Type::Scroll, "Scroll"},
        {Type::Key, "Key"},
        {Type::StartCapture, "StartCapture"},
        {Type::StopCapture, "StopCapture"},
        {Type::QueryStats, "QueryStats"},
        {Type::Watch, "Watch"},
        {Type::Heartbeat, "Heartbeat"},
        {Type::SetChords, "SetChords"},
        {Type::CapturedMotion, "CapturedMotion"},
        {Type::PhysicalPress, "PhysicalPress"},
        {Type::PhysicalScroll, "PhysicalScroll"},
        {Type::PhysicalKey, "PhysicalKey"},
        {Type::LocalInput, "LocalInput"},
        {Type::Stats, "Stats"},
        {Type::Rejected, "Rejected"},
        {Type::WatchedMotion, "WatchedMotion"},
        {Type::HotkeyPressed, "HotkeyPressed"},
        {Type::Released, "Released"},
    }};
    for (const auto& [known, name] : kNames) {
        if (static_cast<std::uint16_t>(known) == type) {
            return name;
        }
    }
    return "unknown";
}

} // namespace osdrop::win::input
