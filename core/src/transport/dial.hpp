#pragma once

// Dialling a peer that has several addresses (RFC 8305, "Happy Eyeballs"). A Bonjour host name
// resolves to every address the peer has, a link-local IPv6 one often first; a peer whose firewall
// drops one of them silently is still reached through the others, well within the connect timeout.

// CMake includes Asio as a system header; SwiftPM cannot, so its warnings are silenced here.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace osdrop::transport {

/// RFC 8305's recommended Connection Attempt Delay.
inline constexpr std::chrono::milliseconds kDialStagger{250};

/// The order addresses are tried in: the resolver's, with the address families alternating from
/// the family of its first address, so a family that does not work costs one attempt.
[[nodiscard]] std::vector<asio::ip::tcp::endpoint>
dial_order(const std::vector<asio::ip::tcp::endpoint>& endpoints);

/// Connects to the first address that answers. Each address gets its own attempt; the next one
/// starts when an attempt fails, or after the stagger without an answer. The first attempt to
/// connect wins and the others are closed; the dial fails only when every attempt has, with the
/// last error. Runs on its io_context's thread.
class Dial : public std::enable_shared_from_this<Dial> {
    struct Token {};

public:
    using Done = std::function<void(const asio::error_code&, asio::ip::tcp::socket)>;
    /// Connects `socket` to an address and reports once; tests replace it.
    using Connect =
        std::function<void(asio::ip::tcp::socket& socket, const asio::ip::tcp::endpoint& endpoint,
                           std::function<void(const asio::error_code&)> handler)>;

    /// `done` runs once, from a handler, unless the dial is cancelled first.
    [[nodiscard]] static std::shared_ptr<Dial>
    start(const asio::io_context::executor_type& executor,
          std::vector<asio::ip::tcp::endpoint> endpoints, Done done,
          std::chrono::milliseconds stagger = kDialStagger, Connect connect = {});

    Dial(Token token, const asio::io_context::executor_type& executor,
         std::vector<asio::ip::tcp::endpoint> endpoints, Done done,
         std::chrono::milliseconds stagger, Connect connect);

    /// Closes every attempt; `done` does not run.
    void cancel();

private:
    void attempt_next();
    void attempt_finished(std::size_t index, const asio::error_code& error);
    void finish(const asio::error_code& error, asio::ip::tcp::socket socket);
    void close_attempts();

    asio::io_context::executor_type executor_;
    std::vector<asio::ip::tcp::endpoint> endpoints_;
    Done done_;
    std::chrono::milliseconds stagger_;
    Connect connect_;
    asio::steady_timer stagger_timer_;
    /// One per address started; a deque keeps them in place for the handlers.
    std::deque<asio::ip::tcp::socket> attempts_;
    std::size_t failed_ = 0;
    bool finished_ = false;
};

} // namespace osdrop::transport
