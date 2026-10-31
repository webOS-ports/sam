// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "TempTree.h"
#include "conf/RuntimeInfo.h"
#include "conf/SAMConf.h"
#include "util/File.h"
#include "util/NativeProcess.h"

// SAM runs as root and writes its state into /var/preferences and /run/sam. These
// guard File::writeFile() and File::readStateFile() against the symlink tricks
// that let anyone who can write to such a directory make root truncate or
// overwrite a file of their choosing, and check that a replacement is atomic.
class SafeFileTest : public ::testing::Test {
protected:
    static mode_t modeOf(const string& path)
    {
        struct stat info;
        if (lstat(path.c_str(), &info) != 0)
            return 0;
        return info.st_mode & 07777;
    }

    static bool isSymlink(const string& path)
    {
        struct stat info;
        return lstat(path.c_str(), &info) == 0 && S_ISLNK(info.st_mode);
    }

    static bool exists(const string& path)
    {
        struct stat info;
        return lstat(path.c_str(), &info) == 0;
    }

    static mode_t currentUmask()
    {
        const mode_t mask = umask(022);
        umask(mask);
        return mask;
    }

    TempTree m_tree;
};

// sam.service creates /run/sam (root, 0700) and sam.sh tests the marker there, in
// another repository: these names must not drift back to a world-writable /tmp.
TEST(StatePathTest, StateLivesInTheRootOnlyRunDirectory)
{
    EXPECT_STREQ("/run/sam/sam_runtime", PATH_RUNTIME_INFO);
    EXPECT_EQ("/run/sam/sam-respawned", SAMConf::getInstance().getRespawnedPath());
}

TEST_F(SafeFileTest, TempPathSitsNextToTheTarget)
{
    EXPECT_EQ("/var/preferences/.sam-conf.json.tmp", File::tempPathFor("/var/preferences/sam-conf.json"));
    EXPECT_EQ("/.sam_runtime.tmp", File::tempPathFor("/sam_runtime"));
    EXPECT_EQ("./.name.tmp", File::tempPathFor("name"));
    EXPECT_EQ("", File::tempPathFor("/var/preferences/"));
    EXPECT_EQ("", File::tempPathFor("/var/preferences/.."));
    EXPECT_EQ("", File::tempPathFor("/var/preferences/."));
}

TEST_F(SafeFileTest, WritesANewFile)
{
    const string path = m_tree.path("new.json");

    ASSERT_TRUE(File::writeFile(path, "{\"a\": 1}"));
    EXPECT_EQ("{\"a\": 1}", File::readFile(path));
    EXPECT_EQ(0644 & ~currentUmask(), modeOf(path));
    EXPECT_FALSE(exists(File::tempPathFor(path)));
}

TEST_F(SafeFileTest, ReplacesAnExistingFileByRename)
{
    const string path = m_tree.write("conf.json", "old contents, longer than the new ones");
    struct stat before;
    ASSERT_EQ(0, stat(path.c_str(), &before));

    ASSERT_TRUE(File::writeFile(path, "new"));

    struct stat after;
    ASSERT_EQ(0, stat(path.c_str(), &after));
    EXPECT_EQ("new", File::readFile(path));
    // A new inode: the file was replaced, not truncated and rewritten in place.
    EXPECT_NE(before.st_ino, after.st_ino);
    EXPECT_FALSE(exists(File::tempPathFor(path)));
}

TEST_F(SafeFileTest, AReaderOfTheOldFileSeesItWhole)
{
    // What a crash or a concurrent reader sees: the old file stays complete
    // until the new one replaces it in one step.
    const string path = m_tree.write("conf.json", "the complete old file");
    const int oldFd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    ASSERT_GE(oldFd, 0);

    ASSERT_TRUE(File::writeFile(path, "{}"));

    char buffer[64] = {};
    const ssize_t got = pread(oldFd, buffer, sizeof(buffer) - 1, 0);
    close(oldFd);
    ASSERT_GT(got, 0);
    EXPECT_EQ("the complete old file", string(buffer, static_cast<size_t>(got)));
    EXPECT_EQ("{}", File::readFile(path));
}

TEST_F(SafeFileTest, AFailedWriteLeavesTheOldFileIntact)
{
    const string path = m_tree.write("conf.json", "old");

    // Let the write itself fail half way, as a full disk would: a file size
    // limit makes write() return EFBIG instead of raising SIGXFSZ.
    struct rlimit saved;
    ASSERT_EQ(0, getrlimit(RLIMIT_FSIZE, &saved));
    struct rlimit small = saved;
    small.rlim_cur = 1024;
    void (*previous)(int) = signal(SIGXFSZ, SIG_IGN);
    ASSERT_EQ(0, setrlimit(RLIMIT_FSIZE, &small));

    const bool written = File::writeFile(path, string(64 * 1024, 'x'));
    const int writeErrno = errno;

    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, previous);

    EXPECT_FALSE(written);
    EXPECT_EQ(EFBIG, writeErrno);
    EXPECT_EQ("old", File::readFile(path));
    EXPECT_FALSE(exists(File::tempPathFor(path)));
}

TEST_F(SafeFileTest, RefusesASymlinkAtTheTarget)
{
    // The audit's case: /var/preferences/sam-conf.json pointing at a file root
    // owns, which the old ofstream truncated to "{}".
    const string victim = m_tree.write("victim", "precious");
    const string path = m_tree.path("sam-conf.json");
    ASSERT_EQ(0, symlink(victim.c_str(), path.c_str()));

    errno = 0;
    EXPECT_FALSE(File::writeFile(path, "{}"));
    EXPECT_EQ(ELOOP, errno);
    EXPECT_EQ("precious", File::readFile(victim));
    EXPECT_TRUE(isSymlink(path));
    EXPECT_FALSE(exists(File::tempPathFor(path)));
}

TEST_F(SafeFileTest, RefusesADanglingSymlinkAtTheTarget)
{
    // Following it would create a file wherever the link points.
    const string pointedAt = m_tree.path("created-through-the-link");
    const string path = m_tree.path("sam-conf.json");
    ASSERT_EQ(0, symlink(pointedAt.c_str(), path.c_str()));

    errno = 0;
    EXPECT_FALSE(File::writeFile(path, "{}"));
    EXPECT_EQ(ELOOP, errno);
    EXPECT_FALSE(exists(pointedAt));
    EXPECT_TRUE(isSymlink(path));
}

TEST_F(SafeFileTest, DoesNotFollowASymlinkAtTheTempName)
{
    const string victim = m_tree.write("victim", "precious");
    const string path = m_tree.path("sam-conf.json");
    const string temp = File::tempPathFor(path);
    ASSERT_EQ(0, symlink(victim.c_str(), temp.c_str()));

    ASSERT_TRUE(File::writeFile(path, "{}"));
    EXPECT_EQ("precious", File::readFile(victim));
    EXPECT_EQ("{}", File::readFile(path));
    EXPECT_FALSE(isSymlink(path));
    EXPECT_FALSE(exists(temp));
}

TEST_F(SafeFileTest, DoesNotFollowADanglingSymlinkAtTheTempName)
{
    const string pointedAt = m_tree.path("created-through-the-link");
    const string path = m_tree.path("sam-conf.json");
    ASSERT_EQ(0, symlink(pointedAt.c_str(), File::tempPathFor(path).c_str()));

    ASSERT_TRUE(File::writeFile(path, "{}"));
    EXPECT_FALSE(exists(pointedAt));
    EXPECT_EQ("{}", File::readFile(path));
}

TEST_F(SafeFileTest, ReplacesATempFileLeftByACrash)
{
    const string path = m_tree.write("conf.json", "old");
    m_tree.write(".conf.json.tmp", "half-written ne");

    ASSERT_TRUE(File::writeFile(path, "new"));
    EXPECT_EQ("new", File::readFile(path));
    EXPECT_FALSE(exists(File::tempPathFor(path)));
}

TEST_F(SafeFileTest, FailsWhenTheTempNameCannotBeCleared)
{
    // A non-empty directory at the temp name cannot be unlinked; the write
    // fails and the old file stays.
    const string path = m_tree.write("conf.json", "old");
    m_tree.write(".conf.json.tmp/inside", "x");

    EXPECT_FALSE(File::writeFile(path, "new"));
    EXPECT_EQ("old", File::readFile(path));
}

TEST_F(SafeFileTest, KeepsTheModeOfAnExistingFile)
{
    const string path = m_tree.write("private.json", "old");
    ASSERT_EQ(0, chmod(path.c_str(), 0600));
    ASSERT_TRUE(File::writeFile(path, "new"));
    EXPECT_EQ(0600u, modeOf(path));

    ASSERT_EQ(0, chmod(path.c_str(), 0640));
    ASSERT_TRUE(File::writeFile(path, "newer"));
    EXPECT_EQ(0640u, modeOf(path));
    EXPECT_EQ("newer", File::readFile(path));
}

TEST_F(SafeFileTest, NeverKeepsGroupOrOtherWriteOrSpecialBits)
{
    const string path = m_tree.write("open.json", "old");
    ASSERT_EQ(0, chmod(path.c_str(), 06777));

    ASSERT_TRUE(File::writeFile(path, "new"));
    EXPECT_EQ(0755u, modeOf(path));
}

TEST_F(SafeFileTest, RefusesADirectoryOrAFifoAtTheTarget)
{
    const string dir = m_tree.mkdirs("a-directory");
    errno = 0;
    EXPECT_FALSE(File::writeFile(dir, "{}"));
    EXPECT_EQ(EINVAL, errno);

    const string fifo = m_tree.path("a-fifo");
    ASSERT_EQ(0, mkfifo(fifo.c_str(), 0600));
    errno = 0;
    EXPECT_FALSE(File::writeFile(fifo, "{}"));
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(SafeFileTest, RefusesAPathWithoutAFileName)
{
    errno = 0;
    EXPECT_FALSE(File::writeFile(m_tree.root() + "/", "{}"));
    EXPECT_EQ(EINVAL, errno);
    errno = 0;
    EXPECT_FALSE(File::writeFile(m_tree.root() + "/..", "{}"));
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(SafeFileTest, FollowsASymlinkedDirectory)
{
    // Only the last component is protected: /tmp is itself a symlink on
    // oe-core, and that must keep working.
    m_tree.mkdirs("real");
    const string link = m_tree.path("linked");
    ASSERT_EQ(0, symlink(m_tree.path("real").c_str(), link.c_str()));

    ASSERT_TRUE(File::writeFile(link + "/state", "ok"));
    EXPECT_EQ("ok", File::readFile(m_tree.path("real/state")));
}

TEST_F(SafeFileTest, CreateFileMakesAnEmptyMarker)
{
    const string path = m_tree.path("marker");
    ASSERT_TRUE(File::createFile(path));
    string contents = "untouched";
    EXPECT_TRUE(File::readStateFile(path, contents));
    EXPECT_EQ("", contents);
}

TEST_F(SafeFileTest, CreateFileRefusesASymlink)
{
    const string victim = m_tree.write("victim", "precious");
    const string path = m_tree.path("marker");
    ASSERT_EQ(0, symlink(victim.c_str(), path.c_str()));

    EXPECT_FALSE(File::createFile(path));
    EXPECT_EQ("precious", File::readFile(victim));
}

TEST_F(SafeFileTest, ReadStateFileReadsWhatWriteFileWrote)
{
    const string path = m_tree.path("state.json");
    const string large(10000, 'y');  // more than one read() chunk
    ASSERT_TRUE(File::writeFile(path, large));

    string contents;
    EXPECT_TRUE(File::readStateFile(path, contents));
    EXPECT_EQ(large, contents);
}

TEST_F(SafeFileTest, ReadStateFileRefusesASymlink)
{
    const string real = m_tree.path("real.json");
    ASSERT_TRUE(File::writeFile(real, "{\"planted\": true}"));
    const string path = m_tree.path("state.json");
    ASSERT_EQ(0, symlink(real.c_str(), path.c_str()));

    string contents = "untouched";
    errno = 0;
    EXPECT_FALSE(File::readStateFile(path, contents));
    EXPECT_EQ(ELOOP, errno);
    EXPECT_EQ("", contents);
}

TEST_F(SafeFileTest, ReadStateFileRefusesAFileOthersCanWrite)
{
    const string path = m_tree.write("state.json", "{}");
    string contents;

    ASSERT_EQ(0, chmod(path.c_str(), 0666));
    errno = 0;
    EXPECT_FALSE(File::readStateFile(path, contents));
    EXPECT_EQ(EPERM, errno);

    ASSERT_EQ(0, chmod(path.c_str(), 0620));
    errno = 0;
    EXPECT_FALSE(File::readStateFile(path, contents));
    EXPECT_EQ(EPERM, errno);

    ASSERT_EQ(0, chmod(path.c_str(), 0644));
    EXPECT_TRUE(File::readStateFile(path, contents));
    EXPECT_EQ("{}", contents);
}

TEST_F(SafeFileTest, ReadStateFileRefusesAFileOwnedBySomeoneElse)
{
    if (geteuid() == 0)
        GTEST_SKIP() << "needs a file owned by another uid; root owns them all";
    // Owned by root, mode 0644 or so, on every system this runs on.
    const string path = "/etc/passwd";
    struct stat info;
    ASSERT_EQ(0, stat(path.c_str(), &info));
    ASSERT_NE(geteuid(), info.st_uid);

    string contents;
    errno = 0;
    EXPECT_FALSE(File::readStateFile(path, contents));
    EXPECT_EQ(EPERM, errno);
}

TEST_F(SafeFileTest, ReadStateFileRefusesAFifoWithoutBlocking)
{
    const string fifo = m_tree.path("state.json");
    ASSERT_EQ(0, mkfifo(fifo.c_str(), 0600));

    string contents;
    errno = 0;
    EXPECT_FALSE(File::readStateFile(fifo, contents));
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(SafeFileTest, ReadStateFileReportsAMissingFile)
{
    string contents;
    errno = 0;
    EXPECT_FALSE(File::readStateFile(m_tree.path("nothing-here"), contents));
    EXPECT_EQ(ENOENT, errno);
}

TEST_F(SafeFileTest, NativeAppLogDoesNotFollowASymlink)
{
    const string victim = m_tree.write("victim", "precious");
    const string log = m_tree.path("com.example.app-0");
    ASSERT_EQ(0, symlink(victim.c_str(), log.c_str()));

    NativeProcess process;
    process.openStdFile(log);
    process.closeStdFd();

    // O_TRUNC through the link would have emptied it.
    EXPECT_EQ("precious", File::readFile(victim));
}

TEST_F(SafeFileTest, NativeAppLogIsCreated)
{
    const string log = m_tree.path("com.example.app-1");

    NativeProcess process;
    process.openStdFile(log);
    process.closeStdFd();

    EXPECT_TRUE(File::isFile(log));
}

// The read-write sam-conf.json, through SAMConf itself: a symlink there must
// neither be read nor be written through.
class SAMConfSymlinkTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const char* home = getenv("HOME");
        m_previousHome = (home != nullptr) ? home : "";
        setenv("HOME", m_tree.root().c_str(), 1);
        RuntimeInfo::getInstance().initialize();
    }

    void TearDown() override
    {
        // As in SAMConfLocaleTest: leave the locale empty, and do not reload
        // the config once the real $HOME is back.
        SAMConf::getInstance().setLocale("", "", "");
        if (m_previousHome.empty())
            unsetenv("HOME");
        else
            setenv("HOME", m_previousHome.c_str(), 1);
        RuntimeInfo::getInstance().initialize();
    }

    TempTree m_tree;
    string m_previousHome;
};

TEST_F(SAMConfSymlinkTest, ASymlinkedConfigIsNeitherReadNorWrittenThrough)
{
    const string victim = m_tree.write("victim.json", "{\"language\": \"planted\"}");
    m_tree.mkdirs(".config");
    const string conf = m_tree.path(".config/sam-conf.json");
    ASSERT_EQ(0, symlink(victim.c_str(), conf.c_str()));

    SAMConf::getInstance().initialize();
    EXPECT_EQ("", SAMConf::getInstance().getLanguage());

    SAMConf::getInstance().setLocale("en", "", "US");
    EXPECT_EQ("{\"language\": \"planted\"}", File::readFile(victim));
    struct stat info;
    ASSERT_EQ(0, lstat(conf.c_str(), &info));
    EXPECT_TRUE(S_ISLNK(info.st_mode));
}
