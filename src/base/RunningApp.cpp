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

#include "RunningApp.h"

#include "bus/client/AbsLifeHandler.h"
#include "bus/service/ApplicationManager.h"
#include "conf/SAMConf.h"

const string RunningApp::CLASS_NAME = "RunningApp";

const char* RunningApp::toString(LifeStatus status)
{
    switch (status) {
    case LifeStatus::LifeStatus_STOP:
        return "stop";

    case LifeStatus::LifeStatus_PRELOADING:
        return "preloading";

    case LifeStatus::LifeStatus_PRELOADED:
        return "preloaded";

    case LifeStatus::LifeStatus_SPLASHING:
        return "splashing";

    case LifeStatus::LifeStatus_SPLASHED:
        return "splashed";

    case LifeStatus::LifeStatus_LAUNCHING:
        return "launching";

    case LifeStatus::LifeStatus_RELAUNCHING:
        return "relaunching";

    case LifeStatus::LifeStatus_FOREGROUND:
        return "foreground";

    case LifeStatus::LifeStatus_BACKGROUND:
        return "background";

    case LifeStatus::LifeStatus_PAUSING:
        return "pausing";

    case LifeStatus::LifeStatus_PAUSED:
        return "paused";

    case LifeStatus::LifeStatus_CLOSING:
        return "closing";
    }
    return "unknown";
}

bool RunningApp::isTransition(LifeStatus status)
{
    switch (status) {
    case LifeStatus::LifeStatus_STOP:
        return false;

    case LifeStatus::LifeStatus_PRELOADING:
        return true;

    case LifeStatus::LifeStatus_PRELOADED:
        return false;

    case LifeStatus::LifeStatus_SPLASHING:
        return true;

    case LifeStatus::LifeStatus_SPLASHED:
        return false;

    case LifeStatus::LifeStatus_LAUNCHING:
        return true;

    case LifeStatus::LifeStatus_RELAUNCHING:
        return true;

    case LifeStatus::LifeStatus_FOREGROUND:
        return false;

    case LifeStatus::LifeStatus_BACKGROUND:
        return false;

    case LifeStatus::LifeStatus_PAUSING:
        return true;

    case LifeStatus::LifeStatus_PAUSED:
        return false;

    case LifeStatus::LifeStatus_CLOSING:
        return true;
    }
    return false;
}

string RunningApp::generateInstanceId(int displayId)
{
    string instanceId = Time::generateUid();
    instanceId += std::to_string(displayId);
    return instanceId;
}

int RunningApp::getDisplayId(const string& instanceId)
{
    int displayId = instanceId.back() - '0';
    if (displayId < 0 || displayId > 10)
        displayId = 0;
    return displayId;
}

RunningApp::RunningApp(LaunchPointPtr launchPoint)
    : m_launchPoint(launchPoint),
      m_instanceId(""),
      m_displayId(-1),
      m_webprocessid(""),
      m_isFullWindow(true),
      m_lifeStatus(LifeStatus::LifeStatus_STOP),
      m_launchCount(0),
      m_killingTimer(0),
      m_keepAlive(false),
      m_noSplash(true),
      m_spinner(true),
      m_isHidden(false),
      m_token(0),
      m_context(0),
      m_isRegistered(false)
{
}

RunningApp::~RunningApp()
{
    stopKillingTimer();
}

void RunningApp::launch(LunaTaskPtr lunaTask)
{
    AbsLifeHandler::getLifeHandler(*this).launch(*this, lunaTask);
}

void RunningApp::relaunch(LunaTaskPtr lunaTask)
{
    if (isRegistered() && SAMConf::getInstance().isAppRelaunchSupported()) {
        setLifeStatus(LifeStatus::LifeStatus_LAUNCHING);
        JValue payload = getRelaunchParams(lunaTask);
        if (!sendEvent(payload)) {
            LunaTaskList::getInstance().removeAfterReply(lunaTask, ErrCode_LAUNCH, "Failed to send relaunch event");
            return;
        }

        lunaTask->toJson(lunaTask->getResponsePayload(), false, true);
        LunaTaskList::getInstance().removeAfterReply(lunaTask);
        return;
    }
    AbsLifeHandler::getLifeHandler(*this).relaunch(*this, lunaTask);
}

void RunningApp::pause(LunaTaskPtr lunaTask)
{
    AbsLifeHandler::getLifeHandler(*this).pause(*this, lunaTask);
}

void RunningApp::close(LunaTaskPtr lunaTask)
{
    AbsLifeHandler::getLifeHandler(*this).term(*this, lunaTask);

    if (m_lifeStatus == LifeStatus::LifeStatus_CLOSING) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_instanceId, "The instance is already closing");
        lunaTask->toJson(lunaTask->getResponsePayload(), false, true);
        LunaTaskList::getInstance().removeAfterReply(lunaTask);
        return;
    }
}

void RunningApp::registerApp(LunaTaskPtr lunaTask)
{
    if (m_isRegistered) {
        LunaTaskList::getInstance().removeAfterReply(lunaTask, ErrCode_GENERAL, "The app is already registered");
        return;
    }

    m_registeredApp = lunaTask->getRequest();
    m_isRegistered = true;

    JValue payload = pbnjson::Object();
    payload.put("event", "registered");
    payload.put("message", "registered");// TODO this should be removed. Let's use event only.

    if (!sendEvent(payload)) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_instanceId, "Failed to register application");
        m_isRegistered = false;
        return;
    }
    Logger::info(CLASS_NAME, __FUNCTION__, m_instanceId, "Application is registered");
}

bool RunningApp::sendEvent(JValue& responsePayload)
{
    if (!m_isRegistered) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_instanceId, "RunningApp is not registered");
        return false;
    }

    responsePayload.put("returnValue", true);
    Logger::logAPIResponse(CLASS_NAME, __FUNCTION__, m_registeredApp, responsePayload);
    m_registeredApp.respond(responsePayload.stringify().c_str());
    return true;
}

string RunningApp::getLaunchParams(LunaTaskPtr lunaTask)
{
    JValue params = pbnjson::Object();
    AppType type = this->getLaunchPoint()->getAppDesc()->getAppType();

    if (AppType::AppType_Native_Qml == type) {
        params.put("main", this->getLaunchPoint()->getAppDesc()->getAbsMain());
    }
    if (!m_preload.empty()) {
        params.put("preload", m_preload);
    }


    if (AppType::AppType_Native_Qml == type) {
        params.put("appId", this->getLaunchPoint()->getAppDesc()->getAppId());
        params.put("params", lunaTask->getParams());
    } else {
        params.put("event", "launch");
        params.put("reason", lunaTask->getReason());
        params.put("appId", lunaTask->getAppId());
        params.put("nid", lunaTask->getAppId());
        params.put("interfaceVersion", 2);
        params.put("interfaceMethod", "registerApp");
        params.put("parameters", lunaTask->getParams());
        params.put("@system_native_app", true);
    }
    return params.stringify();
}

JValue RunningApp::getRelaunchParams(LunaTaskPtr lunaTask)
{
    JValue params = pbnjson::Object();
    params.put("returnValue", true);
    params.put("event", "relaunch");
    params.put("message", "relaunch"); // TODO this should be removed. Let's use event only.
    params.put("parameters", lunaTask->getParams());
    params.put("reason", lunaTask->getReason());
    params.put("appId", lunaTask->getAppId());
    return params;
}

bool RunningApp::setLifeStatus(LifeStatus lifeStatus)
{
    if (m_lifeStatus == lifeStatus) {
        Logger::debug(CLASS_NAME, __FUNCTION__, m_instanceId,
                      Logger::format("Ignored: %s (%s ==> %s)", getAppId().c_str(), toString(m_lifeStatus), toString(lifeStatus)));
        return true;
    }

    // CLOSING is special transition. It should be allowed all cases
    if (isTransition(m_lifeStatus) && isTransition(lifeStatus) && lifeStatus != LifeStatus::LifeStatus_CLOSING) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_instanceId,
                        Logger::format("Warning: %s (%s ==> %s)", getAppId().c_str(), toString(m_lifeStatus), toString(lifeStatus)));
        return false;
    }

    switch (lifeStatus) {
    case LifeStatus::LifeStatus_STOP:
        if (m_lifeStatus == LifeStatus::LifeStatus_CLOSING)
            Logger::info(CLASS_NAME, __FUNCTION__, m_instanceId, "Closed by SAM");
        else
            Logger::info(CLASS_NAME, __FUNCTION__, m_instanceId, "Closed by Itself");
        break;

    case LifeStatus::LifeStatus_PRELOADING:
        m_launchCount++;
        break;

    case LifeStatus::LifeStatus_LAUNCHING:
        m_launchCount++;
        if (m_lifeStatus == LifeStatus::LifeStatus_FOREGROUND) {
            Logger::info(CLASS_NAME, __FUNCTION__, m_instanceId,
                         Logger::format("Changed: %s (%s ==> %s)", getAppId().c_str(), toString(m_lifeStatus), toString(LifeStatus::LifeStatus_RELAUNCHING)));
            m_lifeStatus = LifeStatus::LifeStatus_RELAUNCHING;
            ApplicationManager::getInstance().postGetAppLifeStatus(*this);
            lifeStatus = LifeStatus::LifeStatus_FOREGROUND;
        } else if (m_lifeStatus == LifeStatus::LifeStatus_BACKGROUND ||
                   m_lifeStatus == LifeStatus::LifeStatus_PAUSED ||
                   m_lifeStatus == LifeStatus::LifeStatus_PRELOADED) {
            lifeStatus = LifeStatus::LifeStatus_RELAUNCHING;
        }
        break;

    default:
        break;
    }

    Logger::info(CLASS_NAME, __FUNCTION__, m_instanceId,
                 Logger::format("Changed: %s (%s ==> %s)", getAppId().c_str(), toString(m_lifeStatus), toString(lifeStatus)));
    m_lifeStatus = lifeStatus;

    if (isTransition(m_lifeStatus)) {
        // Transition should be completed within timeout
        startKillingTimer(TIMEOUT_TRANSITION);
    } else {
        stopKillingTimer();
    }

    ApplicationManager::getInstance().postGetAppLifeStatus(*this);
    ApplicationManager::getInstance().postGetAppLifeEvents(*this);
    return true;
}

gboolean RunningApp::onKillingTimer(gpointer context)
{
    RunningApp* runningApp = static_cast<RunningApp*>(context);
    if (runningApp == nullptr) {
        return G_SOURCE_REMOVE;
    }
    Logger::warning(CLASS_NAME, __FUNCTION__, runningApp->m_instanceId, "Transition is timeout");

    AbsLifeHandler::getLifeHandler(*runningApp).kill(*runningApp);

    // It tries to kill the app continually
    return G_SOURCE_CONTINUE;
}

void RunningApp::startKillingTimer(guint timeout)
{
    stopKillingTimer();
    m_killingTimer = g_timeout_add(timeout, onKillingTimer, this);
}

void RunningApp::stopKillingTimer()
{
    if (m_killingTimer > 0) {
        g_source_remove(m_killingTimer);
        m_killingTimer = 0;
    }
}
