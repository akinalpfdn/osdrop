#include <osdrop/sys/input_client.hpp>
#include <osdrop/sys/session.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>

namespace osdrop::sys {
namespace {

constexpr DWORD kBusyWaitMilliseconds = 2000;
constexpr DWORD kWriteTimeoutMilliseconds = 2000;

/// OsdropInput's first word on a new connection.
struct Admission {
    enum class Kind : std::uint8_t {
        Accepted,
        TurnedAway,
        Closed, ///< the pipe closed first: refused by an OsdropInput from before TurnedAway
        Silent, ///< nothing within kAdmissionTimeout: an OsdropInput from before Accepted took it
    };
    Kind kind = Kind::Silent;
    win::input::TurnAway reason = win::input::TurnAway::OtherCopy;
};

Admission admission(std::span<const std::uint8_t> message) {
    const auto report = win::input::decode_report(message);
    if (report) {
        if (const auto* refused = std::get_if<win::input::TurnedAway>(&report.value())) {
            return {.kind = Admission::Kind::TurnedAway, .reason = refused->reason};
        }
        if (std::holds_alternative<win::input::Accepted>(report.value())) {
            return {.kind = Admission::Kind::Accepted};
        }
    }
    return {}; // nothing an OsdropInput says first: carry on as with one that says nothing
}

Admission read_admission(HANDLE pipe) {
    const UniqueHandle done = make_event(true);
    std::array<std::uint8_t, win::input::kMaxMessageSize> buffer{};
    OVERLAPPED overlapped{};
    overlapped.hEvent = done.get();
    DWORD received = 0;
    if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &received, &overlapped) ==
        FALSE) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return {.kind = Admission::Kind::Closed};
        }
        const auto timeout = static_cast<DWORD>(win::input::kAdmissionTimeout.count());
        if (WaitForSingleObject(done.get(), timeout) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &overlapped);
            if (GetOverlappedResult(pipe, &overlapped, &received, TRUE) == FALSE) {
                return {}; // still open and silent
            }
        } else if (GetOverlappedResult(pipe, &overlapped, &received, FALSE) == FALSE) {
            return {.kind = Admission::Kind::Closed};
        }
    }
    return admission(std::span<const std::uint8_t>(buffer.data(), received));
}

} // namespace

InputConnection InputClient::connect(OnReport on_report, OnClosed on_closed) {
    const std::wstring name = input_pipe_name(current_session());
    for (int attempt = 0; attempt < 2; ++attempt) {
        // Identification lets OsdropInput check who connects; it cannot act as this user.
        UniqueHandle pipe(CreateFileW(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
        if (pipe.get() != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr)) {
                return {};
            }
            const Admission answer = read_admission(pipe.get());
            switch (answer.kind) {
            case Admission::Kind::TurnedAway:
                return {.client = nullptr, .turned_away = true, .reason = answer.reason};
            case Admission::Kind::Closed:
                return {.client = nullptr, .turned_away = true, .reason = std::nullopt};
            case Admission::Kind::Accepted:
            case Admission::Kind::Silent:
                break;
            }
            return {.client = std::unique_ptr<InputClient>(new InputClient(
                        std::move(pipe), std::move(on_report), std::move(on_closed))),
                    .turned_away = false,
                    .reason = std::nullopt};
        }
        if (GetLastError() != ERROR_PIPE_BUSY ||
            !WaitNamedPipeW(name.c_str(), kBusyWaitMilliseconds)) {
            return {};
        }
    }
    return {};
}

InputClient::InputClient(UniqueHandle pipe, OnReport on_report, OnClosed on_closed)
    : pipe_(std::move(pipe)), stop_(make_event(true)), written_(make_event(true)),
      on_report_(std::move(on_report)), on_closed_(std::move(on_closed)),
      reader_([this] { read_reports(); }) {}

InputClient::~InputClient() {
    closing_ = true;
    SetEvent(stop_.get());
    if (reader_.joinable()) {
        reader_.join();
    }
}

bool InputClient::send(const win::input::Request& request) {
    const win::input::Bytes bytes = win::input::encode(request);
    const std::scoped_lock lock(send_mutex_);
    OVERLAPPED overlapped{};
    overlapped.hEvent = written_.get();
    ResetEvent(written_.get());
    DWORD written = 0;
    if (WriteFile(pipe_.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                  &overlapped)) {
        return written == bytes.size();
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    if (WaitForSingleObject(written_.get(), kWriteTimeoutMilliseconds) != WAIT_OBJECT_0) {
        CancelIoEx(pipe_.get(), &overlapped);
        GetOverlappedResult(pipe_.get(), &overlapped, &written, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe_.get(), &overlapped, &written, FALSE) != FALSE &&
           written == bytes.size();
}

void InputClient::read_reports() {
    const UniqueHandle read_event = make_event(true);
    std::array<std::uint8_t, win::input::kMaxMessageSize> buffer{};
    while (!closing_) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = read_event.get();
        ResetEvent(read_event.get());
        DWORD received = 0;
        bool done = ReadFile(pipe_.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                             &received, &overlapped) != FALSE;
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            const std::array<HANDLE, 2> handles{read_event.get(), stop_.get()};
            if (WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                       INFINITE) != WAIT_OBJECT_0) {
                CancelIoEx(pipe_.get(), &overlapped);
                GetOverlappedResult(pipe_.get(), &overlapped, &received, TRUE);
                return; // closing
            }
            done = GetOverlappedResult(pipe_.get(), &overlapped, &received, FALSE) != FALSE;
        }
        if (!done) {
            break; // the pipe broke, or a message larger than any report
        }
        const auto report =
            win::input::decode_report(std::span<const std::uint8_t>(buffer.data(), received));
        if (report && on_report_) {
            on_report_(report.value());
        }
    }
    if (!closing_ && on_closed_) {
        on_closed_();
    }
}

} // namespace osdrop::sys
