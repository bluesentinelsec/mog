/**
 * @file json_test.cpp
 * @brief nlohmann/json interop tests (requires MOG_WITH_JSON).
 */

#include "mog/mog.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#if !defined(MOG_HAS_JSON) || !MOG_HAS_JSON
#error "json_test requires MOG_HAS_JSON"
#endif

TEST(JsonInteropTest, WithJsonFromNlohmann)
{
    nlohmann::json body = {{"name", "mog"}, {"n", 1}};
    mog::Options opt;
    mog::WithJson(opt, body);
    ASSERT_TRUE(opt.json.has_value());
    auto parsed = nlohmann::json::parse(*opt.json);
    EXPECT_EQ(parsed["name"], "mog");
    EXPECT_EQ(parsed["n"], 1);
}

TEST(JsonInteropTest, JsonOptionsFactory)
{
    auto opt = mog::JsonOptions(nlohmann::json::array({1, 2, 3}));
    ASSERT_TRUE(opt.json.has_value());
    EXPECT_EQ(nlohmann::json::parse(*opt.json), nlohmann::json::array({1, 2, 3}));
}

TEST(JsonInteropTest, ParseJsonResponse)
{
    mog::Response r;
    r.body = R"({"ok":true,"v":2})";
    auto j = mog::ParseJson(r);
    ASSERT_TRUE(j) << j.error().to_string();
    EXPECT_TRUE((*j)["ok"].get<bool>());
    EXPECT_EQ((*j)["v"], 2);
}

TEST(JsonInteropTest, ParseJsonInvalid)
{
    mog::Response r;
    r.body = "not-json{";
    auto j = mog::ParseJson(r);
    ASSERT_FALSE(j);
    EXPECT_EQ(j.error().code(), mog::ErrorCode::JsonError);
}

TEST(JsonInteropTest, StringWithJsonStillWorks)
{
    mog::Options opt;
    mog::WithJson(opt, std::string{R"({"a":1})"});
    ASSERT_TRUE(opt.json.has_value());
    EXPECT_EQ(*opt.json, R"({"a":1})");
}
