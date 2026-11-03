#include "store/dpapi_store.hpp"

#include <windows.h>

#include <dpapi.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace osdrop::engine {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr const wchar_t* kFolder = L"Osdrop";
constexpr const wchar_t* kDescription = L"Osdrop";

const wchar_t* file_name(win::StoredItem item) {
    switch (item) {
    case win::StoredItem::Identity:
        return L"identity.bin";
    case win::StoredItem::GroupState:
        return L"group.bin";
    case win::StoredItem::LayoutState:
        return L"layout.bin";
    case win::StoredItem::ListenPort:
        return L"port.bin";
    }
    return L"unknown.bin";
}

bool sealed(win::StoredItem item) {
    return item == win::StoredItem::Identity || item == win::StoredItem::GroupState;
}

/// %APPDATA%\Osdrop, created when missing.
std::optional<std::filesystem::path> store_folder() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming))) {
        CoTaskMemFree(roaming);
        return std::nullopt;
    }
    std::filesystem::path folder = std::filesystem::path(roaming) / kFolder;
    CoTaskMemFree(roaming);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return error ? std::nullopt : std::optional(folder);
}

/// Nullopt when the file does not exist.
std::optional<Bytes> read_file(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// Writes next to the target and replaces it, so a crash never leaves half a file.
bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as chars
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return false;
        }
    }
    return MoveFileExW(temporary.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

std::optional<Bytes> protect(std::span<const std::uint8_t> plain) {
    Bytes copy(plain.begin(), plain.end()); // DPAPI takes a mutable pointer
    DATA_BLOB input{.cbData = static_cast<DWORD>(copy.size()), .pbData = copy.data()};
    DATA_BLOB output{};
    const BOOL done = CryptProtectData(&input, kDescription, nullptr, nullptr, nullptr,
                                       CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(copy.data(), copy.size());
    if (done == FALSE) {
        return std::nullopt;
    }
    Bytes result(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return result;
}

std::optional<Bytes> unprotect(std::span<const std::uint8_t> sealed_bytes) {
    Bytes copy(sealed_bytes.begin(), sealed_bytes.end()); // DPAPI takes a mutable pointer
    DATA_BLOB input{.cbData = static_cast<DWORD>(copy.size()), .pbData = copy.data()};
    DATA_BLOB output{};
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                           &output) == FALSE) {
        return std::nullopt;
    }
    Bytes result(output.pbData, output.pbData + output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

} // namespace

win::Expected<std::optional<Bytes>, osdrop_result> DpapiStore::load(win::StoredItem item) {
    using Result = win::Expected<std::optional<Bytes>, osdrop_result>;
    const std::scoped_lock lock(mutex_);
    const auto folder = store_folder();
    if (!folder) {
        return Result::failure(OSDROP_ERROR_INTERNAL);
    }
    auto stored = read_file(*folder / file_name(item));
    if (!stored || !sealed(item)) {
        return std::move(stored);
    }
    auto plain = unprotect(*stored);
    if (!plain) {
        return Result::failure(OSDROP_ERROR_DECRYPTION_FAILED); // another user's, or damaged
    }
    return plain;
}

bool DpapiStore::save(win::StoredItem item, std::span<const std::uint8_t> bytes) {
    const std::scoped_lock lock(mutex_);
    const auto folder = store_folder();
    if (!folder) {
        return false;
    }
    if (!sealed(item)) {
        return write_file(*folder / file_name(item), bytes);
    }
    const auto protected_bytes = protect(bytes);
    return protected_bytes && write_file(*folder / file_name(item), *protected_bytes);
}

} // namespace osdrop::engine
