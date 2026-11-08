#include "pipe_server.hpp"

#include <osdrop/sys/session.hpp>

#include <sddl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace osdrop::input {
namespace {

namespace protocol = win::input;

constexpr DWORD kPipeBufferBytes = 64 * 1024;
constexpr DWORD kWriteTimeoutMilliseconds = 1000;
constexpr std::array<const wchar_t*, 2> kClientNames{L"Osdrop.exe", L"OsdropInputTool.exe"};

/// Full access for SYSTEM, read and write for the session's user, nobody else; P blocks
/// inherited entries.
std::wstring security_descriptor(const std::wstring& user_sid) {
    return L"D:P(A;;GA;;;SY)(A;;GRGW;;;" + user_sid + L")";
}

/// The client's user SID, from an identification-level impersonation.
std::optional<std::wstring> client_sid(HANDLE pipe) {
    if (!ImpersonateNamedPipeClient(pipe)) {
        return std::nullopt;
    }
    HANDLE raw = nullptr;
    const BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw);
    if (!RevertToSelf()) {
        std::terminate(); // a SYSTEM process must never go on as its client
    }
    if (!opened) {
        return std::nullopt;
    }
    const sys::UniqueHandle token(raw);
    return sys::token_user_sid(token.get());
}

} // namespace

PipeServer::PipeServer(protocol::InputSession& session, protocol::ReportQueue& reports,
                       HANDLE reports_ready, sys::FileLog& log, HANDLE stop)
    : session_(session), reports_(reports), reports_ready_(reports_ready), log_(log), stop_(stop),
      session_id_(sys::current_session()), written_(sys::make_event(true)) {
    if (const auto folder = sys::executable_folder()) {
        for (const wchar_t* name : kClientNames) {
            clients_.push_back(*folder + L"\\" + name);
        }
    }
}

bool PipeServer::run() {
    const auto user = sys::session_user_sid(session_id_);
    if (!user) {
        log_.error("Nobody is logged on in session " + std::to_string(session_id_));
        return false;
    }
    user_sid_ = *user;
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(security_descriptor(user_sid_).c_str(),
                                                             SDDL_REVISION_1, &raw_descriptor,
                                                             nullptr) == FALSE) {
        log_.error("Cannot build the pipe's security descriptor: " +
                   std::to_string(GetLastError()));
        return false;
    }
    const std::unique_ptr<void, sys::LocalFreer> descriptor(raw_descriptor);
    SECURITY_ATTRIBUTES attributes{.nLength = sizeof(SECURITY_ATTRIBUTES),
                                   .lpSecurityDescriptor = descriptor.get(),
                                   .bInheritHandle = FALSE};
    // One pipe for the life of the process: the name is never free for another process to take,
    // and FILE_FLAG_FIRST_PIPE_INSTANCE fails if one already holds it.
    const std::wstring name = sys::input_pipe_name(session_id_);
    const sys::UniqueHandle pipe(CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        kPipeBufferBytes, kPipeBufferBytes, 0, &attributes));
    if (pipe.get() == INVALID_HANDLE_VALUE) {
        log_.error("Cannot create the input pipe: " + std::to_string(GetLastError()));
        return false;
    }
    log_.info("Serving the input pipe in session " + std::to_string(session_id_));

    const sys::UniqueHandle connected = sys::make_event(true);
    while (WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = connected.get();
        ResetEvent(connected.get());
        DWORD ignored = 0;
        if (!ConnectNamedPipe(pipe.get(), &overlapped)) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                if (wait_for(pipe.get(), overlapped, ignored) != Wait::Done) {
                    continue; // stopping, or a client that went at once
                }
            } else if (error != ERROR_PIPE_CONNECTED) {
                log_.error("Waiting for a client failed: " + std::to_string(error));
                WaitForSingleObject(stop_, 1000);
                continue;
            }
        }
        if (accepted(pipe.get())) {
            log_.info("Client connected");
            serve(pipe.get());
            session_.reset();
            static_cast<void>(reports_.take());
            log_.info("Client disconnected");
        }
        DisconnectNamedPipe(pipe.get());
    }
    return true;
}

bool PipeServer::accepted(HANDLE pipe) {
    ULONG client_session = 0;
    if (!GetNamedPipeClientSessionId(pipe, &client_session) || client_session != session_id_) {
        log_.warning("Refused a client from another session");
        return false;
    }
    const auto sid = client_sid(pipe);
    if (!sid) {
        log_.warning("Refused a client that did not allow identification");
        return false;
    }
    if (*sid != user_sid_) {
        log_.warning("Refused a client running as " + sys::ascii(*sid));
        return false;
    }
    ULONG process_id = 0;
    const auto image = GetNamedPipeClientProcessId(pipe, &process_id)
                           ? sys::process_image(process_id)
                           : std::nullopt;
    if (!image) {
        log_.warning("Refused a client whose executable is unknown");
        return false;
    }
    for (const std::wstring& allowed : clients_) {
        if (sys::same_path(*image, allowed)) {
            return true;
        }
    }
    log_.warning("Refused a client run from " + sys::ascii(*image));
    return false;
}

struct PipeServer::Reading {
    sys::UniqueHandle done = sys::make_event(true);
    std::array<std::uint8_t, protocol::kMaxMessageSize> buffer{};
    OVERLAPPED overlapped{};
    bool pending = false;
};

void PipeServer::serve(HANDLE pipe) {
    Reading reading;
    while (true) {
        if (!reading.pending && !read_next(pipe, reading)) {
            return;
        }
        const std::array<HANDLE, 3> handles{reading.done.get(), reports_ready_, stop_};
        const DWORD signaled = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                      handles.data(), FALSE, INFINITE);
        bool going_on = false; // stopping, unless a read or reports arrived
        if (signaled == WAIT_OBJECT_0) {
            going_on = answer(pipe, reading);
        } else if (signaled == WAIT_OBJECT_0 + 1) {
            going_on = forward_reports(pipe);
        }
        if (!going_on) {
            if (reading.pending) {
                cancel(pipe, reading.overlapped);
            }
            return;
        }
    }
}

bool PipeServer::read_next(HANDLE pipe, Reading& reading) {
    reading.overlapped = OVERLAPPED{};
    reading.overlapped.hEvent = reading.done.get();
    ResetEvent(reading.done.get());
    DWORD received = 0;
    if (ReadFile(pipe, reading.buffer.data(), static_cast<DWORD>(reading.buffer.size()), &received,
                 &reading.overlapped) == FALSE) {
        const DWORD error = GetLastError();
        if (error == ERROR_MORE_DATA) {
            log_.warning("Refused a message larger than the protocol allows");
        }
        if (error != ERROR_IO_PENDING) {
            return false; // the client went, or broke the protocol
        }
    } else {
        SetEvent(reading.done.get()); // completed at once; answered like any other
    }
    reading.pending = true;
    return true;
}

bool PipeServer::answer(HANDLE pipe, Reading& reading) {
    reading.pending = false;
    DWORD received = 0;
    if (GetOverlappedResult(pipe, &reading.overlapped, &received, FALSE) == FALSE) {
        if (GetLastError() == ERROR_MORE_DATA) {
            log_.warning("Refused a message larger than the protocol allows");
        }
        return false;
    }
    const auto reply =
        session_.handle(std::span<const std::uint8_t>(reading.buffer.data(), received));
    return !reply || write(pipe, protocol::encode(*reply));
}

bool PipeServer::forward_reports(HANDLE pipe) {
    return std::ranges::all_of(reports_.take(), [&](const protocol::Report& report) {
        return write(pipe, protocol::encode(report)); // stops at the first that fails
    });
}

void PipeServer::cancel(HANDLE pipe, OVERLAPPED& overlapped) {
    // The kernel references `overlapped` until the cancellation completes.
    CancelIoEx(pipe, &overlapped);
    DWORD ignored = 0;
    GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
}

bool PipeServer::write(HANDLE pipe, const protocol::Bytes& bytes) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = written_.get();
    ResetEvent(written_.get());
    DWORD written = 0;
    if (WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &written, &overlapped)) {
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    // A client that stops reading is dropped rather than holding the hooks' queue up.
    if (WaitForSingleObject(written_.get(), kWriteTimeoutMilliseconds) != WAIT_OBJECT_0) {
        log_.warning("Dropped a client that stopped reading");
        cancel(pipe, overlapped);
        return false;
    }
    return GetOverlappedResult(pipe, &overlapped, &written, FALSE) != FALSE;
}

PipeServer::Wait PipeServer::wait_for(HANDLE pipe, OVERLAPPED& overlapped, DWORD& transferred) {
    const std::array<HANDLE, 2> handles{overlapped.hEvent, stop_};
    const DWORD signaled =
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
    if (signaled == WAIT_OBJECT_0) {
        return GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) ? Wait::Done
                                                                           : Wait::Failed;
    }
    cancel(pipe, overlapped);
    return signaled == WAIT_OBJECT_0 + 1 ? Wait::Stopped : Wait::Failed;
}

} // namespace osdrop::input
