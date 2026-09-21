// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "util/JValueUtil.h"

// getValue() leaves its out-parameter untouched on every failure path. Callers
// that declared the target uninitialized and ignored the return read garbage -
// that was the cause of the lockApp, onGetBootStatus and onStatus defects. These
// tests pin the contract so the next caller can rely on it.
class JValueUtilTest : public ::testing::Test {
};

TEST_F(JValueUtilTest, ReadsAPresentValueAndReportsSuccess)
{
    JValue json = pbnjson::Object();
    json.put("lock", true);

    bool lock = false;
    EXPECT_TRUE(JValueUtil::getValue(json, "lock", lock));
    EXPECT_TRUE(lock);
}

TEST_F(JValueUtilTest, LeavesTheTargetUntouchedWhenTheKeyIsAbsent)
{
    JValue json = pbnjson::Object();
    json.put("somethingElse", true);

    bool lock = false;
    EXPECT_FALSE(JValueUtil::getValue(json, "lock", lock));
    EXPECT_FALSE(lock) << "getValue must not write when it reports failure";

    bool lockSeededTrue = true;
    EXPECT_FALSE(JValueUtil::getValue(json, "lock", lockSeededTrue));
    EXPECT_TRUE(lockSeededTrue) << "getValue must not write when it reports failure";
}

TEST_F(JValueUtilTest, LeavesTheTargetUntouchedOnANullJson)
{
    const JValue json;

    int statusValue = -1;
    EXPECT_FALSE(JValueUtil::getValue(json, "statusValue", statusValue));
    EXPECT_EQ(-1, statusValue);
}

TEST_F(JValueUtilTest, LeavesTheTargetUntouchedOnATypeMismatch)
{
    JValue json = pbnjson::Object();
    json.put("lock", "not-a-bool");

    bool lock = false;
    EXPECT_FALSE(JValueUtil::getValue(json, "lock", lock));
    EXPECT_FALSE(lock);
}

TEST_F(JValueUtilTest, ReadsNestedKeys)
{
    JValue signals = pbnjson::Object();
    signals.put("core-boot-done", true);
    JValue json = pbnjson::Object();
    json.put("signals", signals);

    bool coreBootDone = false;
    EXPECT_TRUE(JValueUtil::getValue(json, "signals", "core-boot-done", coreBootDone));
    EXPECT_TRUE(coreBootDone);
}

TEST_F(JValueUtilTest, LeavesTheTargetUntouchedWhenAnIntermediateKeyIsMissing)
{
    const JValue json = pbnjson::Object();

    bool coreBootDone = false;
    EXPECT_FALSE(JValueUtil::getValue(json, "signals", "core-boot-done", coreBootDone));
    EXPECT_FALSE(coreBootDone);

    JValue notAnObject = pbnjson::Object();
    notAnObject.put("signals", 42);
    EXPECT_FALSE(JValueUtil::getValue(notAnObject, "signals", "core-boot-done", coreBootDone));
    EXPECT_FALSE(coreBootDone);
}

TEST_F(JValueUtilTest, AddUniqueItemToArrayDoesNotDuplicate)
{
    JValue array = pbnjson::Array();
    string first = "com.webos.app.one";
    string again = "com.webos.app.one";
    string second = "com.webos.app.two";

    JValueUtil::addUniqueItemToArray(array, first);
    JValueUtil::addUniqueItemToArray(array, again);
    JValueUtil::addUniqueItemToArray(array, second);

    EXPECT_EQ(2, array.arraySize());
}

TEST_F(JValueUtilTest, GetSchemaFallsBackToAllSchemaWhenTheFileIsMissing)
{
    // The schemas are installed separately; a missing one must not make the
    // application scan fail outright.
    EXPECT_TRUE(JValueUtil::getSchema("NoSuchSchemaAnywhere").isInitialized());
}
