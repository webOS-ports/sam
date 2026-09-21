// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "util/Logger.h"

// Logger::format() used to build its result in a function-local static buffer of
// 1024 bytes: concurrent callers scribbled over each other and anything longer
// was silently cut short.
class LoggerFormatTest : public ::testing::Test {
};

TEST_F(LoggerFormatTest, FormatsTheOrdinaryCases)
{
    EXPECT_EQ("hello", Logger::format("hello"));
    EXPECT_EQ("", Logger::format(""));
    EXPECT_EQ("a-42", Logger::format("%s-%d", "a", 42));
    EXPECT_EQ("lock(true)", Logger::format("lock(%s)", Logger::toString(true)));
    EXPECT_EQ("lock(false)", Logger::format("lock(%s)", Logger::toString(false)));
}

TEST_F(LoggerFormatTest, KeepsResultsLongerThanTheOldFixedBuffer)
{
    const std::string big(4096, 'x');
    const std::string formatted = Logger::format("%s", big.c_str());

    EXPECT_EQ(big.size(), formatted.size());
    EXPECT_EQ(big, formatted);
}

TEST_F(LoggerFormatTest, DoesNotEmbedATrailingNul)
{
    const std::string formatted = Logger::format("ab%sc", "d");

    EXPECT_EQ("abdc", formatted);
    EXPECT_EQ(std::string::npos, formatted.find('\0'));
    EXPECT_EQ(4u, formatted.size());
}

TEST_F(LoggerFormatTest, IsSafeToCallFromSeveralThreadsAtOnce)
{
    // SAM formats log lines from the glib main loop and from luna-service
    // callbacks. Each thread checks its own results, so a shared buffer shows up
    // as a mismatch rather than as a crash.
    const int threadCount = 8;
    const int perThread = 2000;
    std::atomic<int> mismatches(0);
    std::vector<std::thread> threads;

    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([t, perThread, &mismatches]() {
            for (int i = 0; i < perThread; ++i) {
                const std::string expected =
                    "thread(" + std::to_string(t) + ") iteration(" + std::to_string(i) + ")";
                if (Logger::format("thread(%d) iteration(%d)", t, i) != expected)
                    ++mismatches;
            }
        });
    }
    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(0, mismatches.load());
}

TEST_F(LoggerFormatTest, ToStringIsStable)
{
    // toString() hands back a pointer the caller may hold across other calls.
    const char* const t = Logger::toString(true);
    const char* const f = Logger::toString(false);

    EXPECT_STREQ("true", t);
    EXPECT_STREQ("false", f);
    EXPECT_STREQ("true", Logger::toString(true));
    EXPECT_STREQ("true", t);
    EXPECT_STREQ("false", f);
}
