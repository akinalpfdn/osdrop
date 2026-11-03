#pragma once

// Reads protocol/fixtures/manifest.tsv and fixture files (docs/PROTOCOL.md, "Fixtures").

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace osdrop::test {

struct Fixture {
    std::string file;
    std::string kind;   // "control", "input" or "transfer"
    bool valid = false; // outcome
    int detail = 0;     // message type when valid, error code otherwise
    std::string description;
};

inline std::filesystem::path fixtures_directory() {
    return std::filesystem::path(OSDROP_FIXTURES_DIR);
}

inline std::vector<Fixture> load_manifest() {
    std::ifstream manifest(fixtures_directory() / "manifest.tsv");
    if (!manifest) {
        throw std::runtime_error("cannot open manifest.tsv");
    }
    std::vector<Fixture> fixtures;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream fields(line);
        Fixture fixture;
        std::string outcome;
        std::string detail;
        std::getline(fields, fixture.file, '\t');
        std::getline(fields, fixture.kind, '\t');
        std::getline(fields, outcome, '\t');
        std::getline(fields, detail, '\t');
        std::getline(fields, fixture.description);
        fixture.valid = outcome == "valid";
        fixture.detail = std::stoi(detail);
        fixtures.push_back(fixture);
    }
    return fixtures;
}

inline std::vector<std::uint8_t> read_fixture(const std::string& file) {
    std::ifstream stream(fixtures_directory() / file, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open fixture " + file);
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace osdrop::test
