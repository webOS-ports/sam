// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <string>

#include "bus/client/NativeContainer.h"

// SAM opens a native app's log as root, under a name built from the app id. The
// schema leaves "id" free-form, and an id that came from AppInstallService is only
// checked against the folder it names, so the check has to be here.
TEST(NativeLogPathTest, BuildsTheLogNames)
{
    string path;

    EXPECT_TRUE(NativeContainer::getLogPath("com.example.app", "", 3, path));
    EXPECT_EQ("/var/log/com.example.app-3", path);
    EXPECT_TRUE(NativeContainer::getLogPath("com.example.app", "root", 4, path));
    EXPECT_EQ("/var/log/com.example.app-root-4", path);
}

TEST(NativeLogPathTest, RefusesAnIdThatLeavesTheLogDirectory)
{
    for (const string id : { "", "/etc/shadow", "../../etc/shadow", "com.example/app", "..", "com..example" }) {
        string path = "untouched";
        EXPECT_FALSE(NativeContainer::getLogPath(id, "", 0, path)) << id;
        EXPECT_FALSE(NativeContainer::getLogPath(id, "root", 0, path)) << id;
        EXPECT_EQ("untouched", path) << id;
    }
}

TEST(NativeLogPathTest, RefusesAUserThatLeavesTheLogDirectory)
{
    string path;

    EXPECT_FALSE(NativeContainer::getLogPath("com.example.app", "../root", 0, path));
    EXPECT_FALSE(NativeContainer::getLogPath("com.example.app", "a/b", 0, path));
}
