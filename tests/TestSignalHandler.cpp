// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <glib.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include "util/SignalHandler.h"

// SAM used to do its shutdown from inside an SA_SIGINFO handler: it built
// std::string and std::stringstream values, called Logger::warning(), read
// /proc/<pid>/cmdline through std::ifstream and then quit the glib main loop -
// none of it async-signal-safe. On the shutdown path SIGTERM lands while SAM is
// already unwinding subscriptions inside LSCallCancel(), and the daemon
// segfaulted in libc there on every poweroff.
//
// The fix is structural rather than a narrowing of what the handler does: the
// signals are blocked and read back through a signalfd, so the work happens on
// the main loop and there is no signal context to be unsafe in. These tests
// pin that structure down, because "we do less in the handler" is the kind of
// property that quietly erodes.
class SignalHandlerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_mainLoop = g_main_loop_new(NULL, FALSE);
        ASSERT_TRUE(SignalHandler::initialize(m_mainLoop));
    }

    void TearDown() override
    {
        // The singletons here are process-global: leaving the signals blocked
        // or the fd open would leak into whichever test runs next.
        SignalHandler::finalize();
        // resetForChild() deliberately undoes initialize()'s process-wide
        // state; finalize() above restores the mask either way, but SIGPIPE
        // has to be put back by hand for the tests that follow.
        signal(SIGPIPE, SIG_DFL);
        if (m_mainLoop) {
            g_main_loop_unref(m_mainLoop);
            m_mainLoop = nullptr;
        }
        EXPECT_FALSE(SignalHandler::isInitialized());
    }

    // Wait for the signalfd to become readable. Returns false on timeout so a
    // broken implementation fails the test instead of hanging the suite.
    static bool waitReadable(int timeoutMs = 2000)
    {
        struct pollfd pfd = { SignalHandler::getFd(), POLLIN, 0 };
        return poll(&pfd, 1, timeoutMs) == 1 && (pfd.revents & POLLIN) != 0;
    }

    GMainLoop* m_mainLoop = nullptr;
};

// The regression guard proper. The old code reached this path by installing a
// handler; if anyone does that again, the signal is delivered asynchronously
// and every argument above stops holding. SIG_DFL here means nothing of SAM's
// runs in signal context.
TEST_F(SignalHandlerTest, InstallsNoHandlerForTheSignalsItTakes)
{
    for (int signo : { SIGTERM, SIGQUIT, SIGHUP, SIGINT }) {
        struct sigaction current = {};
        ASSERT_EQ(0, sigaction(signo, nullptr, &current)) << "signal " << signo;

        EXPECT_EQ(SIG_DFL, current.sa_handler) << "signal " << signo << " has a handler installed";
        EXPECT_EQ(0, current.sa_flags & SA_SIGINFO) << "signal " << signo << " has an SA_SIGINFO handler";
    }
}

// Blocking is what makes the signalfd the only delivery path. Without it the
// default disposition still runs and SIGTERM kills the process outright.
TEST_F(SignalHandlerTest, BlocksThoseSignalsProcessWide)
{
    sigset_t blocked;
    sigemptyset(&blocked);
    ASSERT_EQ(0, sigprocmask(SIG_BLOCK, nullptr, &blocked));

    EXPECT_EQ(1, sigismember(&blocked, SIGTERM));
    EXPECT_EQ(1, sigismember(&blocked, SIGQUIT));
    EXPECT_EQ(1, sigismember(&blocked, SIGHUP));
    EXPECT_EQ(1, sigismember(&blocked, SIGINT));

    // Synchronous faults are deliberately left alone: blocking them is
    // undefined behaviour and a signalfd cannot report one usefully.
    EXPECT_EQ(0, sigismember(&blocked, SIGABRT));
    EXPECT_EQ(0, sigismember(&blocked, SIGFPE));
}

// Raising SIGTERM in a process that has not been killed by it is the whole
// point: reaching the next line proves the default disposition did not run.
TEST_F(SignalHandlerTest, DeliversSigtermAsAReadableEventInsteadOfKillingUs)
{
    ASSERT_EQ(0, raise(SIGTERM));

    ASSERT_TRUE(waitReadable()) << "SIGTERM did not arrive on the signalfd";
    EXPECT_TRUE(SignalHandler::dispatchOnce());
}

TEST_F(SignalHandlerTest, QuitsTheMainLoopOnATerminatingSignal)
{
    ASSERT_EQ(0, raise(SIGTERM));
    ASSERT_TRUE(waitReadable());

    // g_main_loop_quit() on a loop that is not running still clears the flag
    // g_main_loop_run() checks, so is_running() is the observable effect here
    // without having to spin a loop up and race it.
    ASSERT_TRUE(SignalHandler::dispatchOnce());
    EXPECT_FALSE(g_main_loop_is_running(m_mainLoop));
}

// SIGHUP and SIGINT were logged and ignored before, and still are. A daemon
// that exits on SIGHUP would be a behaviour change, not a bug fix.
TEST_F(SignalHandlerTest, DoesNotQuitOnSignalsItOnlyReports)
{
    for (int signo : { SIGHUP, SIGINT }) {
        ASSERT_EQ(0, raise(signo)) << "signal " << signo;
        ASSERT_TRUE(waitReadable()) << "signal " << signo;
        ASSERT_TRUE(SignalHandler::dispatchOnce()) << "signal " << signo;

        EXPECT_FALSE(SignalHandler::isTerminating(signo)) << "signal " << signo;
    }
}

TEST_F(SignalHandlerTest, ClassifiesSignalsTheWayTheOldHandlerDid)
{
    EXPECT_TRUE(SignalHandler::isTerminating(SIGTERM));
    EXPECT_TRUE(SignalHandler::isTerminating(SIGQUIT));
    EXPECT_FALSE(SignalHandler::isTerminating(SIGHUP));
    EXPECT_FALSE(SignalHandler::isTerminating(SIGINT));
    EXPECT_FALSE(SignalHandler::isTerminating(SIGPIPE));
}

// SIGPIPE has to stop killing the process, and it must do so without a handler:
// SIG_IGN is the disposition, so a write to a dead peer returns EPIPE.
TEST_F(SignalHandlerTest, IgnoresSigpipeWithoutAHandler)
{
    struct sigaction current = {};
    ASSERT_EQ(0, sigaction(SIGPIPE, nullptr, &current));

    EXPECT_EQ(SIG_IGN, current.sa_handler);
}

// Several signals can be queued before the loop gets a turn - exactly what
// happens when SAM is busy tearing down subscriptions. All of them have to come
// back out, not just the first.
TEST_F(SignalHandlerTest, DrainsMoreThanOneQueuedSignal)
{
    ASSERT_EQ(0, raise(SIGHUP));
    ASSERT_EQ(0, raise(SIGINT));
    ASSERT_TRUE(waitReadable());

    EXPECT_TRUE(SignalHandler::dispatchOnce());
    EXPECT_TRUE(SignalHandler::dispatchOnce());
    // ...and then nothing, rather than a short read reported as an event.
    EXPECT_FALSE(SignalHandler::dispatchOnce());
}

TEST_F(SignalHandlerTest, NamesTheSenderItCanStillRead)
{
    struct signalfd_siginfo info = {};
    info.ssi_signo = SIGTERM;
    info.ssi_code = 0;
    info.ssi_pid = static_cast<uint32_t>(getpid());
    info.ssi_uid = 0;

    // Our own /proc/<pid>/cmdline is readable, so the sender branch is taken
    // and the pid is reported alongside it.
    const std::string described = SignalHandler::describe(info);

    EXPECT_NE(std::string::npos, described.find("signal(15)"));
    EXPECT_NE(std::string::npos, described.find("sender("));
    EXPECT_NE(std::string::npos, described.find("si_pid(" + std::to_string(getpid()) + ")"));
}

TEST_F(SignalHandlerTest, FallsBackToPidWhenTheSenderIsAlreadyGone)
{
    struct signalfd_siginfo info = {};
    info.ssi_signo = SIGTERM;
    info.ssi_pid = 0;         // kernel-generated, or a sender that has exited
    info.ssi_uid = 0;

    const std::string described = SignalHandler::describe(info);

    EXPECT_NE(std::string::npos, described.find("si_pid(0)"));
    EXPECT_EQ(std::string::npos, described.find("sender("));
}

// The regression this guards is subtle and cost a full stop timeout on every
// shutdown: a signal mask survives exec, so every application SAM launched
// inherited SIGTERM blocked and sat through it, and SAM's unit only finished
// stopping once systemd escalated to SIGKILL. Handlers would have been reset by
// exec on their own; blocking and SIG_IGN are not.
TEST_F(SignalHandlerTest, ResetForChildUnblocksEverythingItBlocked)
{
    sigset_t blocked;
    sigemptyset(&blocked);
    ASSERT_EQ(0, sigprocmask(SIG_BLOCK, nullptr, &blocked));
    ASSERT_EQ(1, sigismember(&blocked, SIGTERM)) << "fixture should start with SIGTERM blocked";

    SignalHandler::resetForChild();

    sigemptyset(&blocked);
    ASSERT_EQ(0, sigprocmask(SIG_BLOCK, nullptr, &blocked));
    for (int signo : { SIGTERM, SIGQUIT, SIGHUP, SIGINT })
        EXPECT_EQ(0, sigismember(&blocked, signo)) << "signal " << signo << " still blocked in the child";
}

// SIG_IGN survives exec too, so a child would inherit SIGPIPE ignored and
// silently keep writing into dead sockets instead of dying like any other
// program.
TEST_F(SignalHandlerTest, ResetForChildPutsSigpipeBackToDefault)
{
    struct sigaction before = {};
    ASSERT_EQ(0, sigaction(SIGPIPE, nullptr, &before));
    ASSERT_EQ(SIG_IGN, before.sa_handler) << "fixture should start with SIGPIPE ignored";

    SignalHandler::resetForChild();

    struct sigaction after = {};
    ASSERT_EQ(0, sigaction(SIGPIPE, nullptr, &after));
    EXPECT_EQ(SIG_DFL, after.sa_handler);
}

// initialize() is called once from main(), but a second call must not swap the
// fd out from under the source that is already watching it.
TEST_F(SignalHandlerTest, IsIdempotent)
{
    const int fd = SignalHandler::getFd();

    EXPECT_TRUE(SignalHandler::initialize(m_mainLoop));
    EXPECT_EQ(fd, SignalHandler::getFd());
}
