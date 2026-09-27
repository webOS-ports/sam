// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "base/LunaTask.h"

// PolicyManager::pre() gives a task default reply callbacks, but only where the
// caller has not set its own, using hasSuccessCallback()/hasErrorCallback().
// hasErrorCallback() used to test the success callback, so once pre() had set
// that, it never set the error callback: error() then did nothing and the
// caller got no reply at all (closeByAppId on an app WAM had already closed).
class LunaTaskTest : public ::testing::Test {
protected:
    LunaTaskPtr makeTask()
    {
        JValue payload = pbnjson::Object();
        return std::make_shared<LunaTask>(m_request, payload, nullptr);
    }

    LS::Message m_request;
};

TEST_F(LunaTaskTest, ReportsEachCallbackOnItsOwn)
{
    LunaTaskPtr task = makeTask();
    EXPECT_FALSE(task->hasSuccessCallback());
    EXPECT_FALSE(task->hasErrorCallback());

    task->setSuccessCallback([](LunaTaskPtr) {});
    EXPECT_TRUE(task->hasSuccessCallback());
    EXPECT_FALSE(task->hasErrorCallback()) << "a success callback must not count as an error callback";

    task->setErrorCallback([](LunaTaskPtr) {});
    EXPECT_TRUE(task->hasErrorCallback());
}

TEST_F(LunaTaskTest, ErrorCallbackIsReachableAfterPreStyleDefaults)
{
    LunaTaskPtr task = makeTask();
    int successCalls = 0;
    int errorCalls = 0;

    // What PolicyManager::pre() does
    if (!task->hasSuccessCallback())
        task->setSuccessCallback([&successCalls](LunaTaskPtr) { ++successCalls; });
    if (!task->hasErrorCallback())
        task->setErrorCallback([&errorCalls](LunaTaskPtr) { ++errorCalls; });

    task->error(task);
    EXPECT_EQ(1, errorCalls) << "error() must reach a reply callback";
    EXPECT_EQ(0, successCalls);
}
