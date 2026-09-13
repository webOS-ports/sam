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

#ifndef BUS_CLIENT_ABSLUNACLIENT_H_
#define BUS_CLIENT_ABSLUNACLIENT_H_

#include <iostream>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>
#include <boost/signals2.hpp>

#include "bus/service/ApplicationManager.h"
#include "interface/IClassName.h"
#include "util/JValueUtil.h"

using namespace std;
using namespace LS;
using namespace pbnjson;

class LSErrorSafe: public LSError {
public:
    LSErrorSafe()
    {
        LSErrorInit(this);
    }

    ~LSErrorSafe()
    {
        LSErrorFree(this);
    }
};

class AbsLunaClient : public IClassName {
public:
    static JValue& getEmptyPayload();
    static JValue& getSubscriptionPayload();

    AbsLunaClient(const string& name);
    virtual ~AbsLunaClient() override;

    virtual void initialize() final;
    virtual void finalize() final;

    const string& getName()
    {
        return m_name;
    }

    bool isConnected()
    {
        return m_isConnected;
    }

    boost::signals2::signal<void(bool)> EventServiceStatusChanged;

protected:
    /**
     * Cancel a subscription and leave the Call inactive either way.
     *
     * LS::Call::cancel() clears its token only when LSCallCancel() succeeds.
     * On the shutdown path it routinely does not: ls-hubd and the peer
     * services are already gone, so the hub answers "Could not find call N to
     * cancel" and the Call goes on reporting isActive().
     *
     * That matters because these clients are function-local statics. Their
     * destructors run from exit(), long after MainDaemon::finalize() has
     * called ApplicationManager::detach() and destroyed the LSHandle they
     * still point at - and ~Call() calls cancel() one more time. LSCallCancel()
     * then takes the call-map lock of a freed handle, which is the segfault in
     * pthread_mutex_lock that SAM died with on most shutdowns:
     *
     *     #0 ___pthread_mutex_lock (mutex=0x17)
     *     #1 _CallMapLock ()
     *     #2 LSCallCancel ()
     *     #3 LS::Call::cancel (this=ISingleton<WAM>::_instance+416)
     *     #4 LS::Call::~Call ()
     *     #5 WAM::~WAM ()
     *     #6 __run_exit_handlers ()
     *
     * Move-assigning a fresh Call over it takes the token and the handle
     * pointer from the (inactive) temporary, so isActive() goes false and the
     * destructor at exit has nothing left to do. The subscription itself is
     * not leaked: either the cancel above reached the hub, or the hub is gone
     * and there is nothing left to cancel it on.
     */
    static void releaseCall(Call& call);

    virtual void onInitialzed() = 0;
    virtual void onFinalized() = 0;
    virtual void onServerStatusChanged(bool isConnected) = 0;

    int m_serverStatusCount;

private:
    static bool _onServerStatus(LSHandle* sh, LSMessage* message, void* context);

    string m_name;
    bool m_isConnected;

    Call m_statusCall;
};

#endif /* BUS_CLIENT_ABSLUNACLIENT_H_ */
