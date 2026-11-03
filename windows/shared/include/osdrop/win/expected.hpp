#pragma once

#include <utility>
#include <variant>

namespace osdrop::win {

/// Minimal stand-in for C++23 std::expected: a value or an error, never an exception.
template <typename T, typename E>
class Expected {
public:
    Expected(T value)
        : storage_(std::in_place_index<0>, std::move(value)) {
    } // NOLINT(google-explicit-constructor)
    static Expected failure(E error) { return Expected(std::in_place_index<1>, std::move(error)); }

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] T& value() & { return std::get<0>(storage_); }
    [[nodiscard]] const E& error() const& { return std::get<1>(storage_); }

private:
    template <std::size_t Index, typename U>
    Expected(std::in_place_index_t<Index> index, U&& payload)
        : storage_(index, std::forward<U>(payload)) {}

    std::variant<T, E> storage_;
};

/// Result of an operation that produces no value.
template <typename E>
class Status {
public:
    static Status ok() { return Status(); }
    static Status failure(E error) { return Status(error); }

    [[nodiscard]] bool has_value() const noexcept { return !failed_; }
    explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] E error() const noexcept { return error_; }

private:
    Status() = default;
    explicit Status(E error) : error_(error), failed_(true) {}

    E error_{};
    bool failed_ = false;
};

} // namespace osdrop::win
