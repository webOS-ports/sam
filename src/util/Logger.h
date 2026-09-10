// Copyright (c) 2019-2020 LG Electronics, Inc.
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

#ifndef UTIL_LOGGER_H_
#define UTIL_LOGGER_H_

#include <iostream>
#include <map>

#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>

using namespace std;
using namespace LS;
using namespace pbnjson;

enum ErrCode {
    ErrCode_NOERROR = 0,
    ErrCode_UNKNOWN = 1,
    ErrCode_GENERAL = 2,
    ErrCode_INVALID_PAYLOAD = 3,
    ErrCode_LAUNCH = 10,
    ErrCode_LAUNCH_APP_LOCKED = 11,
    ErrCode_RELAUNCH = 20,
    ErrCode_PAUSE = 30,
    ErrCode_CLOSE = 40,
    ErrCode_DEPRECATED = 999
};

enum LogLevel {
    LogLevel_DEBUG,
    LogLevel_INFO,
    LogLevel_WARNING,
    LogLevel_ERROR,
};

enum LogType {
    LogType_CONSOLE,
    LogType_PMLOG
};

class Logger {
public:
    template<typename ... Args>
    static const string format(const string& format, Args ... args)
    {
        // The buffer used to be static, which raced between the glib main loop
        // thread and the luna-service threads and silently truncated at 1024.
        // Measure first, then format into a string sized to fit.
#pragma GCC diagnostic push
// Every caller passes a literal, but the format only becomes one here, so the
// compiler cannot see through the pack to check it. -Wformat-security matters
// too: without it a zero-argument format() is a hard error under the recipe's
// -Werror=format-security.
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
        const int needed = snprintf(nullptr, 0, format.c_str(), args ...);
        if (needed < 0)
            return string();

        string buffer(static_cast<size_t>(needed) + 1, '\0');
        snprintf(&buffer[0], buffer.size(), format.c_str(), args ...);
#pragma GCC diagnostic pop
        buffer.resize(static_cast<size_t>(needed));
        return buffer;
    }

    static const char* toString(bool BOOL)
    {
        const char* const TStr = "true";
        const char* const FStr = "false";

        if (BOOL)
            return TStr;
        else
            return FStr;
    }

    static bool isVerbose()
    {
        return s_isVerbose;
    }

    static void logAPIRequest(const string& className, const string& functionName, Message& request, JValue& requestPayload);
    static void logAPIResponse(const string& className, const string& functionName, Message& request, JValue& responsePayload);

    static void logCallRequest(const string& className, const string& functionName, const string& method, JValue& requestPayload);
    static void logCallResponse(const string& className, const string& functionName, Message& response, JValue& responsePayload);

    static void logSubscriptionRequest(const string& className, const string& functionName, const string& method, JValue& requestPayload);
    static void logSubscriptionResponse(const string& className, const string& functionName, Message& response, JValue& subscriptionPayload);
    static void logSubscriptionPost(const string& className, const string& functionName, const LS::SubscriptionPoint& point, JValue& subscriptionPayload);
    static void logSubscriptionPost(const string& className, const string& functionName, const string& key, JValue& subscriptionPayload);

    static void debug(const string& className, const string& functionName, const string& what);
    static void info(const string& className, const string& functionName, const string& what);
    static void warning(const string& className, const string& functionName, const string& what);
    static void error(const string& className, const string& functionName, const string& what);

    static void debug(const string& className, const string& functionName, const string& who, const string& what, const string& detail = EMPTY);
    static void info(const string& className, const string& functionName, const string& who, const string& what, const string& detail = EMPTY);
    static void warning(const string& className, const string& functionName, const string& who, const string& what, const string& detail = EMPTY);
    static void error(const string& className, const string& functionName, const string& who, const string& what, const string& detail = EMPTY);

    virtual ~Logger();

    static Logger& getInstance()
    {
        static Logger _instance;
        return _instance;
    }

    void setLevel(enum LogLevel level);
    void setType(enum LogType type);

private:
    static const string EMPTY;
    static bool s_isVerbose;

    static const string& toString(const enum LogLevel& level);

    Logger();

    void write(const enum LogLevel& level, const string& className, const string& functionName, const string& who, const string& what, const string& detail);
    static void writeConsole(const enum LogLevel& level, const string& className, const string& functionName, const string& who, const string& what, const string& detail);
    static void writePmlog(const enum LogLevel& level, const string& className, const string& functionName, const string& who, const string& what, const string& detail);

    enum LogLevel m_level;
    enum LogType m_type;
};

#endif /* UTIL_LOGGER_H_ */
