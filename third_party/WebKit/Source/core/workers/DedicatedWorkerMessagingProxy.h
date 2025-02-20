// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DedicatedWorkerMessagingProxy_h
#define DedicatedWorkerMessagingProxy_h

#include "core/CoreExport.h"
#include "core/workers/WorkerMessagingProxy.h"

namespace blink {

class CORE_EXPORT DedicatedWorkerMessagingProxy final : public WorkerMessagingProxy {
    WTF_MAKE_NONCOPYABLE(DedicatedWorkerMessagingProxy);
    USING_FAST_MALLOC(WorkerMessagingProxy);
public:
    DedicatedWorkerMessagingProxy(InProcessWorkerBase*, RawPtr<WorkerClients>);
    ~DedicatedWorkerMessagingProxy() override;

    PassOwnPtr<WorkerThread> createWorkerThread(double originTime) override;
};

} // namespace blink

#endif // DedicatedWorkerMessagingProxy_h
