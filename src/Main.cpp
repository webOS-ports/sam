// Copyright (c) 2012-2020 LG Electronics, Inc.
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

#include <stdlib.h>

#include <gio/gio.h>

#include "MainDaemon.h"
#include "util/Logger.h"
#include "util/SignalHandler.h"

static const char* CLASS_NAME = "Main";

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    Logger::info(CLASS_NAME, __FUNCTION__, "Start SAM process");

    try {
        MainDaemon::getInstance().initialize();

        // After initialize(), so there is a main loop to quit, and before
        // start(), so a signal arriving during the first dispatch is queued
        // rather than delivered. From here on signals reach SAM as main-loop
        // events; SignalHandler explains why they must not reach it as a
        // signal handler.
        if (!SignalHandler::initialize(MainDaemon::getInstance().getMainLoop()))
            Logger::warning(CLASS_NAME, __FUNCTION__, "Running with default signal dispositions");

        MainDaemon::getInstance().start();

        SignalHandler::finalize();
        MainDaemon::getInstance().finalize();
    } catch(...) {
        Logger::info(CLASS_NAME, __FUNCTION__, "Failed to start SAM");
    }
    return EXIT_SUCCESS;
}
