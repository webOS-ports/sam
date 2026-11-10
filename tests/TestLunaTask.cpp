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

// The activity manager's "$activity" reaches the application inside its
// params, as LunaSysMgr handed it on (see LunaTask::passActivityToParams).
class LunaTaskActivityTest : public ::testing::Test {
protected:
    LunaTaskPtr makeTask(const char* json)
    {
        m_payload = pbnjson::JDomParser::fromString(json);
        return std::make_shared<LunaTask>(m_request, m_payload, nullptr);
    }

    LS::Message m_request;
    JValue m_payload;
};

TEST_F(LunaTaskActivityTest, ActivityGoesIntoParams)
{
    const LunaTaskPtr task = makeTask(R"({"id":"com.palm.app.calendar","params":{"dayChange":true},)"
                                R"("$activity":{"activityId":42,"name":"calendar.updateIcon"}})");
    task->passActivityToParams();

    const JValue params = task->getParams();
    ASSERT_TRUE(params.hasKey("$activity"));
    EXPECT_EQ(42, params["$activity"]["activityId"].asNumber<int>());
    EXPECT_EQ("calendar.updateIcon", params["$activity"]["name"].asString());
    EXPECT_TRUE(params["dayChange"].asBool()) << "the caller's own params stay";
}

TEST_F(LunaTaskActivityTest, ParamsAreMadeWhenThereAreNone)
{
    const LunaTaskPtr task = makeTask(R"({"id":"com.example.app","$activity":{"activityId":7}})");
    task->passActivityToParams();

    EXPECT_EQ(7, task->getParams()["$activity"]["activityId"].asNumber<int>());
}

TEST_F(LunaTaskActivityTest, NoActivityLeavesParamsAlone)
{
    const LunaTaskPtr task = makeTask(R"({"id":"com.example.app","params":{"a":1}})");
    task->passActivityToParams();

    const JValue params = task->getParams();
    EXPECT_FALSE(params.hasKey("$activity"));
    EXPECT_EQ(1u, params.objectSize());

    const LunaTaskPtr bare = makeTask(R"({"id":"com.example.app"})");
    bare->passActivityToParams();
    EXPECT_FALSE(bare->getRequestPayload().hasKey("params")) << "no params made for nothing";
}

TEST_F(LunaTaskActivityTest, ApplicationsOwnActivityWins)
{
    const LunaTaskPtr task = makeTask(R"({"id":"com.example.app","params":{"$activity":{"activityId":1}},)"
                                R"("$activity":{"activityId":2}})");
    task->passActivityToParams();

    EXPECT_EQ(1, task->getParams()["$activity"]["activityId"].asNumber<int>());
}

TEST_F(LunaTaskActivityTest, ActivityThatIsNotAnObjectIsIgnored)
{
    const LunaTaskPtr task = makeTask(R"({"id":"com.example.app","$activity":"x"})");
    task->passActivityToParams();

    EXPECT_FALSE(task->getRequestPayload().hasKey("params"));
}

TEST_F(LunaTaskActivityTest, ParamsThatAreNotAnObjectAreLeftAlone)
{
    const LunaTaskPtr task = makeTask(R"({"id":"com.example.app","params":"text","$activity":{"activityId":3}})");
    task->passActivityToParams();

    EXPECT_EQ("text", task->getRequestPayload()["params"].asString());
}
