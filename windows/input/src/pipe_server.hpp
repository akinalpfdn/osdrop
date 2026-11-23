#pragma once

// OsdropInput's end of the input pipe (DEVPLAN Phase 3): one pipe for this session, readable and
// writable only by SYSTEM and the session's user, one client at a time. A client is accepted only
// when it runs in this session as its user, from Osdrop's own executables next to OsdropInput; it
// hears Accepted first, or TurnedAway with the reason before the pipe closes. Requests go to the
// session; this PC's input goes back as it arrives, and a capture whose client stops confirming it
// ends here.

#include <osdrop/sys/file_log.hpp>
#include <osdrop/sys/handles.hpp>
#include <osdrop/win/input_session.hpp>
#include <osdrop/win/refusal_tally.hpp>
#include <osdrop/win/report_queue.hpp>

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osdrop::input {

class PipeServer {
public:
    PipeServer(win::input::InputSession& session, win::input::ReportQueue& reports,
               HANDLE reports_ready, sys::FileLog& log, HANDLE stop);
    ~PipeServer() = default;
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;
    PipeServer(PipeServer&&) = delete;
    PipeServer& operator=(PipeServer&&) = delete;

    /// Serves clients until `stop` is set; false when the pipe could not be created.
    [[nodiscard]] bool run();

private:
    enum class Wait : std::uint8_t { Done, Failed, Stopped };
    struct Reading;
    struct Refusal {
        win::input::TurnAway reason = win::input::TurnAway::OtherCopy;
        std::string who; ///< for the log: "a client run from C:\..."
    };

    /// Why the client on the pipe may not connect; nullopt when it may.
    [[nodiscard]] std::optional<Refusal> refusal(HANDLE pipe);
    /// Tells a refused client why, and waits up to kTurnAwayLinger for it to close its end.
    void turn_away(HANDLE pipe, win::input::TurnAway reason);
    void serve(HANDLE pipe);
    /// Waits for a request, reports or the capture's deadline and handles what came; false when
    /// the client went or the server stops.
    [[nodiscard]] bool step(HANDLE pipe, Reading& reading);
    /// Until the capture's deadline; INFINITE without a capture.
    [[nodiscard]] DWORD wait_time() const;
    /// Starts reading the next request; false when the client went.
    [[nodiscard]] bool read_next(HANDLE pipe, Reading& reading);
    /// The request that arrived, carried out; false when the client went.
    [[nodiscard]] bool answer(HANDLE pipe, Reading& reading);
    /// Captured input sent on; false when the client went.
    [[nodiscard]] bool forward_reports(HANDLE pipe);
    static void cancel(HANDLE pipe, OVERLAPPED& overlapped);
    [[nodiscard]] bool write(HANDLE pipe, const win::input::Bytes& bytes);
    Wait wait_for(HANDLE pipe, OVERLAPPED& overlapped, DWORD& transferred);

    win::input::InputSession& session_;
    win::input::ReportQueue& reports_;
    HANDLE reports_ready_;
    sys::FileLog& log_;
    HANDLE stop_;
    DWORD session_id_;
    std::wstring user_sid_;
    std::vector<std::wstring> clients_; // executables that may connect
    win::input::RefusalTally refusals_; // a client that keeps trying is logged once in a while
    sys::UniqueHandle written_;
};

} // namespace osdrop::input
