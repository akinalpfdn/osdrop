#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

namespace osdrop {

/// Minimal stand-in for C++23 std::expected: a value or an error, never an exception.
template <typename T, typename E>
class Expected {
public:
    /// Implicit, like std::expected: `return value;` works for anything T can be built from,
    /// e.g. one alternative of a std::variant T.
    template <typename U>
        requires(std::is_constructible_v<T, U&&> &&
                 !std::is_same_v<std::remove_cvref_t<U>, Expected>)
    Expected(U&& value)
        : storage_(std::in_place_index<0>, std::forward<U>(value)) {} // NOLINT(*-explicit-*)
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

    [[nodiscard]] bool has_value() const noexcept { return state_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }
    /// Only valid when has_value() is false.
    [[nodiscard]] E error() const { return std::get<1>(state_); }

private:
    Status() = default;
    explicit Status(E error) : state_(std::in_place_index<1>, error) {}

    std::variant<std::monostate, E> state_;
};

} // namespace osdrop
