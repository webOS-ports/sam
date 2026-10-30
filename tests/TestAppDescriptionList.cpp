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
                     "\"icon\":\"icon.png\","
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

// scanApp() joins the appId onto each application path. The ids come from
// appinstalld and, through the scanApp method, from any caller allowed to use
// it, so an id must stay one path component.
TEST(AppDescriptionListScannableAppIdTest, AcceptsOrdinaryIds)
{
    EXPECT_TRUE(AppDescriptionList::isScannableAppId("org.webosports.app.phone"));
    EXPECT_TRUE(AppDescriptionList::isScannableAppId("waydroid.com.android.calculator2"));
    EXPECT_TRUE(AppDescriptionList::isScannableAppId("a..b"));
}

TEST(AppDescriptionListScannableAppIdTest, RejectsIdsThatLeaveTheApplicationPath)
{
    EXPECT_FALSE(AppDescriptionList::isScannableAppId(""));
    EXPECT_FALSE(AppDescriptionList::isScannableAppId("."));
    EXPECT_FALSE(AppDescriptionList::isScannableAppId(".."));
    EXPECT_FALSE(AppDescriptionList::isScannableAppId("../etc"));
    EXPECT_FALSE(AppDescriptionList::isScannableAppId(".hidden"));
    EXPECT_FALSE(AppDescriptionList::isScannableAppId("a/b"));
    EXPECT_FALSE(AppDescriptionList::isScannableAppId("/abs"));
    EXPECT_FALSE(AppDescriptionList::isScannableAppId(string("a\0b", 3)));
}

// findApp() is what scanApp() adds, or the reason it removes: an app whose
// folder is gone is not found.
class AppDescriptionListFindAppTest : public AppDescriptionCompareTest {
protected:
    void writeApp(const string& dir, const string& appId, const string& version) const
    {
        m_tree.write(dir + "/" + appId + "/appinfo.json",
                     "{\"id\":\"" + appId + "\",\"title\":\"T\",\"main\":\"index.html\","
                     "\"icon\":\"icon.png\","
                     "\"type\":\"web\",\"version\":\"" + version + "\"}");
    }

    JValue paths(std::initializer_list<std::pair<string, string>> dirs) const
    {
        JValue array = pbnjson::Array();
        for (const auto& dir : dirs) {
            JValue entry = pbnjson::Object();
            entry.put("path", m_tree.path(dir.first));
            entry.put("typeByDir", dir.second);
            array.append(entry);
        }
        return array;
    }
};

TEST_F(AppDescriptionListFindAppTest, FindsAnAppWrittenAfterStartup)
{
    m_tree.mkdirs("store");
    const JValue applicationPaths = paths({{"store", "store"}});
    EXPECT_EQ(nullptr, AppDescriptionList::findApp("waydroid.com.example", applicationPaths));

    writeApp("store", "waydroid.com.example", "1.0.0");
    const AppDescriptionPtr appDesc = AppDescriptionList::findApp("waydroid.com.example", applicationPaths);
    ASSERT_NE(nullptr, appDesc);
    EXPECT_EQ("waydroid.com.example", appDesc->getAppId());
    EXPECT_EQ(m_tree.path("store/waydroid.com.example"), appDesc->getFolderPath());
}

TEST_F(AppDescriptionListFindAppTest, DoesNotFindAnAppWhoseFolderIsGone)
{
    writeApp("store", "waydroid.com.example", "1.0.0");
    const JValue applicationPaths = paths({{"store", "store"}});
    ASSERT_NE(nullptr, AppDescriptionList::findApp("waydroid.com.example", applicationPaths));

    unlink(m_tree.path("store/waydroid.com.example/appinfo.json").c_str());
    rmdir(m_tree.path("store/waydroid.com.example").c_str());
    EXPECT_EQ(nullptr, AppDescriptionList::findApp("waydroid.com.example", applicationPaths));
}

// The same order scanApp() always used: the last listed path that has the app.
TEST_F(AppDescriptionListFindAppTest, TheLastPathThatHasTheAppWins)
{
    writeApp("builtin", "com.example.both", "1.0.0");
    writeApp("store", "com.example.both", "2.0.0");
    const AppDescriptionPtr appDesc = AppDescriptionList::findApp(
        "com.example.both", paths({{"builtin", "system_builtin"}, {"store", "store"}}));
    ASSERT_NE(nullptr, appDesc);
    EXPECT_EQ(m_tree.path("store/com.example.both"), appDesc->getFolderPath());
}

// An app next to the application path, reachable only by climbing out of it,
// is not found. Its appinfo carries the climbing id, so nothing later in the
// scan rejects it on a mismatch: only the id check stands in the way.
TEST_F(AppDescriptionListFindAppTest, DoesNotLeaveTheApplicationPath)
{
    m_tree.mkdirs("store");
    m_tree.write("outside/app/appinfo.json",
                 "{\"id\":\"../outside/app\",\"title\":\"T\",\"main\":\"index.html\","
                 "\"icon\":\"icon.png\",\"type\":\"web\",\"version\":\"1.0.0\"}");
    const JValue applicationPaths = paths({{"store", "store"}});
    EXPECT_EQ(nullptr, AppDescriptionList::findApp("../outside/app", applicationPaths));
}
