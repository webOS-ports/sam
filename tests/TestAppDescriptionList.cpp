// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "TempTree.h"
#include "base/AppDescription.h"
#include "base/AppDescriptionList.h"

// compare(me, another) answers "should me be replaced by another?". It orders by
// version and, when the versions are equal, by where the application was found.
// That last tiebreak compared 'me' against itself and so never fired.
class AppDescriptionCompareTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_previousHome = getenv("HOME") ? getenv("HOME") : "";
        setenv("HOME", m_tree.root().c_str(), 1);
    }

    void TearDown() override
    {
        if (m_previousHome.empty())
            unsetenv("HOME");
        else
            setenv("HOME", m_previousHome.c_str(), 1);
    }

    // Each application needs its own folder named after the appId, so give the
    // two sides of a comparison distinct roots.
    AppDescriptionPtr make(const string& slot, const string& appId, const string& version,
                           AppLocation location)
    {
        const string relative = slot + "/" + appId;
        m_tree.write(relative + "/appinfo.json",
                     "{\"id\":\"" + appId + "\",\"title\":\"T\",\"main\":\"index.html\","
                     "\"type\":\"web\",\"version\":\"" + version + "\"}");

        AppDescriptionPtr appDesc = std::make_shared<AppDescription>(appId);
        appDesc->scan(m_tree.path(relative), location);
        return appDesc;
    }

    TempTree m_tree;
    string m_previousHome;
};

TEST_F(AppDescriptionCompareTest, PrefersAHigherMajorVersion)
{
    AppDescriptionPtr older = make("a", "com.webos.app.v", "1.9.9", AppLocation::AppLocation_System_ReadOnly);
    AppDescriptionPtr newer = make("b", "com.webos.app.v", "2.0.0", AppLocation::AppLocation_System_ReadOnly);

    EXPECT_TRUE(AppDescriptionList::compare(older, newer));
    EXPECT_FALSE(AppDescriptionList::compare(newer, older));
}

TEST_F(AppDescriptionCompareTest, PrefersAHigherMinorVersion)
{
    AppDescriptionPtr older = make("a", "com.webos.app.v", "1.2.9", AppLocation::AppLocation_System_ReadOnly);
    AppDescriptionPtr newer = make("b", "com.webos.app.v", "1.3.0", AppLocation::AppLocation_System_ReadOnly);

    EXPECT_TRUE(AppDescriptionList::compare(older, newer));
    EXPECT_FALSE(AppDescriptionList::compare(newer, older));
}

TEST_F(AppDescriptionCompareTest, PrefersAHigherMicroVersion)
{
    AppDescriptionPtr older = make("a", "com.webos.app.v", "1.2.3", AppLocation::AppLocation_System_ReadOnly);
    AppDescriptionPtr newer = make("b", "com.webos.app.v", "1.2.4", AppLocation::AppLocation_System_ReadOnly);

    EXPECT_TRUE(AppDescriptionList::compare(older, newer));
    EXPECT_FALSE(AppDescriptionList::compare(newer, older));
}

TEST_F(AppDescriptionCompareTest, FallsBackToAppLocationWhenVersionsMatch)
{
    // This is the tiebreak that the self-comparison disabled. Both sides carry
    // the same version, so only the location can decide.
    AppDescriptionPtr lower = make("a", "com.webos.app.same", "1.0.0", AppLocation::AppLocation_System_ReadOnly);
    AppDescriptionPtr higher = make("b", "com.webos.app.same", "1.0.0", AppLocation::AppLocation_Devmode);

    ASSERT_EQ(lower->getIntVersion(), higher->getIntVersion());
    ASSERT_LT(static_cast<int>(lower->getAppLocation()), static_cast<int>(higher->getAppLocation()));

    // compare() returns true only when 'me' ranks above 'another' by location,
    // so exactly one direction may be true. Before the fix both were false.
    const bool lowerFirst = AppDescriptionList::compare(lower, higher);
    const bool higherFirst = AppDescriptionList::compare(higher, lower);

    EXPECT_NE(lowerFirst, higherFirst) << "the appLocation tiebreak never fired";
}

TEST_F(AppDescriptionCompareTest, IsFalseForIdenticalDescriptions)
{
    AppDescriptionPtr appDesc = make("a", "com.webos.app.self", "1.0.0", AppLocation::AppLocation_System_ReadOnly);

    EXPECT_FALSE(AppDescriptionList::compare(appDesc, appDesc));
}
