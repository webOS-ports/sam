// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "TempTree.h"
#include "base/AppDescription.h"
#include "conf/SAMConf.h"

// anchorLocalePath() decides which of two conventions a localized appinfo.json
// is using. Legacy Palm/HP applications write "main" relative to the
// localization directory ("../../index.html"); webOS OSE applications write it
// relative to the application root ("index.multi.html"). Guessing from the shape
// of the value gets one of them wrong, so the resolver keeps whichever candidate
// is actually on disk.
class AnchorLocalePathTest : public ::testing::Test {
protected:
    TempTree m_tree;
};

TEST_F(AnchorLocalePathTest, ReAnchorsALegacyPathAgainstTheLocalizationDir)
{
    m_tree.write("index.html", "<html/>");
    m_tree.mkdirs("resources/en");

    EXPECT_EQ("index.html",
              AppDescription::anchorLocalePath(m_tree.root(), "resources/en/", "../../index.html"));
}

TEST_F(AnchorLocalePathTest, KeepsAnOseRootRelativePathAsItIs)
{
    m_tree.write("index.multi.html", "<html/>");
    m_tree.mkdirs("resources/en");

    EXPECT_EQ("index.multi.html",
              AppDescription::anchorLocalePath(m_tree.root(), "resources/en/", "index.multi.html"));
}

TEST_F(AnchorLocalePathTest, PrefersTheLocalizedFileWhenBothExist)
{
    m_tree.write("icon.png", "root");
    m_tree.write("resources/en/icon.png", "localized");

    EXPECT_EQ("resources/en/icon.png",
              AppDescription::anchorLocalePath(m_tree.root(), "resources/en/", "icon.png"));
}

TEST_F(AnchorLocalePathTest, NeverReturnsAnAbsolutePath)
{
    // applyFolderPath() treats a leading '/' as "already absolute" and publishes
    // the value unprefixed, which resolves nowhere. Even for a value that
    // escapes the application root, the result must stay relative.
    const string resolved =
        AppDescription::anchorLocalePath(m_tree.root(), "resources/en/", "../../../../etc/passwd");

    ASSERT_FALSE(resolved.empty());
    EXPECT_NE('/', resolved[0]);
}

TEST_F(AnchorLocalePathTest, FallsBackToTheLocalizedFormWhenNeitherExists)
{
    // A value naming something generated later must not be silently rewritten.
    EXPECT_EQ("resources/en/generated.html",
              AppDescription::anchorLocalePath(m_tree.root(), "resources/en/", "generated.html"));
}

// A full scan() of a synthetic application directory. This covers the paths that
// read a third-party appinfo.json, which is attacker-shaped input as far as SAM
// is concerned.
class AppDescriptionScanTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Keep SAMConf's read-write config inside the temp tree.
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

    // Build <root>/<appId>/appinfo.json and scan it.
    AppDescriptionPtr scanApp(const string& appId, const string& appinfo)
    {
        m_tree.write(appId + "/appinfo.json", appinfo);
        AppDescriptionPtr appDesc = std::make_shared<AppDescription>(appId);
        appDesc->scan(m_tree.path(appId), AppLocation::AppLocation_System_ReadOnly);
        return appDesc;
    }

    TempTree m_tree;
    string m_previousHome;
};

TEST_F(AppDescriptionScanTest, ParsesAThreeComponentVersion)
{
    AppDescriptionPtr appDesc = scanApp("com.webos.app.test",
        R"({"id":"com.webos.app.test","title":"Test","main":"index.html","type":"web","version":"2.5.7"})");

    const AppIntVersion& version = appDesc->getIntVersion();
    EXPECT_EQ(2, std::get<0>(version));
    EXPECT_EQ(5, std::get<1>(version));
    EXPECT_EQ(7, std::get<2>(version));
}

TEST_F(AppDescriptionScanTest, SurvivesANonNumericVersion)
{
    // stoi() used to throw std::invalid_argument straight out of the scan,
    // taking every remaining application with it.
    EXPECT_NO_THROW({
        scanApp("com.webos.app.beta",
            R"({"id":"com.webos.app.beta","title":"Beta","main":"index.html","type":"web","version":"1.0.0-beta"})");
    });

    EXPECT_NO_THROW({
        scanApp("com.webos.app.junk",
            R"({"id":"com.webos.app.junk","title":"Junk","main":"index.html","type":"web","version":"not.a.version"})");
    });

    EXPECT_NO_THROW({
        scanApp("com.webos.app.empty",
            R"({"id":"com.webos.app.empty","title":"Empty","main":"index.html","type":"web","version":""})");
    });
}

TEST_F(AppDescriptionScanTest, TreatsAnUnparseableVersionComponentAsZero)
{
    AppDescriptionPtr appDesc = scanApp("com.webos.app.partial",
        R"({"id":"com.webos.app.partial","title":"Partial","main":"index.html","type":"web","version":"3.x.9"})");

    const AppIntVersion& version = appDesc->getIntVersion();
    EXPECT_EQ(3, std::get<0>(version));
    EXPECT_EQ(0, std::get<1>(version));
    EXPECT_EQ(9, std::get<2>(version));
}

TEST_F(AppDescriptionScanTest, RejectsAFolderThatDoesNotMatchTheAppId)
{
    m_tree.write("some-other-folder/appinfo.json",
        R"({"id":"com.webos.app.mismatch","title":"X","main":"index.html","type":"web","version":"1.0.0"})");

    AppDescriptionPtr appDesc = std::make_shared<AppDescription>("com.webos.app.mismatch");
    EXPECT_FALSE(appDesc->scan(m_tree.path("some-other-folder"), AppLocation::AppLocation_System_ReadOnly));
}

TEST_F(AppDescriptionScanTest, RejectsAMissingOrUnparseableAppinfo)
{
    m_tree.mkdirs("com.webos.app.none");
    AppDescriptionPtr missing = std::make_shared<AppDescription>("com.webos.app.none");
    EXPECT_FALSE(missing->scan(m_tree.path("com.webos.app.none"), AppLocation::AppLocation_System_ReadOnly));

    AppDescriptionPtr broken = scanApp("com.webos.app.broken", "{ this is not json");
    EXPECT_FALSE(broken->isScanned());
}

TEST_F(AppDescriptionScanTest, MarksOrgWebosportsAsPrivileged)
{
    // LuneOS addition: org.webosports.* is a first-party prefix here.
    AppDescriptionPtr ours = std::make_shared<AppDescription>("org.webosports.app.settings");
    EXPECT_TRUE(ours->isPrivilegedAppId());

    AppDescriptionPtr theirs = std::make_shared<AppDescription>("com.example.app");
    EXPECT_FALSE(theirs->isPrivilegedAppId());

    AppDescriptionPtr upstream = std::make_shared<AppDescription>("com.webos.app.browser");
    EXPECT_TRUE(upstream->isPrivilegedAppId());
}

TEST_F(AppDescriptionScanTest, ReadsTheLuneOSAppinfoFlags)
{
    AppDescriptionPtr plain = scanApp("com.webos.app.plain",
        R"({"id":"com.webos.app.plain","title":"Plain","main":"index.html","type":"web","version":"1.0.0"})");
    EXPECT_FALSE(plain->useLuneOSStyle());
    EXPECT_FALSE(plain->hasNoWindow());
    EXPECT_FALSE(plain->isTrusted());

    AppDescriptionPtr flagged = scanApp("com.webos.app.flagged",
        R"({"id":"com.webos.app.flagged","title":"Flagged","main":"index.html","type":"web","version":"1.0.0",)"
        R"("useLuneOSStyle":true,"noWindow":true,"trustLevel":"trusted"})");
    EXPECT_TRUE(flagged->useLuneOSStyle());
    EXPECT_TRUE(flagged->hasNoWindow());
    EXPECT_TRUE(flagged->isTrusted());
}
