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

#include "LunaTask.h"

#include "AppDescriptionList.h"
#include "RunningAppList.h"
#include "util/JValueUtil.h"

#define LOG_NAME "LunaTask"

int LunaTask::getDisplayId()
{
    int displayId = -1;
    if (!JValueUtil::getValue(m_requestPayload, "displayId", displayId))
        JValueUtil::getValue(m_requestPayload, "params", "displayAffinity", displayId);
    return displayId;
}

void LunaTask::setDisplayId(const int displayId)
{
    const int oldDisplayId = getDisplayId();
    if (oldDisplayId != -1 && oldDisplayId != displayId) {
        Logger::warning(LOG_NAME, __FUNCTION__, Logger::format("DisplayId is not empty: Old(%d) New(%d)", oldDisplayId, displayId));
    }
    m_requestPayload.put("displayId", displayId);
    if (!m_requestPayload.hasKey("params")) {
        m_requestPayload.put("params", pbnjson::Object());
    }
    m_requestPayload["params"].put("displayAffinity", displayId);
}

/*
 * The activity manager launches an application for an activity (a scheduled
 * wakeup, a db watch) by calling launch with the activity's details as
 * "$activity", beside "params" rather than in them. LunaSysMgr handed them on
 * inside the launch parameters, creating those if there were none, and legacy
 * applications read them there: Calendar's midnight relaunch completes and
 * reschedules its calendar.updateIcon activity from params.$activity, and
 * without it crashed and never ran again, leaving the launcher's day number
 * stale. Do the same.
 *
 * An application's own params.$activity is left alone, as are params that
 * are not an object, which have nowhere to put it.
 */
void LunaTask::passActivityToParams()
{
    if (!m_requestPayload.hasKey("$activity") || !m_requestPayload["$activity"].isObject())
        return;

    if (!m_requestPayload.hasKey("params"))
        m_requestPayload.put("params", pbnjson::Object());
    if (!m_requestPayload["params"].isObject() || m_requestPayload["params"].hasKey("$activity"))
        return;

    m_requestPayload["params"].put("$activity", m_requestPayload["$activity"].duplicate());
}
