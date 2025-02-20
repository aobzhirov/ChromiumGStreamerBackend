// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/workers/DedicatedWorkerMessagingProxy.h"

#include "core/workers/DedicatedWorkerThread.h"
#include "core/workers/WorkerClients.h"

namespace blink {

DedicatedWorkerMessagingProxy::DedicatedWorkerMessagingProxy(InProcessWorkerBase* workerObject, RawPtr<WorkerClients> workerClients)
    : WorkerMessagingProxy(workerObject, workerClients)
{
}

DedicatedWorkerMessagingProxy::~DedicatedWorkerMessagingProxy()
{
}

PassOwnPtr<WorkerThread> DedicatedWorkerMessagingProxy::createWorkerThread(double originTime)
{
    return DedicatedWorkerThread::create(loaderProxy(), workerObjectProxy(), originTime);
}

} // namespace blink
