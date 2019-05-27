// Copyright (c) 2012-2018 LG Electronics, Inc.
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

#ifndef APP_LAUNCHING_ITEM_H_
#define APP_LAUNCHING_ITEM_H_

#include <luna-service2/lunaservice.h>
#include <list>
#include <pbnjson.hpp>

#include "core/lifecycle/application_errors.h"
#include "core/package/application_description.h"

const std::string SYS_LAUNCHING_UID = "alertId";

enum class AppLaunchRequestType {
    INTERNAL = 0,
    EXTERNAL,
    EXTERNAL_FOR_VIRTUALAPP,
};

enum class AppLaunchingStage {
    NONE = 0,
    PRELAUNCH,
    MEMORY_CHECK,
    LAUNCH,
    DONE,
};

class AppLaunchingItem {
public:
    AppLaunchingItem(const std::string& app_id, AppLaunchRequestType rtype, const pbnjson::JValue& params, LSMessage* lsmsg);
    virtual ~AppLaunchingItem();

    const std::string& uid() const
    {
        return m_uid;
    }
    const std::string& appId() const
    {
        return m_appId;
    }
    const std::string& pid() const
    {
        return m_pid;
    }
    const std::string& requestedAppId() const
    {
        return m_requestedAppId;
    }
    bool isRedirected() const
    {
        return m_redirected;
    }
    const AppLaunchingStage& stage() const
    {
        return m_stage;
    }
    const int& subStage() const
    {
        return m_subStage;
    }
    const std::string& callerId() const
    {
        return m_callerId;
    }
    const std::string& callerPid() const
    {
        return m_callerPid;
    }
    bool showSplash() const
    {
        return m_showSplash;
    }
    bool showSpinner() const
    {
        return m_showSpinner;
    }
    const std::string& preload() const
    {
        return m_preload;
    }
    bool keepAlive() const
    {
        return m_keepAlive;
    }
    bool automaticLaunch() const
    {
        return m_automaticLaunch;
    }
    const pbnjson::JValue& params() const
    {
        return mParams;
    }
    LSMessage* lsmsg() const
    {
        return m_lsmsg;
    }
    LSMessageToken returnToken() const
    {
        return m_returnToken;
    }
    const pbnjson::JValue& returnJmsg() const
    {
        return m_returnJmsg;
    }
    int errCode() const
    {
        return m_errCode;
    }
    const std::string& errText() const
    {
        return m_errText;
    }
    const double& launchStartTime() const
    {
        return m_launchStartTime;
    }
    const std::string& launchReason() const
    {
        return m_launchReason;
    }
    bool isLastInputApp() const
    {
        return m_isLastInputApp;
    }

    // re-code redirection for app_desc and consider other values again
    bool setRedirection(const std::string& target_app_id, const pbnjson::JValue& new_params);
    void setStage(AppLaunchingStage stage)
    {
        m_stage = stage;
    }
    void setSubStage(const int stage)
    {
        m_subStage = stage;
    }
    void setPid(const std::string& pid)
    {
        m_pid = pid;
    }
    void setCallerId(const std::string& id)
    {
        m_callerId = id;
    }
    void setCallerPid(const std::string& pid)
    {
        m_callerPid = pid;
    }
    void setShowSplash(bool v)
    {
        m_showSplash = v;
    }
    void setShowSpinner(bool v)
    {
        m_showSpinner = v;
    }
    void setPreload(const std::string& preload)
    {
        m_preload = preload;
    }
    void setKeepAlive(bool v)
    {
        m_keepAlive = v;
    }
    void setAutomaticLaunch(bool v)
    {
        m_automaticLaunch = v;
    }
    void setReturnToken(LSMessageToken token)
    {
        m_returnToken = token;
    }
    void resetReturnToken()
    {
        m_returnToken = 0;
    }
    void setCallReturnJmsg(const pbnjson::JValue& jmsg)
    {
        m_returnJmsg = jmsg.duplicate();
    }
    void setErrCodeText(int code, std::string err)
    {
        m_errCode = code;
        m_errText = err;
    }
    void setErrCode(int code)
    {
        m_errCode = code;
    }
    void setErrText(std::string err)
    {
        m_errText = err;
    }
    void setLaunchStartTime(const double& start_time)
    {
        m_launchStartTime = start_time;
    }
    void setLaunchReason(const std::string& launch_reason)
    {
        m_launchReason = launch_reason;
    }
    void setLastInputApp(bool v)
    {
        m_isLastInputApp = v;
    }

private:
    std::string m_uid;
    std::string m_appId;
    std::string m_pid;
    std::string m_requestedAppId;
    bool m_redirected;
    AppLaunchRequestType m_rtype;
    AppLaunchingStage m_stage;
    int m_subStage;
    pbnjson::JValue mParams;
    LSMessage* m_lsmsg;
    std::string m_callerId;
    std::string m_callerPid;
    bool m_showSplash;
    bool m_showSpinner;
    std::string m_preload;
    bool m_keepAlive;
    bool m_automaticLaunch;
    LSMessageToken m_returnToken;
    pbnjson::JValue m_returnJmsg;
    int m_errCode;
    std::string m_errText;
    double m_launchStartTime;
    std::string m_launchReason;
    bool m_isLastInputApp;
};

typedef std::shared_ptr<AppLaunchingItem> AppLaunchingItemPtr;
typedef std::list<AppLaunchingItemPtr> AppLaunchingItemList;

#endif
