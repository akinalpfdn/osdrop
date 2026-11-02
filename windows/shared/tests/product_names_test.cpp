#include <osdrop/win/product_names.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string_view>

namespace {

using osdrop::win::input_pipe_name;

TEST(ProductNames, InputPipeNameAppendsTheSessionId) {
    EXPECT_EQ(input_pipe_name(0), LR"(\\.\pipe\com.akinalpfdn.osdrop.input.0)");
    EXPECT_EQ(input_pipe_name(17), LR"(\\.\pipe\com.akinalpfdn.osdrop.input.17)");
    EXPECT_EQ(input_pipe_name(std::numeric_limits<std::uint32_t>::max()),
              LR"(\\.\pipe\com.akinalpfdn.osdrop.input.4294967295)");
}

TEST(ProductNames, InputPipeNameFitsTheWindowsLimit) {
    // CreateNamedPipe accepts names of up to 256 characters.
    constexpr std::size_t kMaxPipeName = 256;
    EXPECT_LE(input_pipe_name(std::numeric_limits<std::uint32_t>::max()).size(), kMaxPipeName);
}

TEST(ProductNames, ServiceNameIsAValidServiceKey) {
    // The Service Control Manager rejects names longer than 256 characters and with slashes.
    constexpr std::size_t kMaxServiceName = 256;
    const std::wstring_view name = osdrop::win::kServiceName;
    EXPECT_FALSE(name.empty());
    EXPECT_LE(name.size(), kMaxServiceName);
    EXPECT_EQ(name.find_first_of(L"/\\"), std::wstring_view::npos);
}

TEST(ProductNames, ExecutablesShareTheProductPrefix) {
    for (const std::wstring_view executable :
         {osdrop::win::kServiceExecutable, osdrop::win::kInputExecutable,
          osdrop::win::kAppExecutable}) {
        EXPECT_TRUE(executable.starts_with(L"Osdrop")) << "executable name without the prefix";
        EXPECT_TRUE(executable.ends_with(L".exe")) << "executable name without .exe";
    }
}

} // namespace
