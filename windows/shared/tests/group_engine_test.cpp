// The Windows engine's group logic with fake DNS-SD and storage and real meshes on loopback: two
// computers find each other, pair into a group, store it and advertise it, and a restart keeps
// both the identity and the group.
#include <osdrop/win/group_engine.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using osdrop::win::DeviceIdBytes;
using osdrop::win::DiscoveredComputer;
using osdrop::win::GroupEngine;
using osdrop::win::StoredItem;
using Bytes = std::vector<std::uint8_t>;

osdrop_result fill(void* context, std::uint8_t* buffer, std::size_t length) {
    auto* state = static_cast<std::uint64_t*>(context);
    for (std::size_t i = 0; i < length; ++i) {
        *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
        buffer[i] = static_cast<std::uint8_t>(*state >> 33U); // NOLINT(*-pointer-arithmetic)
    }
    return OSDROP_OK;
}

class MemoryStore final : public osdrop::win::StateStore {
public:
    osdrop::win::Expected<std::optional<Bytes>, osdrop_result> load(StoredItem item) override {
        const std::scoped_lock lock(mutex_);
        if (const auto it = items_.find(item); it != items_.end()) {
            return std::optional<Bytes>(it->second);
        }
        return std::optional<Bytes>();
    }
    bool save(StoredItem item, std::span<const std::uint8_t> bytes) override {
        const std::scoped_lock lock(mutex_);
        items_[item] = Bytes(bytes.begin(), bytes.end());
        return true;
    }
    std::optional<Bytes> get(StoredItem item) {
        const std::scoped_lock lock(mutex_);
        const auto it = items_.find(item);
        return it == items_.end() ? std::nullopt : std::optional(it->second);
    }
    void put(StoredItem item, Bytes bytes) {
        const std::scoped_lock lock(mutex_);
        items_[item] = std::move(bytes);
    }

private:
    std::mutex mutex_;
    std::map<StoredItem, Bytes> items_;
};

class FakeDiscovery final : public osdrop::win::Discovery {
public:
    bool advertise(const osdrop_advertisement& advertisement, std::uint16_t port) override {
        const std::scoped_lock lock(mutex_);
        advertisements_.push_back(advertisement);
        port_ = port;
        return true;
    }
    bool browse(Callbacks callbacks) override {
        const std::scoped_lock lock(mutex_);
        callbacks_ = std::move(callbacks);
        return true;
    }
    void stop() override {
        const std::scoped_lock lock(mutex_);
        callbacks_ = {};
    }

    /// What DNS-SD would report for this computer, as another computer resolves it.
    std::optional<DiscoveredComputer> resolved() {
        const std::scoped_lock lock(mutex_);
        if (advertisements_.empty()) {
            return std::nullopt;
        }
        return DiscoveredComputer{.label = "computer",
                                  .advertisement = advertisements_.back(),
                                  .address = "127.0.0.1",
                                  .port = port_};
    }
    void report(const DiscoveredComputer& computer) {
        Callbacks callbacks;
        {
            const std::scoped_lock lock(mutex_);
            callbacks = callbacks_;
        }
        if (callbacks.found) {
            callbacks.found(computer);
        }
    }
    std::size_t advertisement_count() {
        const std::scoped_lock lock(mutex_);
        return advertisements_.size();
    }
    std::optional<osdrop_advertisement> last() {
        const std::scoped_lock lock(mutex_);
        return advertisements_.empty() ? std::nullopt : std::optional(advertisements_.back());
    }

private:
    std::mutex mutex_;
    std::vector<osdrop_advertisement> advertisements_;
    std::uint16_t port_ = 0;
    Callbacks callbacks_;
};

class RecordingEvents final : public osdrop::win::EngineEvents {
public:
    void group_changed() override {
        change([&] { ++group_changes_; });
    }
    void nearby_changed() override {
        change([&] { ++nearby_changes_; });
    }
    void left_group(osdrop_result reason) override {
        change([&] { left_.push_back(reason); });
    }
    void pairing_code(std::uint64_t connection, std::uint32_t code,
                      const osdrop_peer& /*peer*/) override {
        change([&] { codes_.emplace_back(connection, code); });
    }
    void pairing_failed(std::uint64_t /*connection*/, osdrop_result /*reason*/) override {}
    void peer_connected(const osdrop_peer& /*peer*/) override {
        change([&] { ++connected_; });
    }
    void peer_disconnected(const DeviceIdBytes& /*device*/, osdrop_result /*reason*/) override {}
    void peer_responsive(const DeviceIdBytes& /*device*/, bool /*responsive*/) override {}
    void stats(const DeviceIdBytes& /*device*/, const osdrop_session_stats& /*stats*/) override {}
    void log(osdrop_log_level /*level*/, const char* /*message*/) override {}
    void clipboard_notice(osdrop::win::ClipboardNotice /*notice*/, const DeviceIdBytes& /*device*/,
                          const std::string& /*name*/) override {}

    bool wait_for(const std::function<bool()>& condition) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(5), condition);
    }
    std::optional<std::pair<std::uint64_t, std::uint32_t>> code() {
        const std::scoped_lock lock(mutex_);
        return codes_.empty() ? std::nullopt : std::optional(codes_.back());
    }
    int connected() {
        const std::scoped_lock lock(mutex_);
        return connected_;
    }
    int connected_unlocked() const { return connected_; }
    std::size_t codes_unlocked() const { return codes_.size(); }

private:
    template <typename Change>
    void change(Change&& apply) {
        {
            const std::scoped_lock lock(mutex_);
            apply();
        }
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    int group_changes_ = 0;
    int nearby_changes_ = 0;
    int connected_ = 0;
    std::vector<osdrop_result> left_;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> codes_;
};

/// A clipboard thread that records what is written to the clipboard.
class ThreadHost final : public osdrop::win::ClipboardHost {
public:
    ThreadHost() : thread_([this] { run(); }) {}
    ~ThreadHost() override { stop(); }
    ThreadHost(const ThreadHost&) = delete;
    ThreadHost& operator=(const ThreadHost&) = delete;
    ThreadHost(ThreadHost&&) = delete;
    ThreadHost& operator=(ThreadHost&&) = delete;

    void post(std::function<void()> work) override {
        {
            const std::scoped_lock lock(mutex_);
            if (stopping_) {
                return;
            }
            queue_.push_back(std::move(work));
        }
        changed_.notify_all();
    }
    void write(const osdrop::win::ClipData& data) override {
        {
            const std::scoped_lock lock(mutex_);
            written_.push_back(data);
        }
        changed_.notify_all();
    }
    std::optional<std::string> make_staging_folder() override { return std::nullopt; }
    void clear_staging(const std::optional<std::string>& /*kept*/) override {}

    /// Ends the thread; work posted later is dropped. Before the engine goes.
    void stop() {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    std::optional<osdrop::win::ClipData> wait_for_write() {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, std::chrono::seconds(5), [&] { return !written_.empty(); });
        return written_.empty() ? std::nullopt : std::optional(written_.front());
    }

private:
    void run() {
        for (;;) {
            std::function<void()> work;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                if (stopping_) {
                    return;
                }
                work = std::move(queue_.front());
                queue_.pop_front();
            }
            work();
        }
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> queue_;
    std::vector<osdrop::win::ClipData> written_;
    bool stopping_ = false;
    std::thread thread_; // declared last: starts once the rest exists
};

/// One Windows computer with its own fakes.
struct Computer {
    explicit Computer(std::uint64_t seed, std::string name,
                      osdrop::win::ClipboardHost* clipboard_host = nullptr)
        : random_state(seed), clipboard(clipboard_host) {
        engine = start(std::move(name));
    }
    std::unique_ptr<GroupEngine> start(std::string name) {
        auto created = GroupEngine::create(
            {.device_name = std::move(name),
             .random = osdrop_random_source{.fill = fill, .context = &random_state},
             .clipboard = clipboard},
            store, discovery, events);
        EXPECT_TRUE(created.has_value());
        return created ? std::move(created.value()) : nullptr;
    }

    std::uint64_t random_state;
    osdrop::win::ClipboardHost* clipboard;
    MemoryStore store;
    FakeDiscovery discovery;
    RecordingEvents events;
    std::unique_ptr<GroupEngine> engine;
};

bool wait_until(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

TEST(GroupEngine, StartsWithANewIdentityAndAdvertisesIt) {
    Computer pc(1, "PC");
    ASSERT_NE(pc.engine, nullptr);
    const auto identity = pc.store.get(StoredItem::Identity);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->size(), OSDROP_KEY_SIZE);
    const auto info = pc.engine->info();
    EXPECT_NE(info.listen_port, 0);
    EXPECT_FALSE(info.has_group);
    const auto advertised = pc.discovery.last();
    ASSERT_TRUE(advertised.has_value());
    EXPECT_EQ(advertised->platform, OSDROP_PLATFORM_WINDOWS);
    EXPECT_EQ(advertised->has_group, 0);
    EXPECT_EQ(std::string(advertised->name), "PC");
    EXPECT_EQ(pc.store.get(StoredItem::ListenPort)->size(), 2U);
}

TEST(GroupEngine, TwoComputersFindEachOtherPairAndKeepTheGroup) {
    auto pc = std::make_unique<Computer>(2, "PC");
    Computer laptop(3, "Laptop");
    // Each one's DNS-SD reports the other.
    pc->discovery.report(*laptop.discovery.resolved());
    laptop.discovery.report(*pc->discovery.resolved());
    ASSERT_EQ(pc->engine->nearby().size(), 1U);
    EXPECT_EQ(pc->engine->nearby().front().name, "Laptop");

    const auto connection = pc->engine->pair(laptop.engine->info().device_id);
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pc->events.wait_for([&] { return pc->events.codes_unlocked() > 0; }));
    ASSERT_TRUE(laptop.events.wait_for([&] { return laptop.events.codes_unlocked() > 0; }));
    EXPECT_EQ(pc->events.code()->second, laptop.events.code()->second);
    EXPECT_EQ(pc->engine->confirm_pairing(connection.value(), true), OSDROP_OK);
    EXPECT_EQ(laptop.engine->confirm_pairing(laptop.events.code()->first, true), OSDROP_OK);
    ASSERT_TRUE(pc->events.wait_for([&] { return pc->events.connected_unlocked() == 1; }));
    ASSERT_TRUE(wait_until([&] {
        return pc->engine->info().has_group && laptop.engine->info().has_group &&
               pc->engine->members().value().size() == 2;
    }));
    EXPECT_EQ(pc->engine->info().group_id, laptop.engine->info().group_id);
    EXPECT_TRUE(pc->engine->nearby().empty()) << "members are not offered for pairing";
    EXPECT_EQ(pc->discovery.last()->has_group, 1) << "the group is advertised";
    ASSERT_TRUE(pc->store.get(StoredItem::GroupState).has_value());

    // A restart keeps the identity and the group.
    const auto device = pc->engine->info().device_id;
    const auto group = pc->engine->info().group_id;
    pc->engine.reset();
    pc->engine = pc->start("PC");
    ASSERT_NE(pc->engine, nullptr);
    EXPECT_EQ(pc->engine->info().device_id, device);
    EXPECT_TRUE(pc->engine->info().has_group);
    EXPECT_EQ(pc->engine->info().group_id, group);
}

TEST(GroupEngine, StoredStatesAreCheckedBeforeStarting) {
    {
        Computer damaged_layout(4, "PC");
        damaged_layout.engine.reset();
        damaged_layout.store.put(StoredItem::LayoutState, {0xFF, 0xFF});
        damaged_layout.engine = damaged_layout.start("PC");
        EXPECT_NE(damaged_layout.engine, nullptr) << "a damaged arrangement starts over";
    }
    MemoryStore store;
    FakeDiscovery discovery;
    RecordingEvents events;
    std::uint64_t random = 5;
    store.put(StoredItem::GroupState, {'O', 'D', 'G', 'S', 2});
    const auto newer = GroupEngine::create(
        {.device_name = "PC", .random = osdrop_random_source{.fill = fill, .context = &random}},
        store, discovery, events);
    ASSERT_FALSE(newer.has_value());
    EXPECT_EQ(newer.error(), OSDROP_ERROR_UNSUPPORTED_VERSION);
    EXPECT_EQ(store.get(StoredItem::GroupState)->size(), 5U) << "kept, not overwritten";

    store.put(StoredItem::GroupState, {});
    store.put(StoredItem::Identity, {1, 2, 3});
    const auto damaged = GroupEngine::create(
        {.device_name = "PC", .random = osdrop_random_source{.fill = fill, .context = &random}},
        store, discovery, events);
    ASSERT_FALSE(damaged.has_value());
    EXPECT_EQ(damaged.error(), OSDROP_ERROR_INVALID_KEY);
}

TEST(GroupEngine, ACopyOnOnePcIsWrittenToAnothersClipboard) {
    ThreadHost pc_clipboard;
    ThreadHost laptop_clipboard;
    {
        Computer pc(4, "PC", &pc_clipboard);
        Computer laptop(5, "Laptop", &laptop_clipboard);
        pc.discovery.report(*laptop.discovery.resolved());
        laptop.discovery.report(*pc.discovery.resolved());
        const auto connection = pc.engine->pair(laptop.engine->info().device_id);
        ASSERT_TRUE(connection.has_value());
        ASSERT_TRUE(laptop.events.wait_for([&] { return laptop.events.codes_unlocked() > 0; }));
        ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.codes_unlocked() > 0; }));
        EXPECT_EQ(pc.engine->confirm_pairing(connection.value(), true), OSDROP_OK);
        EXPECT_EQ(laptop.engine->confirm_pairing(laptop.events.code()->first, true), OSDROP_OK);
        ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.connected_unlocked() == 1; }));
        ASSERT_TRUE(
            laptop.events.wait_for([&] { return laptop.events.connected_unlocked() == 1; }));

        const std::string text = "copied on the PC";
        pc_clipboard.post([&] {
            pc.engine->clipboard()->copied({.contents = {{.format = OSDROP_CLIPBOARD_TEXT,
                                                          .bytes = {text.begin(), text.end()}}},
                                            .files = {}});
        });
        const auto written = laptop_clipboard.wait_for_write();
        ASSERT_TRUE(written.has_value());
        ASSERT_EQ(written->contents.size(), 1U);
        EXPECT_EQ(written->contents[0].format, OSDROP_CLIPBOARD_TEXT);
        EXPECT_EQ(std::string(written->contents[0].bytes.begin(), written->contents[0].bytes.end()),
                  text);
        // The clipboard threads stop before the engines go.
        pc_clipboard.stop();
        laptop_clipboard.stop();
    }
}

} // namespace
