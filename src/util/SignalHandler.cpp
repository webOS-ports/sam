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

#include "util/SignalHandler.h"

#include <errno.h>
#include <glib-unix.h>
#include <signal.h>
#include <unistd.h>

#include "util/File.h"
#include "util/Logger.h"

static const char* CLASS_NAME = "SignalHandler";

GMainLoop* SignalHandler::s_mainLoop = nullptr;
int SignalHandler::s_fd = -1;
guint SignalHandler::s_source = 0;
bool SignalHandler::s_initialized = false;
static sigset_t s_previousMask;

// The signals delivered through the fd. SIGABRT and SIGFPE are deliberately
// absent: they are synchronous faults, blocking them is undefined behaviour,
// and a signalfd cannot report one usefully anyway. See the header.
static const int HANDLED_SIGNALS[] = { SIGTERM, SIGQUIT, SIGHUP, SIGINT };

static void fillHandledSet(sigset_t& mask)
{
    sigemptyset(&mask);
    for (int signo : HANDLED_SIGNALS)
        sigaddset(&mask, signo);
}

bool SignalHandler::initialize(GMainLoop* mainLoop)
{
    if (s_initialized)
        return true;

    // Writes to a socket whose peer has gone report EPIPE instead of killing
    // the process. The old handler achieved this by catching SIGPIPE and
    // returning; SIG_IGN does the same without running any code in signal
    // context.
    signal(SIGPIPE, SIG_IGN);

    sigset_t mask;
    fillHandledSet(mask);

    // Block first, then open the fd. Between the two the signals are queued
    // rather than delivered, so nothing is lost and nothing runs a default
    // disposition we did not intend.
    if (sigprocmask(SIG_BLOCK, &mask, &s_previousMask) != 0) {
        Logger::warning(CLASS_NAME, __FUNCTION__,
                        Logger::format("sigprocmask failed (errno %d), keeping default signal handling", errno));
        return false;
    }

    s_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (s_fd < 0) {
        Logger::warning(CLASS_NAME, __FUNCTION__,
                        Logger::format("signalfd failed (errno %d), keeping default signal handling", errno));
        sigprocmask(SIG_SETMASK, &s_previousMask, nullptr);
        return false;
    }

    s_mainLoop = mainLoop;
    s_source = g_unix_fd_add(s_fd, G_IO_IN, &SignalHandler::onSignalFd, nullptr);
    s_initialized = true;
    return true;
}

void SignalHandler::finalize()
{
    if (!s_initialized)
        return;

    if (s_source != 0) {
        g_source_remove(s_source);
        s_source = 0;
    }
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
    sigprocmask(SIG_SETMASK, &s_previousMask, nullptr);
    s_mainLoop = nullptr;
    s_initialized = false;
}

void SignalHandler::resetForChild()
{
    sigset_t mask;
    fillHandledSet(mask);
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    signal(SIGPIPE, SIG_DFL);
}

bool SignalHandler::isInitialized()
{
    return s_initialized;
}

int SignalHandler::getFd()
{
    return s_fd;
}

bool SignalHandler::isTerminating(int signo)
{
    return signo == SIGTERM || signo == SIGQUIT;
}

std::string SignalHandler::describe(const struct signalfd_siginfo& info)
{
    const std::string senderPid = std::to_string(info.ssi_pid);
    const std::string cmdline = File::readFile("/proc/" + senderPid + "/cmdline");

    // Same two shapes the old handler logged, so existing log greps still
    // match: the sender's command line when it can still be read, and the
    // bare pid/uid when the sender has already gone.
    if (cmdline.empty()) {
        return Logger::format("signal(%d) si_code(%d) si_pid(%s), si_uid(%d)",
                              info.ssi_signo, info.ssi_code, senderPid.c_str(), info.ssi_uid);
    }
    return Logger::format("signal(%d) si_code(%d) sender(%s) si_pid(%s), si_uid(%d)",
                          info.ssi_signo, info.ssi_code, cmdline.c_str(), senderPid.c_str(), info.ssi_uid);
}

bool SignalHandler::dispatchOnce()
{
    if (s_fd < 0)
        return false;

    struct signalfd_siginfo info;
    const ssize_t got = read(s_fd, &info, sizeof(info));
    if (got != static_cast<ssize_t>(sizeof(info)))
        return false;

    // Everything below here is ordinary main-thread work: allocation, pmlog
    // and /proc reads are all fine now that this is a file-descriptor callback
    // rather than a signal handler.
    Logger::warning(CLASS_NAME, __FUNCTION__, describe(info));

    if (!isTerminating(info.ssi_signo)) {
        Logger::warning(CLASS_NAME, __FUNCTION__, "Ignore received signal");
        return true;
    }

    Logger::warning(CLASS_NAME, __FUNCTION__, "Try to terminate SAM process");
    if (s_mainLoop)
        g_main_loop_quit(s_mainLoop);
    return true;
}

gboolean SignalHandler::onSignalFd(gint fd, GIOCondition condition, gpointer userData)
{
    (void) fd;
    (void) userData;

    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        Logger::warning(CLASS_NAME, __FUNCTION__, "signalfd closed unexpectedly");
        s_source = 0;
        return G_SOURCE_REMOVE;
    }

    // One readable event can carry several queued signals.
    while (dispatchOnce())
        ;
    return G_SOURCE_CONTINUE;
}
