// Copyright (c) 2026 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef UTIL_SIGNAL_HANDLER_H
#define UTIL_SIGNAL_HANDLER_H

#include <glib.h>
#include <sys/signalfd.h>

#include <functional>
#include <string>

/**
 * Delivers the signals SAM cares about through the glib main loop instead of
 * through a signal handler.
 *
 * SAM used to run its diagnostics and its shutdown from inside an SA_SIGINFO
 * handler: it built std::string and std::stringstream values, called
 * Logger::warning(), read /proc/<pid>/cmdline through std::ifstream, and then
 * called MainDaemon::stop(). None of that is async-signal-safe. A SIGTERM that
 * lands while the main thread is inside malloc, inside pmlog or inside a
 * luna-service call re-enters those subsystems on the same thread.
 *
 * On the shutdown path that is not theoretical. ls-hubd drops peer services
 * first, so SAM is already inside LSCallCancel() unwinding its subscriptions
 * when systemd sends SIGTERM, and the journal shows the collision directly:
 *
 *     LS_CANC_METH  "Could not find call 45 to cancel."  LSCallCancel
 *     LS_ASSERT     _lshandle_validate  "Invalid LSHandle"  base.c:213
 *     kernel: sam[800]: segfault at 27 ... in libc.so.6
 *
 * A signalfd has no handler at all. The signals are blocked process-wide, the
 * kernel queues them, and the main loop reads them as ordinary file-descriptor
 * events - so the logging and the teardown below run on the main thread, at a
 * point where the main loop is between dispatches rather than part-way through
 * one.
 *
 * Not every signal belongs here. SIGABRT and SIGFPE are synchronous faults
 * raised by the thread that caused them; blocking those is undefined behaviour
 * and quitting the main loop in response hides the fault behind a clean exit.
 * They keep the default disposition. SIGPIPE is set to SIG_IGN so writes report
 * EPIPE, which is what the old handler achieved by catching and ignoring it.
 */
class SignalHandler {
public:
    /**
     * Block the handled signals and attach a signalfd to the main loop.
     *
     * Call before any thread is created: the signal mask set here is inherited
     * by threads started afterwards, and a thread that has not blocked these
     * signals can still take delivery through a handler.
     *
     * @param  mainLoop the loop to quit when a terminating signal arrives
     * @return false if the signalfd could not be created; the caller keeps the
     *         default dispositions in that case rather than running unhandled
     */
    static bool initialize(GMainLoop* mainLoop);

    /** Detach the source, close the fd and restore the previous signal mask. */
    static void finalize();

    /**
     * Undo initialize()'s process-wide effects in a freshly forked child,
     * before it execs.
     *
     * A signal mask survives exec, and so does an ignored disposition. Without
     * this, everything SAM launches inherits SIGTERM, SIGQUIT, SIGHUP and
     * SIGINT blocked and SIGPIPE ignored - so nothing can terminate those
     * children by signal. On the shutdown path that shows up as SAM's own unit
     * timing out: the apps in its cgroup sit through SIGTERM and only die on
     * the SIGKILL that follows, costing a full TimeoutStopSec every shutdown.
     *
     * The old sigaction-based handler had no such problem: exec resets
     * *handlers* to the default. Only blocking and SIG_IGN carry over, and
     * both are ours to undo.
     *
     * Async-signal-safe, as a post-fork child-setup callback has to be:
     * sigprocmask() and signal() are both on the POSIX list.
     */
    static void resetForChild();

    /** True between a successful initialize() and finalize(). */
    static bool isInitialized();

    /**
     * The signalfd, or -1. Exposed so tests can wait on it; nothing in the
     * daemon reads it directly.
     */
    static int getFd();

    /**
     * Read and act on one queued signal, without running a main loop.
     *
     * Returns false when nothing is queued. The main loop calls this through
     * its own callback, and tests call it directly so they do not have to run
     * a loop to observe the effect.
     */
    static bool dispatchOnce();

    /**
     * Does this signal stop the daemon? SIGHUP, SIGINT and SIGPIPE are logged
     * and otherwise ignored, as they were before.
     */
    static bool isTerminating(int signo);

    /** The line the old handler logged, built from what signalfd reports. */
    static std::string describe(const struct signalfd_siginfo& info);

private:
    static gboolean onSignalFd(gint fd, GIOCondition condition, gpointer userData);

    static GMainLoop* s_mainLoop;
    static int s_fd;
    static guint s_source;
    static bool s_initialized;
};

#endif
