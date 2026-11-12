#include "transport/dial.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <asio/post.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <utility>

namespace osdrop::transport {

using asio::ip::tcp;

namespace {

void connect_socket(tcp::socket& socket, const tcp::endpoint& endpoint,
                    std::function<void(const asio::error_code&)> handler) {
    socket.async_connect(endpoint, std::move(handler));
}

} // namespace

std::vector<tcp::endpoint> dial_order(const std::vector<tcp::endpoint>& endpoints) {
    if (endpoints.empty()) {
        return {};
    }
    const tcp preferred = endpoints.front().protocol();
    std::vector<tcp::endpoint> first;
    std::vector<tcp::endpoint> second;
    for (const tcp::endpoint& endpoint : endpoints) {
        (endpoint.protocol() == preferred ? first : second).push_back(endpoint);
    }
    std::vector<tcp::endpoint> ordered;
    ordered.reserve(endpoints.size());
    for (std::size_t i = 0; i < std::max(first.size(), second.size()); ++i) {
        if (i < first.size()) {
            ordered.push_back(first[i]);
        }
        if (i < second.size()) {
            ordered.push_back(second[i]);
        }
    }
    return ordered;
}

std::shared_ptr<Dial> Dial::start(const asio::io_context::executor_type& executor,
                                  std::vector<tcp::endpoint> endpoints, Done done,
                                  std::chrono::milliseconds stagger, Connect connect) {
    auto dial = std::make_shared<Dial>(Token{}, executor, std::move(endpoints), std::move(done),
                                       stagger, std::move(connect));
    if (dial->endpoints_.empty()) {
        // Never back into the caller from inside its own call.
        asio::post(executor, [dial] {
            if (!dial->finished_) {
                dial->finish(asio::error::host_not_found, tcp::socket(dial->executor_));
            }
        });
    } else {
        dial->attempt_next();
    }
    return dial;
}

Dial::Dial(Token /*token*/, const asio::io_context::executor_type& executor,
           std::vector<tcp::endpoint> endpoints, Done done, std::chrono::milliseconds stagger,
           Connect connect)
    : executor_(executor), endpoints_(std::move(endpoints)), done_(std::move(done)),
      stagger_(stagger), connect_(connect ? std::move(connect) : Connect(connect_socket)),
      stagger_timer_(executor) {}

void Dial::cancel() {
    finished_ = true;
    close_attempts();
    done_ = nullptr;
}

void Dial::attempt_next() {
    if (finished_ || attempts_.size() == endpoints_.size()) {
        return;
    }
    const std::size_t index = attempts_.size();
    attempts_.emplace_back(executor_);
    connect_(attempts_.back(), endpoints_[index],
             [self = shared_from_this(), index](const asio::error_code& error) {
                 self->attempt_finished(index, error);
             });
    if (attempts_.size() < endpoints_.size()) {
        stagger_timer_.expires_after(stagger_); // replaces the wait an earlier attempt started
        stagger_timer_.async_wait([self = shared_from_this()](const asio::error_code& error) {
            if (!error) {
                self->attempt_next();
            }
        });
    }
}

void Dial::attempt_finished(std::size_t index, const asio::error_code& error) {
    if (finished_) {
        return; // cancelled, or another attempt won and closed this one
    }
    if (!error) {
        tcp::socket connected = std::move(attempts_[index]);
        finish(error, std::move(connected));
        return;
    }
    ++failed_;
    if (failed_ == endpoints_.size()) {
        finish(error, tcp::socket(executor_));
        return;
    }
    attempt_next(); // a failure does not wait for the stagger
}

void Dial::finish(const asio::error_code& error, tcp::socket socket) {
    finished_ = true;
    close_attempts();
    const Done done = std::move(done_);
    done_ = nullptr;
    done(error, std::move(socket));
}

void Dial::close_attempts() {
    stagger_timer_.cancel();
    for (tcp::socket& attempt : attempts_) {
        asio::error_code ignored;
        attempt.close(ignored);
    }
}

} // namespace osdrop::transport
