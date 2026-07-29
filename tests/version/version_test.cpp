/**
 * @file version_test.cpp
 * @brief Unit tests for mog::Version.
 */

#include "mog/version.hpp"

#include <gtest/gtest.h>
#include <string>
#include <string_view>

namespace
{

TEST(VersionTest, IsNonEmpty)
{
    const std::string_view version = mog::Version();
    EXPECT_FALSE(version.empty());
}

TEST(VersionTest, MatchesComponentConstants)
{
    // Version() is generated from the root VERSION file; constants must agree.
    const std::string expected = std::to_string(mog::kVersionMajor) + "." +
                                 std::to_string(mog::kVersionMinor) + "." +
                                 std::to_string(mog::kVersionPatch);
    EXPECT_EQ(std::string_view{mog::Version()}, expected);
    EXPECT_GE(mog::kVersionMajor, 0);
    EXPECT_GE(mog::kVersionMinor, 0);
    EXPECT_GE(mog::kVersionPatch, 0);
}

TEST(VersionTest, HasThreeNumericComponents)
{
    const std::string_view version = mog::Version();
    EXPECT_NE(version.find('.'), std::string_view::npos);
    EXPECT_NE(version.rfind('.'), std::string_view::npos);
    EXPECT_NE(version.find('.'), version.rfind('.'));
}

} // namespace
