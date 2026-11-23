// The JNI bridge between the Kotlin app and core's C API (docs/ARCHITECTURE.md, "Android").
//
// Text crosses as UTF-8 byte arrays both ways: JNI's own string calls use modified UTF-8, which
// mangles emoji in device and file names. Core calls the listener on its I/O thread, which is
// attached to the JVM on its first callback and detached when it ends; the listener must not call
// back into the mesh from there.
#include <osdrop/osdrop.h>

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <sys/random.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr const char* kLogTag = "OsdropCore";

JavaVM* g_vm = nullptr;
pthread_key_t g_detach_key;

JNIEnv* attached_env() {
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return nullptr;
    }
    pthread_setspecific(g_detach_key, env);
    return env;
}

void detach_thread(void* /*env*/) {
    g_vm->DetachCurrentThread();
}

osdrop_result fill_random(void* /*context*/, uint8_t* buffer, size_t length) {
    size_t done = 0;
    while (done < length) {
        const ssize_t got = getrandom(buffer + done, length - done, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return OSDROP_ERROR_RANDOM_UNAVAILABLE;
        }
        done += static_cast<size_t>(got);
    }
    return OSDROP_OK;
}

constexpr osdrop_random_source kRandom{.fill = fill_random, .context = nullptr};

void clear_exception(JNIEnv* env, const char* where) {
    if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Java exception in %s", where);
    }
}

jbyteArray to_bytes(JNIEnv* env, const void* data, size_t size) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(size));
    if (array != nullptr && size > 0) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(size),
                                static_cast<const jbyte*>(data));
    }
    return array;
}

jbyteArray text_bytes(JNIEnv* env, const char* text) {
    return to_bytes(env, text, std::strlen(text));
}

std::vector<uint8_t> from_bytes(JNIEnv* env, jbyteArray array) {
    if (array == nullptr) {
        return {};
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(env->GetArrayLength(array)));
    env->GetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()),
                            reinterpret_cast<jbyte*>(bytes.data()));
    return bytes;
}

/// UTF-8 bytes from Kotlin as a string.
std::string from_text(JNIEnv* env, jbyteArray array) {
    const std::vector<uint8_t> bytes = from_bytes(env, array);
    return {bytes.begin(), bytes.end()};
}

/// A 16-byte ID (device or group) from Kotlin; false when it has the wrong size.
bool read_id(JNIEnv* env, jbyteArray array, std::array<uint8_t, OSDROP_DEVICE_ID_SIZE>& id) {
    if (array == nullptr || env->GetArrayLength(array) != OSDROP_DEVICE_ID_SIZE) {
        return false;
    }
    env->GetByteArrayRegion(array, 0, OSDROP_DEVICE_ID_SIZE, reinterpret_cast<jbyte*>(id.data()));
    return true;
}

void put_long(JNIEnv* env, jlongArray out, uint64_t value) {
    const auto stored = static_cast<jlong>(value);
    env->SetLongArrayRegion(out, 0, 1, &stored);
}

jobjectArray text_array(JNIEnv* env, const char* const* texts, size_t count) {
    jclass bytes_class = env->FindClass("[B");
    jobjectArray array = env->NewObjectArray(static_cast<jsize>(count), bytes_class, nullptr);
    env->DeleteLocalRef(bytes_class);
    for (size_t i = 0; i < count && array != nullptr; ++i) {
        jbyteArray text = text_bytes(env, texts[i]);
        env->SetObjectArrayElement(array, static_cast<jsize>(i), text);
        env->DeleteLocalRef(text);
    }
    return array;
}

/// The mesh and the Kotlin listener it reports to.
struct Bridge {
    osdrop_mesh* mesh = nullptr;
    jobject listener = nullptr; // global reference
    jmethodID on_group_changed = nullptr;
    jmethodID on_left_group = nullptr;
    jmethodID on_pairing_code = nullptr;
    jmethodID on_pairing_failed = nullptr;
    jmethodID on_peer_connected = nullptr;
    jmethodID on_peer_disconnected = nullptr;
    jmethodID on_log = nullptr;
    jmethodID on_transfer_progress = nullptr;
    jmethodID on_transfer_finished = nullptr;
    jmethodID on_send_arrived = nullptr;
    jmethodID on_send_finished = nullptr;
};

Bridge* bridge_of(void* context) {
    return static_cast<Bridge*>(context);
}

Bridge* bridge_of(jlong handle) {
    return reinterpret_cast<Bridge*>(handle);
}

template <typename... Args>
void call_void(Bridge* bridge, JNIEnv* env, jmethodID method, const char* name, Args... args) {
    env->CallVoidMethod(bridge->listener, method, args...);
    clear_exception(env, name);
}

void on_group_changed(void* context, const uint8_t* state, size_t size, const uint8_t* group_id) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray bytes = to_bytes(env, state, size);
    jbyteArray group =
        group_id != nullptr ? to_bytes(env, group_id, OSDROP_GROUP_ID_SIZE) : nullptr;
    call_void(bridge, env, bridge->on_group_changed, "onGroupChanged", bytes, group);
    env->DeleteLocalRef(bytes);
    if (group != nullptr) {
        env->DeleteLocalRef(group);
    }
}

void on_left_group(void* context, osdrop_result reason) {
    Bridge* bridge = bridge_of(context);
    if (JNIEnv* env = attached_env()) {
        call_void(bridge, env, bridge->on_left_group, "onLeftGroup", static_cast<jint>(reason));
    }
}

void on_pairing_code(void* context, uint64_t connection, uint32_t code, const osdrop_peer* peer) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray name = text_bytes(env, peer->name);
    call_void(bridge, env, bridge->on_pairing_code, "onPairingCode", static_cast<jlong>(connection),
              static_cast<jint>(code), name, static_cast<jint>(peer->platform));
    env->DeleteLocalRef(name);
}

void on_pairing_failed(void* context, uint64_t connection, osdrop_result reason) {
    Bridge* bridge = bridge_of(context);
    if (JNIEnv* env = attached_env()) {
        call_void(bridge, env, bridge->on_pairing_failed, "onPairingFailed",
                  static_cast<jlong>(connection), static_cast<jint>(reason));
    }
}

void on_peer_connected(void* context, const osdrop_peer* peer, uint32_t /*features*/) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray device = to_bytes(env, peer->device_id, OSDROP_DEVICE_ID_SIZE);
    jbyteArray name = text_bytes(env, peer->name);
    call_void(bridge, env, bridge->on_peer_connected, "onPeerConnected", device, name,
              static_cast<jint>(peer->platform));
    env->DeleteLocalRef(device);
    env->DeleteLocalRef(name);
}

void on_peer_disconnected(void* context, const uint8_t* device_id, osdrop_result reason) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray device = to_bytes(env, device_id, OSDROP_DEVICE_ID_SIZE);
    call_void(bridge, env, bridge->on_peer_disconnected, "onPeerDisconnected", device,
              static_cast<jint>(reason));
    env->DeleteLocalRef(device);
}

void on_log(void* context, osdrop_log_level level, const char* message) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray text = text_bytes(env, message);
    call_void(bridge, env, bridge->on_log, "onLog", static_cast<jint>(level), text);
    env->DeleteLocalRef(text);
}

void on_transfer_progress(void* context, const osdrop_transfer_progress* progress) {
    Bridge* bridge = bridge_of(context);
    if (JNIEnv* env = attached_env()) {
        call_void(bridge, env, bridge->on_transfer_progress, "onTransferProgress",
                  static_cast<jlong>(progress->transfer_id),
                  static_cast<jlong>(progress->bytes_done),
                  static_cast<jlong>(progress->bytes_total));
    }
}

void on_transfer_finished(void* context, uint64_t transfer_id, osdrop_result result,
                          const char* const* items, size_t item_count, uint64_t /*skipped_links*/) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jobjectArray paths = text_array(env, items, item_count);
    call_void(bridge, env, bridge->on_transfer_finished, "onTransferFinished",
              static_cast<jlong>(transfer_id), static_cast<jint>(result), paths);
    env->DeleteLocalRef(paths);
}

void on_send_arrived(void* context, const uint8_t* device_id, uint64_t send_id,
                     const osdrop_drag_item* items, size_t count) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    std::vector<const char*> names;
    std::vector<jlong> sizes;
    std::vector<jboolean> folders;
    for (const osdrop_drag_item& item : std::span(items, count)) {
        names.push_back(item.name);
        sizes.push_back(static_cast<jlong>(item.byte_count));
        folders.push_back(item.is_folder == 1 ? JNI_TRUE : JNI_FALSE);
    }
    jbyteArray device = to_bytes(env, device_id, OSDROP_DEVICE_ID_SIZE);
    jobjectArray name_array = text_array(env, names.data(), names.size());
    jlongArray size_array = env->NewLongArray(static_cast<jsize>(count));
    env->SetLongArrayRegion(size_array, 0, static_cast<jsize>(count), sizes.data());
    jbooleanArray folder_array = env->NewBooleanArray(static_cast<jsize>(count));
    env->SetBooleanArrayRegion(folder_array, 0, static_cast<jsize>(count), folders.data());
    call_void(bridge, env, bridge->on_send_arrived, "onSendArrived", device,
              static_cast<jlong>(send_id), name_array, size_array, folder_array);
    env->DeleteLocalRef(device);
    env->DeleteLocalRef(name_array);
    env->DeleteLocalRef(size_array);
    env->DeleteLocalRef(folder_array);
}

void on_send_finished(void* context, const uint8_t* device_id, uint64_t send_id,
                      osdrop_send_outcome outcome) {
    Bridge* bridge = bridge_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray device = to_bytes(env, device_id, OSDROP_DEVICE_ID_SIZE);
    call_void(bridge, env, bridge->on_send_finished, "onSendFinished", device,
              static_cast<jlong>(send_id), static_cast<jint>(outcome));
    env->DeleteLocalRef(device);
}

bool find_methods(JNIEnv* env, jobject listener, Bridge& bridge) {
    jclass type = env->GetObjectClass(listener);
    const auto method = [&](const char* name, const char* signature) {
        return env->GetMethodID(type, name, signature);
    };
    bridge.on_group_changed = method("onGroupChanged", "([B[B)V");
    bridge.on_left_group = method("onLeftGroup", "(I)V");
    bridge.on_pairing_code = method("onPairingCode", "(JI[BI)V");
    bridge.on_pairing_failed = method("onPairingFailed", "(JI)V");
    bridge.on_peer_connected = method("onPeerConnected", "([B[BI)V");
    bridge.on_peer_disconnected = method("onPeerDisconnected", "([BI)V");
    bridge.on_log = method("onLog", "(I[B)V");
    bridge.on_transfer_progress = method("onTransferProgress", "(JJJ)V");
    bridge.on_transfer_finished = method("onTransferFinished", "(JI[[B)V");
    bridge.on_send_arrived = method("onSendArrived", "([BJ[[B[J[Z)V");
    bridge.on_send_finished = method("onSendFinished", "([BJI)V");
    env->DeleteLocalRef(type);
    const std::array methods{bridge.on_group_changed,
                             bridge.on_left_group,
                             bridge.on_pairing_code,
                             bridge.on_pairing_failed,
                             bridge.on_peer_connected,
                             bridge.on_peer_disconnected,
                             bridge.on_log,
                             bridge.on_transfer_progress,
                             bridge.on_transfer_finished,
                             bridge.on_send_arrived,
                             bridge.on_send_finished};
    if (std::ranges::any_of(methods, [](jmethodID found) { return found == nullptr; })) {
        clear_exception(env, "GetMethodID");
        return false;
    }
    return true;
}

osdrop_mesh_callbacks callbacks_for(Bridge* bridge) {
    osdrop_mesh_callbacks callbacks{};
    callbacks.context = bridge;
    callbacks.group_changed = on_group_changed;
    callbacks.left_group = on_left_group;
    callbacks.pairing_code = on_pairing_code;
    callbacks.pairing_failed = on_pairing_failed;
    callbacks.peer_connected = on_peer_connected;
    callbacks.peer_disconnected = on_peer_disconnected;
    callbacks.log = on_log;
    callbacks.transfer_progress = on_transfer_progress;
    callbacks.transfer_finished = on_transfer_finished;
    callbacks.send_arrived = on_send_arrived;
    callbacks.send_finished = on_send_finished;
    return callbacks;
}

jint result(osdrop_result value) {
    return static_cast<jint>(value);
}

} // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    pthread_key_create(&g_detach_key, detach_thread);
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_coreVersion(JNIEnv* env,
                                                                                 jclass /*type*/) {
    const osdrop_version version = osdrop_core_version();
    const std::string text = std::to_string(version.major) + "." + std::to_string(version.minor) +
                             "." + std::to_string(version.patch);
    return env->NewStringUTF(text.c_str()); // digits and dots only
}

/// A new identity: the secret key followed by the public key; null without randomness.
JNIEXPORT jbyteArray JNICALL
Java_com_akinalpfdn_osdrop_core_NativeCore_generateIdentity(JNIEnv* env, jclass /*type*/) {
    std::array<uint8_t, OSDROP_KEY_SIZE * 2> keys{};
    if (osdrop_identity_generate(&kRandom, keys.data(), keys.data() + OSDROP_KEY_SIZE) !=
        OSDROP_OK) {
        return nullptr;
    }
    jbyteArray array = to_bytes(env, keys.data(), keys.size());
    keys.fill(0);
    return array;
}

/// The TXT values this phone advertises (pv, pm, id, name, os, grp); null for a bad argument.
JNIEXPORT jobjectArray JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_advertisement(
    JNIEnv* env, jclass /*type*/, jbyteArray device_id, jbyteArray name, jbyteArray group_id) {
    osdrop_advertisement advertisement{};
    advertisement.protocol_version = OSDROP_PROTOCOL_VERSION;
    advertisement.protocol_minor = OSDROP_PROTOCOL_MINOR;
    advertisement.platform = OSDROP_PLATFORM_ANDROID;
    std::array<uint8_t, OSDROP_DEVICE_ID_SIZE> id{};
    const std::string text = from_text(env, name);
    if (!read_id(env, device_id, id) || text.empty() || text.size() >= OSDROP_TXT_VALUE_SIZE) {
        return nullptr;
    }
    std::memcpy(advertisement.device_id, id.data(), id.size());
    std::memcpy(advertisement.name, text.data(), text.size());
    if (group_id != nullptr) {
        std::array<uint8_t, OSDROP_GROUP_ID_SIZE> group{};
        if (!read_id(env, group_id, group)) {
            return nullptr;
        }
        advertisement.has_group = 1;
        std::memcpy(advertisement.group_id, group.data(), group.size());
    }
    osdrop_txt_record record{};
    if (osdrop_advertisement_to_txt(&advertisement, &record) != OSDROP_OK) {
        return nullptr;
    }
    const std::array<const char*, 6> values{
        record.protocol_version, record.protocol_minor, record.device_id, record.name, record.os,
        record.group_id};
    return text_array(env, values.data(), values.size());
}

/// A peer's TXT values (pv, pm, id, name, os, grp; missing ones empty) as [version, minor,
/// platform, device ID in hex, group ID in hex or empty, name]; null when they are no valid record.
JNIEXPORT jobjectArray JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_parseAdvertisement(
    JNIEnv* env, jclass /*type*/, jobjectArray values) {
    if (values == nullptr || env->GetArrayLength(values) != 6) {
        return nullptr;
    }
    osdrop_txt_record record{};
    const std::array<char*, 6> fields{
        record.protocol_version, record.protocol_minor, record.device_id, record.name, record.os,
        record.group_id};
    for (jsize i = 0; i < 6; ++i) {
        auto* element = static_cast<jbyteArray>(env->GetObjectArrayElement(values, i));
        const std::string text = from_text(env, element);
        env->DeleteLocalRef(element);
        if (text.size() >= OSDROP_TXT_VALUE_SIZE) {
            return nullptr;
        }
        std::memcpy(fields.at(static_cast<size_t>(i)), text.data(), text.size());
    }
    osdrop_advertisement advertisement{};
    if (osdrop_advertisement_from_txt(&record, &advertisement) != OSDROP_OK) {
        return nullptr;
    }
    const std::string version = std::to_string(advertisement.protocol_version);
    const std::string minor = std::to_string(advertisement.protocol_minor);
    const std::string platform = std::to_string(static_cast<int>(advertisement.platform));
    const std::array<const char*, 6> parsed{version.c_str(),  minor.c_str(),   platform.c_str(),
                                            record.device_id, record.group_id, advertisement.name};
    return text_array(env, parsed.data(), parsed.size());
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshCreate(
    JNIEnv* env, jclass /*type*/, jbyteArray secret_key, jbyteArray name, jbyteArray group_state,
    jint port, jobject listener, jlongArray out_handle) {
    if (secret_key == nullptr || env->GetArrayLength(secret_key) != OSDROP_KEY_SIZE ||
        listener == nullptr || out_handle == nullptr) {
        return result(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    auto* bridge = new Bridge();
    if (!find_methods(env, listener, *bridge)) {
        delete bridge;
        return result(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    bridge->listener = env->NewGlobalRef(listener);
    std::vector<uint8_t> secret = from_bytes(env, secret_key);
    const std::string device_name = from_text(env, name);
    const std::vector<uint8_t> state = from_bytes(env, group_state);
    const osdrop_mesh_config config{.secret_key = secret.data(),
                                    .device_name = device_name.c_str(),
                                    .platform = OSDROP_PLATFORM_ANDROID,
                                    .group_state = state.empty() ? nullptr : state.data(),
                                    .group_state_size = state.size(),
                                    .layout_state = nullptr,
                                    .layout_state_size = 0,
                                    .features = OSDROP_FEATURE_FILES,
                                    .max_transfer_streams = 4,
                                    .listen_port = static_cast<uint16_t>(port)};
    const osdrop_mesh_callbacks callbacks = callbacks_for(bridge);
    const osdrop_result created = osdrop_mesh_create(&config, &kRandom, &callbacks, &bridge->mesh);
    std::ranges::fill(secret, 0);
    if (created != OSDROP_OK) {
        env->DeleteGlobalRef(bridge->listener);
        delete bridge;
        return result(created);
    }
    put_long(env, out_handle, reinterpret_cast<uintptr_t>(bridge));
    return result(OSDROP_OK);
}

JNIEXPORT void JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshDestroy(JNIEnv* env,
                                                                              jclass /*type*/,
                                                                              jlong handle) {
    Bridge* bridge = bridge_of(handle);
    if (bridge == nullptr) {
        return;
    }
    osdrop_mesh_destroy(bridge->mesh); // joins the I/O thread: no callback runs after this
    env->DeleteGlobalRef(bridge->listener);
    delete bridge;
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshListenPort(JNIEnv* /*env*/,
                                                                                 jclass /*type*/,
                                                                                 jlong handle) {
    uint16_t port = 0;
    return osdrop_mesh_listen_port(bridge_of(handle)->mesh, &port) == OSDROP_OK ? port : 0;
}

JNIEXPORT jbyteArray JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshDeviceId(
    JNIEnv* env, jclass /*type*/, jlong handle) {
    std::array<uint8_t, OSDROP_DEVICE_ID_SIZE> id{};
    if (osdrop_mesh_device_id(bridge_of(handle)->mesh, id.data()) != OSDROP_OK) {
        return nullptr;
    }
    return to_bytes(env, id.data(), id.size());
}

JNIEXPORT jbyteArray JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshGroupId(JNIEnv* env,
                                                                                    jclass /*type*/,
                                                                                    jlong handle) {
    std::array<uint8_t, OSDROP_GROUP_ID_SIZE> id{};
    uint8_t has_group = 0;
    if (osdrop_mesh_group_id(bridge_of(handle)->mesh, id.data(), &has_group) != OSDROP_OK ||
        has_group == 0) {
        return nullptr;
    }
    return to_bytes(env, id.data(), id.size());
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshPeerDiscovered(
    JNIEnv* env, jclass /*type*/, jlong handle, jbyteArray device_id, jbyteArray group_id,
    jbyteArray host, jint port) {
    std::array<uint8_t, OSDROP_DEVICE_ID_SIZE> device{};
    std::array<uint8_t, OSDROP_GROUP_ID_SIZE> group{};
    if (!read_id(env, device_id, device) ||
        (group_id != nullptr && !read_id(env, group_id, group))) {
        return result(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    const std::string address = from_text(env, host);
    return result(osdrop_mesh_peer_discovered(bridge_of(handle)->mesh, device.data(),
                                              group_id != nullptr ? group.data() : nullptr,
                                              address.c_str(), static_cast<uint16_t>(port)));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshPeerLost(
    JNIEnv* env, jclass /*type*/, jlong handle, jbyteArray device_id) {
    std::array<uint8_t, OSDROP_DEVICE_ID_SIZE> device{};
    if (!read_id(env, device_id, device)) {
        return result(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    return result(osdrop_mesh_peer_lost(bridge_of(handle)->mesh, device.data()));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshPair(
    JNIEnv* env, jclass /*type*/, jlong handle, jbyteArray host, jint port,
    jlongArray out_connection) {
    const std::string address = from_text(env, host);
    uint64_t connection = 0;
    const osdrop_result paired = osdrop_mesh_pair(bridge_of(handle)->mesh, address.c_str(),
                                                  static_cast<uint16_t>(port), &connection);
    if (paired == OSDROP_OK) {
        put_long(env, out_connection, connection);
    }
    return result(paired);
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshConfirmPairing(
    JNIEnv* /*env*/, jclass /*type*/, jlong handle, jlong connection, jboolean accepted) {
    return result(osdrop_mesh_confirm_pairing(
        bridge_of(handle)->mesh, static_cast<uint64_t>(connection), accepted == JNI_TRUE ? 1 : 0));
}

/// The group's members, three byte arrays each: the device ID, the name (UTF-8), and a record of
/// four bytes (platform, self, connected, files permission). Null when listing failed.
JNIEXPORT jobjectArray JNICALL
Java_com_akinalpfdn_osdrop_core_NativeCore_meshMembers(JNIEnv* env, jclass /*type*/, jlong handle) {
    std::vector<osdrop_member> members(8);
    size_t count = 0;
    osdrop_result listed = OSDROP_ERROR_BUFFER_TOO_SMALL;
    while (listed == OSDROP_ERROR_BUFFER_TOO_SMALL) {
        listed =
            osdrop_mesh_members(bridge_of(handle)->mesh, members.data(), members.size(), &count);
        if (listed == OSDROP_ERROR_BUFFER_TOO_SMALL) {
            members.resize(count);
        }
    }
    if (listed != OSDROP_OK) {
        return nullptr;
    }
    jclass bytes_class = env->FindClass("[B");
    jobjectArray array = env->NewObjectArray(static_cast<jsize>(count * 3), bytes_class, nullptr);
    env->DeleteLocalRef(bytes_class);
    for (size_t i = 0; i < count && array != nullptr; ++i) {
        const osdrop_member& member = members[i];
        jbyteArray device = to_bytes(env, member.device_id, OSDROP_DEVICE_ID_SIZE);
        jbyteArray name = text_bytes(env, member.name);
        const std::array<uint8_t, 4> flags{static_cast<uint8_t>(member.platform), member.self,
                                           member.connected, member.permissions.files};
        jbyteArray record = to_bytes(env, flags.data(), flags.size());
        env->SetObjectArrayElement(array, static_cast<jsize>(i * 3), device);
        env->SetObjectArrayElement(array, static_cast<jsize>((i * 3) + 1), name);
        env->SetObjectArrayElement(array, static_cast<jsize>((i * 3) + 2), record);
        env->DeleteLocalRef(device);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(record);
    }
    return array;
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshLeaveGroup(JNIEnv* /*env*/,
                                                                                 jclass /*type*/,
                                                                                 jlong handle) {
    return result(osdrop_mesh_leave_group(bridge_of(handle)->mesh));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshReceiveSent(
    JNIEnv* env, jclass /*type*/, jlong handle, jbyteArray device_id, jlong send_id, jint index,
    jbyteArray destination, jlongArray out_transfer) {
    std::array<uint8_t, OSDROP_DEVICE_ID_SIZE> device{};
    if (!read_id(env, device_id, device) || index < 0) {
        return result(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    const osdrop_object_ref item{.kind = OSDROP_OBJECT_SENT,
                                 .object_id = static_cast<uint64_t>(send_id),
                                 .item_index = static_cast<uint32_t>(index)};
    const std::string folder = from_text(env, destination);
    uint64_t transfer = 0;
    const osdrop_result received = osdrop_mesh_receive(
        bridge_of(handle)->mesh, device.data(), &item, folder.c_str(), UINT64_MAX, &transfer);
    if (received == OSDROP_OK) {
        put_long(env, out_transfer, transfer);
    }
    return result(received);
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshCancelTransfer(
    JNIEnv* /*env*/, jclass /*type*/, jlong handle, jlong transfer) {
    return result(
        osdrop_mesh_cancel_transfer(bridge_of(handle)->mesh, static_cast<uint64_t>(transfer)));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshSend(
    JNIEnv* env, jclass /*type*/, jlong handle, jbyteArray device_id, jobjectArray paths,
    jobjectArray types, jlongArray out_send) {
    std::array<uint8_t, OSDROP_DEVICE_ID_SIZE> device{};
    if (!read_id(env, device_id, device) || paths == nullptr || types == nullptr ||
        env->GetArrayLength(paths) != env->GetArrayLength(types)) {
        return result(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    const auto count = static_cast<size_t>(env->GetArrayLength(paths));
    std::vector<std::string> path_texts;
    std::vector<std::string> type_texts;
    for (size_t i = 0; i < count; ++i) {
        auto* path =
            static_cast<jbyteArray>(env->GetObjectArrayElement(paths, static_cast<jsize>(i)));
        auto* type =
            static_cast<jbyteArray>(env->GetObjectArrayElement(types, static_cast<jsize>(i)));
        path_texts.push_back(from_text(env, path));
        type_texts.push_back(from_text(env, type));
        env->DeleteLocalRef(path);
        env->DeleteLocalRef(type);
    }
    std::vector<osdrop_dragged_item> items;
    items.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        items.push_back({.path = path_texts[i].c_str(), .content_type = type_texts[i].c_str()});
    }
    uint64_t send_id = 0;
    const osdrop_result sent = osdrop_mesh_send(bridge_of(handle)->mesh, device.data(),
                                                items.data(), items.size(), &send_id);
    if (sent == OSDROP_OK) {
        put_long(env, out_send, send_id);
    }
    return result(sent);
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshAnswerSend(
    JNIEnv* env, jclass /*type*/, jlong handle, jbyteArray device_id, jlong send_id, jint outcome) {
    std::array<uint8_t, OSDROP_DEVICE_ID_SIZE> device{};
    if (!read_id(env, device_id, device)) {
        return result(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    return result(osdrop_mesh_answer_send(bridge_of(handle)->mesh, device.data(),
                                          static_cast<uint64_t>(send_id),
                                          static_cast<osdrop_send_outcome>(outcome)));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osdrop_core_NativeCore_meshCloseAll(JNIEnv* /*env*/,
                                                                               jclass /*type*/,
                                                                               jlong handle) {
    return result(osdrop_mesh_close_all(bridge_of(handle)->mesh, 0));
}

} // extern "C"
