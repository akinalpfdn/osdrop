// Dial on loopback: a peer is reached through its next address when one drops packets silently
// (as a firewall does; simulated, since nothing on loopback stays silent) or refuses, a dial fails
// only when every address has, and addresses are tried with their families alternating.
#include "transport/dial.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <utility>
#include <vector>

namespace {

using asio::ip::tcp;
using osdrop::transport::Dial;
using osdrop::transport::dial_order;
using std::chrono::milliseconds;
using SteadyClock = std::chrono::steady_clock;

constexpr auto kRunLimit = std::chrono::seconds(6);

/// A loopback port nothing listens on: connecting to it is refused.
tcp::endpoint closed_port(asio::io_context& io) {
    tcp::acceptor acceptor(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
    return acceptor.local_endpoint();
}

/// Connects for real, except to `silent`, which never answers.
Dial::Connect dropping(const tcp::endpoint& silent) {
    return [silent](tcp::socket& socket, const tcp::endpoint& endpoint,
                    std::function<void(const asio::error_code&)> handler) {
        if (endpoint != silent) {
            socket.async_connect(endpoint, std::move(handler));
        }
    };
}

struct Outcome {
    bool done = false;
    asio::error_code error;
    tcp::endpoint remote;
    milliseconds elapsed{0};
};

Outcome dial(asio::io_context& io, std::vector<tcp::endpoint> endpoints, milliseconds stagger,
             Dial::Connect connect = {}) {
    Outcome outcome;
    const auto started = SteadyClock::now();
    const auto running = Dial::start(
        io.get_executor(), std::move(endpoints),
        [&outcome, started](const asio::error_code& error, const tcp::socket& socket) {
            outcome.done = true;
            outcome.error = error;
            outcome.elapsed =
                std::chrono::duration_cast<milliseconds>(SteadyClock::now() - started);
            asio::error_code ignored;
            outcome.remote = socket.remote_endpoint(ignored);
        },
        stagger, std::move(connect));
    io.run_for(kRunLimit);
    return outcome;
}

TEST(Dial, ReachesThePeerThroughItsNextAddressWhenOneDropsPacketsSilently) {
    asio::io_context io;
    const tcp::acceptor live(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
    const tcp::endpoint silent(asio::ip::make_address("fe80::1"), live.local_endpoint().port());

    const Outcome outcome =
        dial(io, {silent, live.local_endpoint()}, milliseconds(250), dropping(silent));

    ASSERT_TRUE(outcome.done);
    EXPECT_FALSE(outcome.error) << outcome.error.message();
    EXPECT_EQ(outcome.remote, live.local_endpoint());
    EXPECT_GE(outcome.elapsed, milliseconds(250)) << "the next address waits for the stagger";
    EXPECT_LT(outcome.elapsed, milliseconds(2000)) << "not for the silent one's timeout";
}

TEST(Dial, TriesTheNextAddressAtOnceWhenOneRefuses) {
    asio::io_context io;
    const tcp::acceptor live(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));

    const Outcome outcome =
        dial(io, {closed_port(io), live.local_endpoint()}, milliseconds(10'000));

    ASSERT_TRUE(outcome.done);
    EXPECT_FALSE(outcome.error) << outcome.error.message();
    EXPECT_EQ(outcome.remote, live.local_endpoint());
}

TEST(Dial, FailsWithTheLastErrorOnlyWhenEveryAddressHasFailed) {
    asio::io_context io;

    const Outcome outcome = dial(io, {closed_port(io), closed_port(io)}, milliseconds(10'000));

    ASSERT_TRUE(outcome.done);
    EXPECT_EQ(outcome.error, asio::error::connection_refused);
}

TEST(Dial, ACancelledDialReportsNothing) {
    asio::io_context io;
    const tcp::acceptor live(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
    const tcp::endpoint silent(asio::ip::make_address("fe80::1"), live.local_endpoint().port());
    bool done = false;

    const auto running = Dial::start(
        io.get_executor(), {silent, live.local_endpoint()},
        [&done](const asio::error_code& /*error*/, const tcp::socket& /*socket*/) { done = true; },
        milliseconds(50), dropping(silent));
    running->cancel();
    io.run_for(milliseconds(500));

    EXPECT_FALSE(done);
}

TEST(Dial, TriesAddressFamiliesInTurnStartingWithTheResolversFirst) {
    const tcp::endpoint v6a(asio::ip::make_address("fe80::1"), 1);
    const tcp::endpoint v6b(asio::ip::make_address("fd00::1"), 1);
    const tcp::endpoint v4a(asio::ip::make_address("192.168.1.10"), 1);
    const tcp::endpoint v4b(asio::ip::make_address("10.0.0.1"), 1);

    EXPECT_EQ(dial_order({v6a, v6b, v4a, v4b}), (std::vector{v6a, v4a, v6b, v4b}));
    EXPECT_EQ(dial_order({v4a, v6a, v6b}), (std::vector{v4a, v6a, v6b}));
    EXPECT_TRUE(dial_order({}).empty());
}

} // namespace
