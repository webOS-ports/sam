// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "TempTree.h"
#include "util/File.h"

class FileTest : public ::testing::Test {
protected:
    TempTree m_tree;
};

TEST_F(FileTest, JoinInsertsExactlyOneSeparator)
{
    EXPECT_EQ("/usr/palm/applications", File::join("/usr/palm", "applications"));
    EXPECT_EQ("/usr/palm/applications", File::join("/usr/palm/", "applications"));
    EXPECT_EQ("/usr/palm/applications", File::join("/usr/palm", "/applications"));
    EXPECT_EQ("/usr/palm/applications", File::join("/usr/palm/", "/applications"));
}

TEST_F(FileTest, JoinHandlesEmptyComponents)
{
    EXPECT_EQ("/appinfo.json", File::join("", "appinfo.json"));
    EXPECT_EQ("/usr/palm/", File::join("/usr/palm/", ""));
}

TEST_F(FileTest, TrimPathRemovesOnlyATrailingSeparator)
{
    string withSlash = "/usr/palm/";
    File::trimPath(withSlash);
    EXPECT_EQ("/usr/palm", withSlash);

    string withoutSlash = "/usr/palm";
    File::trimPath(withoutSlash);
    EXPECT_EQ("/usr/palm", withoutSlash);

    string empty = "";
    File::trimPath(empty);
    EXPECT_EQ("", empty);
}

TEST_F(FileTest, SetSlashToBasePathAppendsAtMostOneSeparator)
{
    string path = "/usr/palm";
    File::set_slash_to_base_path(path);
    EXPECT_EQ("/usr/palm/", path);

    File::set_slash_to_base_path(path);
    EXPECT_EQ("/usr/palm/", path);
}

TEST_F(FileTest, ConcatToFilenameInsertsBeforeTheExtension)
{
    string result;

    EXPECT_TRUE(File::concatToFilename("/usr/palm/icon.png", result, "_80"));
    EXPECT_EQ("/usr/palm/icon_80.png", result);

    EXPECT_TRUE(File::concatToFilename("icon.png", result, "_80"));
    EXPECT_EQ("icon_80.png", result);
}

TEST_F(FileTest, ConcatToFilenameRejectsInputItCannotSplit)
{
    string result = "untouched";

    EXPECT_FALSE(File::concatToFilename("", result, "_80"));
    EXPECT_FALSE(File::concatToFilename("/usr/palm/icon.png", result, ""));
    // no extension at all
    EXPECT_FALSE(File::concatToFilename("/usr/palm/icon", result, "_80"));
    // a bare dot leaves an extension shorter than two characters
    EXPECT_FALSE(File::concatToFilename("/usr/palm/icon.", result, "_80"));
}

TEST_F(FileTest, IsFileAndIsDirectoryDistinguishTheTwo)
{
    m_tree.write("app/appinfo.json", "{}");

    EXPECT_TRUE(File::isFile(m_tree.path("app/appinfo.json")));
    EXPECT_FALSE(File::isDirectory(m_tree.path("app/appinfo.json")));

    EXPECT_TRUE(File::isDirectory(m_tree.path("app")));
    EXPECT_FALSE(File::isFile(m_tree.path("app")));

    EXPECT_FALSE(File::isFile(m_tree.path("app/nothing-here")));
    EXPECT_FALSE(File::isDirectory(m_tree.path("app/nothing-here")));
}

TEST_F(FileTest, WriteReadAndDeleteRoundTrip)
{
    const string path = m_tree.path("round-trip.txt");

    EXPECT_TRUE(File::writeFile(path, "contents"));
    EXPECT_EQ("contents", File::readFile(path));
    EXPECT_TRUE(File::isFile(path));

    EXPECT_TRUE(File::deleteFile(path));
    EXPECT_FALSE(File::isFile(path));
}

TEST_F(FileTest, ReadFileReturnsEmptyForAMissingFile)
{
    EXPECT_EQ("", File::readFile(m_tree.path("nothing-here")));
}

TEST_F(FileTest, MakeDirectoryCreatesParents)
{
    const string nested = m_tree.path("a/b/c");

    EXPECT_TRUE(File::makeDirectory(nested));
    EXPECT_TRUE(File::isDirectory(nested));
}
