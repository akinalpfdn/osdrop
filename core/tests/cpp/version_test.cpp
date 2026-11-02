#include <osdrop/osdrop.h>

#include <gtest/gtest.h>

namespace {

TEST(CoreVersion, LinkedLibraryMatchesHeader) {
    const osdrop_version version = osdrop_core_version();

    EXPECT_EQ(version.major, OSDROP_CORE_VERSION_MAJOR);
    EXPECT_EQ(version.minor, OSDROP_CORE_VERSION_MINOR);
    EXPECT_EQ(version.patch, OSDROP_CORE_VERSION_PATCH);
}

} // namespace
