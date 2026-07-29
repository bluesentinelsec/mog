/**
 * @file log_test.cpp
 * @brief Logger injection / default logger tests (requires MOG_WITH_SPDLOG).
 */

#include "mog/log.hpp"

#include <gtest/gtest.h>

#if !defined(MOG_HAS_SPDLOG) || !MOG_HAS_SPDLOG
#error "log_test requires MOG_HAS_SPDLOG"
#endif

#include <spdlog/sinks/ostream_sink.h>
#include <sstream>

TEST(LogTest, ParseLogLevel)
{
    mog::LogLevel level = mog::LogLevel::Off;
    ASSERT_TRUE(mog::ParseLogLevel("debug", level));
    EXPECT_EQ(level, mog::LogLevel::Debug);
    ASSERT_TRUE(mog::ParseLogLevel("WARN", level));
    EXPECT_EQ(level, mog::LogLevel::Warn);
    EXPECT_FALSE(mog::ParseLogLevel("nope", level));
}

TEST(LogTest, DefaultLoggerAndLevel)
{
    mog::UseDefaultLogger(mog::LogLevel::Info);
    auto logger = mog::GetLogger();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "mog");
    EXPECT_EQ(mog::GetLogLevel(), mog::LogLevel::Info);

    mog::SetLogLevel(mog::LogLevel::Debug);
    EXPECT_EQ(mog::GetLogLevel(), mog::LogLevel::Debug);
}

TEST(LogTest, InjectCustomLogger)
{
    auto stream = std::make_shared<std::ostringstream>();
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*stream);
    auto custom = std::make_shared<spdlog::logger>("custom-mog", sink);
    custom->set_level(spdlog::level::info);
    custom->set_pattern("%v");

    mog::SetLogger(custom);
    ASSERT_EQ(mog::GetLogger()->name(), "custom-mog");
    MOG_LOG_INFO("hello-from-test");
    custom->flush();
    EXPECT_NE(stream->str().find("hello-from-test"), std::string::npos);

    // Restore default for other tests.
    mog::UseDefaultLogger(mog::LogLevel::Warn);
}
