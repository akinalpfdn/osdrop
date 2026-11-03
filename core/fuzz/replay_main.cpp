// Runs every file under the given directories through the fuzz entry point, so the fuzz
// properties are checked on the fixtures in every test run, without libFuzzer.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <vector>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

int run(std::span<char*> arguments) {
    std::size_t count = 0;
    for (const char* argument : arguments.subspan(1)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(argument)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::ifstream file(entry.path(), std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                                  std::istreambuf_iterator<char>());
            LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
            ++count;
        }
    }
    std::cout << "Replayed " << count << " inputs\n";
    return count > 0 ? 0 : 1;
}

} // namespace

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(*-avoid-c-arrays)
int main(int argc, char* argv[]) {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
}
