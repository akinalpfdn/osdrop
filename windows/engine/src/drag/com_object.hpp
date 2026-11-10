#pragma once

// A minimal COM object: reference counting and QueryInterface for the interfaces it lists, and an
// owning pointer to an interface. The drag bridge's objects live on one STA thread each.

#include <windows.h>

#include <unknwn.h>

#include <atomic>
#include <utility>

namespace osdrop::engine::drag {

/// Owns one reference to a COM interface.
template <typename Interface>
class ComPtr {
public:
    ComPtr() = default;
    /// Takes over a reference the caller already holds.
    explicit ComPtr(Interface* pointer) : pointer_(pointer) {}
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr& other) : pointer_(other.pointer_) {
        if (pointer_ != nullptr) {
            pointer_->AddRef();
        }
    }
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] Interface* get() const { return pointer_; }
    Interface* operator->() const { return pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }
    /// For functions that hand out a new reference.
    Interface** put() {
        reset();
        return &pointer_;
    }
    /// Hands the reference to the caller (an out parameter).
    [[nodiscard]] Interface* detach() { return std::exchange(pointer_, nullptr); }
    void reset() {
        if (pointer_ != nullptr) {
            std::exchange(pointer_, nullptr)->Release();
        }
    }

private:
    Interface* pointer_ = nullptr;
};

/// IUnknown for a class implementing `Interfaces`; it starts with one reference, which the
/// creator owns (wrap it in a ComPtr).
template <typename... Interfaces>
class ComObject : public Interfaces... {
public:
    ComObject() = default;
    virtual ~ComObject() = default;
    ComObject(const ComObject&) = delete;
    ComObject& operator=(const ComObject&) = delete;
    ComObject(ComObject&&) = delete;
    ComObject& operator=(ComObject&&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown)) {
            *out = static_cast<IUnknown*>(static_cast<First*>(this));
        } else {
            (find<Interfaces>(id, out) || ...);
        }
        if (*out == nullptr) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (left == 0) {
            delete this; // NOLINT(cppcoreguidelines-owning-memory): COM's own lifetime rule
        }
        return left;
    }

private:
    template <typename T, typename...>
    struct Front {
        using Type = T;
    };
    using First = typename Front<Interfaces...>::Type;

    template <typename Interface>
    bool find(REFIID id, void** out) {
        if (IsEqualIID(id, __uuidof(Interface))) {
            *out = static_cast<Interface*>(this);
            return true;
        }
        return false;
    }

    std::atomic<ULONG> references_{1};
};

} // namespace osdrop::engine::drag
