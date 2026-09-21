// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdlib>

#include "TempTree.h"
#include "base/AppDescription.h"
#include "conf/RuntimeInfo.h"
#include "conf/SAMConf.h"
#include "util/File.h"

// SAMConf keeps the locale in its read-write config. When RuntimeInfo has a
// $HOME, that config lives at $HOME/.config/sam-conf.json, so pointing $HOME at
// a temporary tree keeps setLocale() and the reloads it triggers entirely inside
// the sandbox.
class SAMConfLocaleTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const char* home = getenv("HOME");
        m_previousHome = (home != nullptr) ? home : "";
        setenv("HOME", m_tree.root().c_str(), 1);

        // Re-read $HOME, then reload the config from the now-empty sandbox.
        RuntimeInfo::getInstance().initialize();
        SAMConf::getInstance().initialize();
    }

    void TearDown() override
    {
        // The singletons outlive the test, so leave the locale as the rest of
        // the suite expects to find it.
        SAMConf::getInstance().setLocale("", "", "");

        if (m_previousHome.empty())
            unsetenv("HOME");
        else
            setenv("HOME", m_previousHome.c_str(), 1);
        RuntimeInfo::getInstance().initialize();

        // Deliberately no SAMConf::initialize() here. loadReadWriteConf()
        // creates $HOME/.config/sam-conf.json when it cannot parse one, so
        // re-reading it now - with the real $HOME back in place - would write
        // into the developer's home directory.
    }

    string confPath() const { return m_tree.path(".config/sam-conf.json"); }

    TempTree m_tree;
    string m_previousHome;
};

TEST_F(SAMConfLocaleTest, RoundTripsThroughTheGetters)
{
    SAMConf::getInstance().setLocale("en", "Latn", "US");

    EXPECT_EQ("en", SAMConf::getInstance().getLanguage());
    EXPECT_EQ("Latn", SAMConf::getInstance().getScript());
    EXPECT_EQ("US", SAMConf::getInstance().getRegion());
}

TEST_F(SAMConfLocaleTest, KeepsEmptyComponentsEmpty)
{
    // A locale without a script is the common case (en-US), and the empty
    // component must not come back as anything else.
    SAMConf::getInstance().setLocale("nl", "", "NL");

    EXPECT_EQ("nl", SAMConf::getInstance().getLanguage());
    EXPECT_EQ("", SAMConf::getInstance().getScript());
    EXPECT_EQ("NL", SAMConf::getInstance().getRegion());
}

TEST_F(SAMConfLocaleTest, PersistsToTheReadWriteConfig)
{
    SAMConf::getInstance().setLocale("ko", "Kore", "KR");

    ASSERT_TRUE(File::isFile(confPath()));
    const JValue persisted = JDomParser::fromFile(confPath().c_str());
    ASSERT_TRUE(persisted.isObject());
    EXPECT_EQ("ko", persisted["language"].asString());
    EXPECT_EQ("Kore", persisted["script"].asString());
    EXPECT_EQ("KR", persisted["region"].asString());
}

TEST_F(SAMConfLocaleTest, SurvivesAReload)
{
    SAMConf::getInstance().setLocale("de", "", "DE");
    SAMConf::getInstance().initialize();

    EXPECT_EQ("de", SAMConf::getInstance().getLanguage());
    EXPECT_EQ("", SAMConf::getInstance().getScript());
    EXPECT_EQ("DE", SAMConf::getInstance().getRegion());
}

TEST_F(SAMConfLocaleTest, ForgetsALocaleThatIsNoLongerInTheConfig)
{
    // The accessors used to hand back a reference to a function-local static
    // that getValue() writes into, so once a locale had been read, a config
    // without those keys returned the previous value instead of "".
    SAMConf::getInstance().setLocale("fr", "Latn", "FR");
    ASSERT_EQ("fr", SAMConf::getInstance().getLanguage());
    ASSERT_EQ("Latn", SAMConf::getInstance().getScript());
    ASSERT_EQ("FR", SAMConf::getInstance().getRegion());

    ASSERT_TRUE(File::writeFile(confPath(), "{}"));
    SAMConf::getInstance().initialize();

    EXPECT_EQ("", SAMConf::getInstance().getLanguage());
    EXPECT_EQ("", SAMConf::getInstance().getScript());
    EXPECT_EQ("", SAMConf::getInstance().getRegion());
}

TEST_F(SAMConfLocaleTest, SettingTheSameLocaleTwiceIsHarmless)
{
    SAMConf::getInstance().setLocale("es", "", "ES");
    SAMConf::getInstance().setLocale("es", "", "ES");

    EXPECT_EQ("es", SAMConf::getInstance().getLanguage());
    EXPECT_EQ("ES", SAMConf::getInstance().getRegion());
}

// The locale decides which resources/<language>[/<script>][/<region>]
// directories loadAppinfo() overlays onto the root appinfo.json.
class LocalizedAppinfoTest : public SAMConfLocaleTest {
protected:
    static const char* rootAppinfo()
    {
        return R"({"id":"com.webos.app.l10n","title":"Root","main":"index.html","icon":"icon.png",)"
               R"("type":"web","version":"1.0.0"})";
    }

    AppDescriptionPtr scanLocalized()
    {
        AppDescriptionPtr appDesc = std::make_shared<AppDescription>("com.webos.app.l10n");
        appDesc->scan(m_tree.path("com.webos.app.l10n"), AppLocation::AppLocation_System_ReadOnly);
        return appDesc;
    }

    string titleOf(const AppDescriptionPtr& appDesc) const
    {
        JValue properties = pbnjson::Array();
        properties.append("title");
        return appDesc->getJson(properties)["title"].asString();
    }
};

TEST_F(LocalizedAppinfoTest, AppliesTheLanguageDirectory)
{
    m_tree.write("com.webos.app.l10n/appinfo.json", rootAppinfo());
    m_tree.write("com.webos.app.l10n/resources/nl/appinfo.json", R"({"title":"Nederlands"})");

    SAMConf::getInstance().setLocale("nl", "", "");
    EXPECT_EQ("Nederlands", titleOf(scanLocalized()));
}

TEST_F(LocalizedAppinfoTest, IgnoresADirectoryForAnotherLanguage)
{
    m_tree.write("com.webos.app.l10n/appinfo.json", rootAppinfo());
    m_tree.write("com.webos.app.l10n/resources/nl/appinfo.json", R"({"title":"Nederlands"})");

    SAMConf::getInstance().setLocale("ko", "", "");
    EXPECT_EQ("Root", titleOf(scanLocalized()));
}

TEST_F(LocalizedAppinfoTest, LetsTheRegionDirectoryWinOverTheLanguageOne)
{
    // Overlays are applied from least to most specific, so the region wins.
    m_tree.write("com.webos.app.l10n/appinfo.json", rootAppinfo());
    m_tree.write("com.webos.app.l10n/resources/en/appinfo.json", R"({"title":"English"})");
    m_tree.write("com.webos.app.l10n/resources/en/GB/appinfo.json", R"({"title":"British"})");

    SAMConf::getInstance().setLocale("en", "", "GB");
    EXPECT_EQ("British", titleOf(scanLocalized()));
}

TEST_F(LocalizedAppinfoTest, PlacesTheScriptDirectoryBetweenLanguageAndRegion)
{
    m_tree.write("com.webos.app.l10n/appinfo.json", rootAppinfo());
    m_tree.write("com.webos.app.l10n/resources/zh/appinfo.json", R"({"title":"Chinese"})");
    m_tree.write("com.webos.app.l10n/resources/zh/Hant/appinfo.json", R"({"title":"Traditional"})");
    m_tree.write("com.webos.app.l10n/resources/zh/Hant/TW/appinfo.json", R"({"title":"Taiwan"})");

    SAMConf::getInstance().setLocale("zh", "Hant", "TW");
    EXPECT_EQ("Taiwan", titleOf(scanLocalized()));
}

TEST_F(LocalizedAppinfoTest, AppliesTheRegionDirectoryWhenTheScriptIsEmpty)
{
    // With an empty script the region hangs directly off the language dir.
    //
    // Note this does not guard the "skip empty locale components" change: the
    // unskipped form produces resources/en// and resources/en//US/, which POSIX
    // resolves to the same directories, and re-applying an overlay is
    // idempotent because anchorLocalePath() always works from the pristine
    // locale file rather than from the value already published. The skip is
    // tidiness, not behaviour, and mutating it away leaves every test green.
    m_tree.write("com.webos.app.l10n/appinfo.json", rootAppinfo());
    m_tree.write("com.webos.app.l10n/resources/en/appinfo.json", R"({"title":"English"})");
    m_tree.write("com.webos.app.l10n/resources/en/US/appinfo.json", R"({"title":"American"})");

    SAMConf::getInstance().setLocale("en", "", "US");
    EXPECT_EQ("American", titleOf(scanLocalized()));
}

TEST_F(LocalizedAppinfoTest, ReAnchorsALegacyRelativeMainThroughAFullScan)
{
    // com.palm.app.* ship "main" relative to the localization directory. The
    // published value has to come back relative to the application root, and
    // must not be re-anchored a second time by a later overlay.
    m_tree.write("com.webos.app.l10n/appinfo.json", rootAppinfo());
    m_tree.write("com.webos.app.l10n/index.html", "<html/>");
    m_tree.write("com.webos.app.l10n/resources/en/appinfo.json",
                 R"({"main":"../../index.html"})");

    SAMConf::getInstance().setLocale("en", "", "");

    JValue properties = pbnjson::Array();
    properties.append("main");
    const string main = scanLocalized()->getJson(properties)["main"].asString();

    EXPECT_EQ("index.html", main);
    ASSERT_FALSE(main.empty());
    EXPECT_NE('/', main[0]);
}
