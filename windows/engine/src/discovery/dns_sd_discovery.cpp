#include "discovery/dns_sd_discovery.hpp"

#include <osdrop/win/txt_mapping.hpp>

#include <windows.h>

#include <windns.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace osdrop::engine {
namespace {

constexpr const wchar_t* kBrowseName = L"_osdrop._tcp.local";
constexpr const wchar_t* kServiceSuffix = L"._osdrop._tcp.local";

std::string utf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.pop_back(); // the NUL
    return result;
}

std::wstring wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        length);
    return result;
}

/// IPv4 first: core listens on both families, and IPv4 avoids scope IDs.
std::string address_of(const DNS_SERVICE_INSTANCE& instance) {
    if (instance.ip4Address != nullptr) {
        std::array<std::uint8_t, 4> bytes{};
        std::memcpy(bytes.data(), instance.ip4Address, bytes.size()); // network order
        return std::format("{}.{}.{}.{}", bytes[0], bytes[1], bytes[2], bytes[3]);
    }
    if (instance.ip6Address != nullptr) {
        std::array<std::uint8_t, 16> bytes{};
        std::memcpy(bytes.data(), instance.ip6Address, bytes.size());
        std::string text;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            text += std::format("{}{:x}", i == 0 ? "" : ":", (bytes.at(i) << 8U) | bytes.at(i + 1));
        }
        return text;
    }
    return utf8(instance.pszHostName); // Windows resolves .local names itself
}

} // namespace

// --- Browser -------------------------------------------------------------------------------------

class DnsSdBrowser {
public:
    struct State;

    /// Callbacks run on system thread pool threads, one at a time.
    static std::unique_ptr<DnsSdBrowser> start(win::Discovery::Callbacks callbacks);
    ~DnsSdBrowser();
    DnsSdBrowser(const DnsSdBrowser&) = delete;
    DnsSdBrowser& operator=(const DnsSdBrowser&) = delete;
    DnsSdBrowser(DnsSdBrowser&&) = delete;
    DnsSdBrowser& operator=(DnsSdBrowser&&) = delete;

private:
    explicit DnsSdBrowser(State* state) : state_(state) {}
    void stop();
    State* state_; // outlives this object, see stop()
};

struct DnsSdBrowser::State {
    struct Resolve {
        State* state = nullptr;
        std::wstring instance;
        DNS_SERVICE_CANCEL cancel{};
    };

    std::mutex mutex;
    std::condition_variable idle;
    bool closed = false;
    int running = 0; ///< engine callbacks in progress; stop() waits for none
    win::Discovery::Callbacks callbacks;
    DNS_SERVICE_CANCEL browse_cancel{};
    /// By instance name; shared so a request that completes at once cannot free it under us.
    std::map<std::wstring, std::shared_ptr<Resolve>> resolving;
    std::map<std::wstring, std::string> found; ///< instance name → label

    static VOID WINAPI on_browse(DWORD status, PVOID context, PDNS_RECORD records) {
        auto* state = static_cast<State*>(context);
        if (status == ERROR_SUCCESS) {
            for (const DNS_RECORD* record = records; record != nullptr; record = record->pNext) {
                if (record->wType != DNS_TYPE_PTR) {
                    continue;
                }
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): wType says PTR
                const wchar_t* instance = record->Data.PTR.pNameHost;
                if (instance != nullptr) {
                    state->on_pointer(instance, record->dwTtl == 0);
                }
            }
        }
        if (records != nullptr) {
            DnsRecordListFree(records, DnsFreeRecordList);
        }
    }

    static VOID WINAPI on_resolved(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance) {
        auto* resolve = static_cast<Resolve*>(context);
        resolve->state->finish(*resolve, status == ERROR_SUCCESS ? instance : nullptr);
        if (instance != nullptr) {
            DnsServiceFreeInstance(instance);
        }
    }

    void on_pointer(const wchar_t* instance_name, bool goodbye) {
        std::function<void(const std::string&)> lost;
        std::string label;
        std::shared_ptr<Resolve> resolve;
        {
            const std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            const std::wstring name(instance_name);
            if (goodbye) {
                if (const auto it = found.find(name); it != found.end()) {
                    label = it->second;
                    found.erase(it);
                    lost = callbacks.lost;
                    ++running;
                }
            } else if (!resolving.contains(name)) {
                // Also for a computer found before: its record may have changed (its group).
                resolve = std::make_shared<Resolve>();
                resolve->state = this;
                resolve->instance = name;
                resolving.emplace(name, resolve);
            }
        }
        if (lost) {
            lost(label);
            done_calling();
        }
        if (resolve) {
            start_resolve(resolve); // outside the lock: it may complete before returning
        }
    }

    void start_resolve(const std::shared_ptr<Resolve>& resolve) {
        DNS_SERVICE_RESOLVE_REQUEST request{};
        request.Version = DNS_QUERY_REQUEST_VERSION1;
        request.InterfaceIndex = 0;
        request.QueryName = resolve->instance.data();
        request.pResolveCompletionCallback = on_resolved;
        request.pQueryContext = resolve.get();
        if (DnsServiceResolve(&request, &resolve->cancel) != DNS_REQUEST_PENDING) {
            const std::scoped_lock lock(mutex);
            if (!closed) {
                resolving.erase(resolve->instance);
            }
        }
    }

    void finish(const Resolve& resolve, const DNS_SERVICE_INSTANCE* instance) {
        std::function<void(const win::DiscoveredComputer&)> found_callback;
        win::DiscoveredComputer computer;
        {
            const std::scoped_lock lock(mutex);
            if (closed) {
                return; // the resolve object stays with the abandoned state
            }
            const std::wstring name = resolve.instance; // copied: erasing may free `resolve`
            resolving.erase(name);
            if (instance == nullptr) {
                return;
            }
            win::TxtPairs pairs;
            const std::span keys(instance->keys, instance->dwPropertyCount);
            const std::span values(instance->values, instance->dwPropertyCount);
            for (DWORD i = 0; i < instance->dwPropertyCount; ++i) {
                pairs.emplace_back(utf8(keys[i]), utf8(values[i]));
            }
            const osdrop_txt_record record = win::txt_record(pairs);
            const auto label = win::label_of(utf8(name.c_str()));
            if (!label ||
                osdrop_advertisement_from_txt(&record, &computer.advertisement) != OSDROP_OK) {
                return; // not an Osdrop record this version understands
            }
            computer.label = *label;
            computer.address = address_of(*instance);
            computer.port = instance->wPort;
            if (computer.address.empty()) {
                return;
            }
            found[name] = computer.label;
            found_callback = callbacks.found;
            ++running;
        }
        found_callback(computer);
        done_calling();
    }

    void done_calling() {
        {
            const std::scoped_lock lock(mutex);
            --running;
        }
        idle.notify_all();
    }
};

std::unique_ptr<DnsSdBrowser> DnsSdBrowser::start(win::Discovery::Callbacks callbacks) {
    auto state = std::make_unique<State>();
    state->callbacks = std::move(callbacks);
    DNS_SERVICE_BROWSE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = kBrowseName;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): Win32 API
    request.pBrowseCallback = State::on_browse;
    request.pQueryContext = state.get();
    if (DnsServiceBrowse(&request, &state->browse_cancel) != DNS_REQUEST_PENDING) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): private constructor
    return std::unique_ptr<DnsSdBrowser>(new DnsSdBrowser(state.release()));
}

DnsSdBrowser::~DnsSdBrowser() {
    try {
        stop();
    } catch (...) {
        std::terminate(); // locking failed: callbacks could outlive the engine
    }
}

void DnsSdBrowser::stop() {
    // DNS-SD may still call back after a cancel, so the state is closed and left alive for the
    // rest of the process instead of freed (a few hundred bytes per browse).
    std::vector<std::shared_ptr<State::Resolve>> pending;
    {
        std::unique_lock lock(state_->mutex);
        state_->closed = true;
        for (const auto& [name, resolve] : state_->resolving) {
            pending.push_back(resolve);
        }
        state_->idle.wait(lock, [this] { return state_->running == 0; });
    }
    // Outside the lock: a cancel may call back at once, and the callback takes the lock.
    DnsServiceBrowseCancel(&state_->browse_cancel);
    for (const auto& resolve : pending) {
        DnsServiceResolveCancel(&resolve->cancel);
    }
}

// --- Advertiser ----------------------------------------------------------------------------------

class DnsSdAdvertiser {
public:
    struct State {
        DNS_SERVICE_REGISTER_REQUEST request{};
        PDNS_SERVICE_INSTANCE instance = nullptr;
        DNS_SERVICE_CANCEL cancel{};

        static VOID WINAPI on_registered(DWORD /*status*/, PVOID /*context*/,
                                         PDNS_SERVICE_INSTANCE instance) {
            if (instance != nullptr) {
                DnsServiceFreeInstance(instance);
            }
        }
    };

    static std::unique_ptr<DnsSdAdvertiser> start(const osdrop_advertisement& advertisement,
                                                  std::uint16_t port);
    /// Deregistration completes asynchronously through the same request, so the state stays
    /// alive for the rest of the process.
    ~DnsSdAdvertiser() { DnsServiceDeRegister(&state_->request, nullptr); }
    DnsSdAdvertiser(const DnsSdAdvertiser&) = delete;
    DnsSdAdvertiser& operator=(const DnsSdAdvertiser&) = delete;
    DnsSdAdvertiser(DnsSdAdvertiser&&) = delete;
    DnsSdAdvertiser& operator=(DnsSdAdvertiser&&) = delete;

private:
    explicit DnsSdAdvertiser(State* state) : state_(state) {}
    State* state_;
};

std::unique_ptr<DnsSdAdvertiser> DnsSdAdvertiser::start(const osdrop_advertisement& advertisement,
                                                        std::uint16_t port) {
    osdrop_txt_record record{};
    if (osdrop_advertisement_to_txt(&advertisement, &record) != OSDROP_OK) {
        return nullptr;
    }
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 64> computer{};
    auto computer_length = static_cast<DWORD>(computer.size());
    if (GetComputerNameExW(ComputerNameDnsHostname, computer.data(), &computer_length) == FALSE) {
        return nullptr;
    }
    const std::wstring host = std::wstring(computer.data(), computer_length) + L".local";
    const std::wstring service = wide(win::instance_label(record)) + kServiceSuffix;
    std::vector<std::wstring> keys;
    std::vector<std::wstring> values;
    for (const auto& [key, value] : win::txt_pairs(record)) {
        keys.push_back(wide(key));
        values.push_back(wide(value));
    }
    std::vector<PCWSTR> key_pointers;
    std::vector<PCWSTR> value_pointers;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        key_pointers.push_back(keys[i].c_str());
        value_pointers.push_back(values[i].c_str());
    }
    auto state = std::make_unique<State>();
    // The system answers address queries for its own host name.
    state->instance = DnsServiceConstructInstance(service.c_str(), host.c_str(), nullptr, nullptr,
                                                  port, 0, 0, static_cast<DWORD>(keys.size()),
                                                  key_pointers.data(), value_pointers.data());
    if (state->instance == nullptr) {
        return nullptr;
    }
    // Owned by the request from here on; DNS-SD keeps using it until deregistration completes.
    state->request.Version = DNS_QUERY_REQUEST_VERSION1;
    state->request.InterfaceIndex = 0;
    state->request.pServiceInstance = state->instance;
    state->request.pRegisterCompletionCallback = State::on_registered;
    state->request.pQueryContext = state.get();
    state->request.hCredentials = nullptr;
    state->request.unicastEnabled = FALSE;
    if (DnsServiceRegister(&state->request, &state->cancel) != DNS_REQUEST_PENDING) {
        DnsServiceFreeInstance(state->instance);
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): private constructor
    return std::unique_ptr<DnsSdAdvertiser>(new DnsSdAdvertiser(state.release()));
}

// --- Discovery -----------------------------------------------------------------------------------

DnsSdDiscovery::DnsSdDiscovery() = default;

DnsSdDiscovery::~DnsSdDiscovery() {
    try {
        stop();
    } catch (...) {
        std::terminate(); // locking failed: DNS-SD could call into a destroyed engine
    }
}

bool DnsSdDiscovery::advertise(const osdrop_advertisement& advertisement, std::uint16_t port) {
    const std::scoped_lock lock(mutex_);
    advertiser_.reset(); // the old record goes first, so browsers see the new one as a change
    advertiser_ = DnsSdAdvertiser::start(advertisement, port);
    return advertiser_ != nullptr;
}

void DnsSdDiscovery::withdraw() {
    const std::scoped_lock lock(mutex_);
    advertiser_.reset();
}

bool DnsSdDiscovery::browse(Callbacks callbacks) {
    const std::scoped_lock lock(mutex_);
    browser_.reset();
    browser_ = DnsSdBrowser::start(std::move(callbacks));
    return browser_ != nullptr;
}

void DnsSdDiscovery::stop() {
    const std::scoped_lock lock(mutex_);
    browser_.reset();
    advertiser_.reset();
}

} // namespace osdrop::engine
