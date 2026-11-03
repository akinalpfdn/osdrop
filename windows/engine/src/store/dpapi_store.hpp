#pragma once

// Files under %APPDATA%\Osdrop: the identity and the group state sealed with DPAPI for this user,
// the arrangement and the listening port as they are (DEVPLAN, state and persistence).

#include <osdrop/win/group_engine.hpp>

#include <mutex>

namespace osdrop::engine {

class DpapiStore final : public win::StateStore {
public:
    [[nodiscard]] win::Expected<std::optional<std::vector<std::uint8_t>>, osdrop_result>
    load(win::StoredItem item) override;
    [[nodiscard]] bool save(win::StoredItem item, std::span<const std::uint8_t> bytes) override;

private:
    std::mutex mutex_;
};

} // namespace osdrop::engine
