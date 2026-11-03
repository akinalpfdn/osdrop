#pragma once

// Bonjour on Windows through the system DNS-SD API (DnsServiceRegister / Browse / Resolve):
// advertises this PC's listening port and finds other Osdrop computers on the LAN.

#include <osdrop/win/group_engine.hpp>

#include <memory>
#include <mutex>

namespace osdrop::engine {

class DnsSdBrowser;
class DnsSdAdvertiser;

class DnsSdDiscovery final : public win::Discovery {
public:
    DnsSdDiscovery();
    ~DnsSdDiscovery() override;
    DnsSdDiscovery(const DnsSdDiscovery&) = delete;
    DnsSdDiscovery& operator=(const DnsSdDiscovery&) = delete;
    DnsSdDiscovery(DnsSdDiscovery&&) = delete;
    DnsSdDiscovery& operator=(DnsSdDiscovery&&) = delete;

    bool advertise(const osdrop_advertisement& advertisement, std::uint16_t port) override;
    bool browse(Callbacks callbacks) override;
    void stop() override;

private:
    std::mutex mutex_;
    std::unique_ptr<DnsSdBrowser> browser_;
    std::unique_ptr<DnsSdAdvertiser> advertiser_;
};

} // namespace osdrop::engine
