/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DedicatedWorkerGlobalScope_h
#define DedicatedWorkerGlobalScope_h

#include "core/CoreExport.h"
#include "core/dom/MessagePort.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/workers/WorkerGlobalScope.h"
#include "platform/heap/Handle.h"

namespace blink {

class DedicatedWorkerThread;
class WorkerThreadStartupData;

class CORE_EXPORT DedicatedWorkerGlobalScope final : public WorkerGlobalScope {
    DEFINE_WRAPPERTYPEINFO();
public:
    typedef WorkerGlobalScope Base;
    static RawPtr<DedicatedWorkerGlobalScope> create(DedicatedWorkerThread*, PassOwnPtr<WorkerThreadStartupData>, double timeOrigin);
    ~DedicatedWorkerGlobalScope() override;

    bool isDedicatedWorkerGlobalScope() const override { return true; }
    void countFeature(UseCounter::Feature) const override;
    void countDeprecation(UseCounter::Feature) const override;

    // EventTarget
    const AtomicString& interfaceName() const override;

    void postMessage(ExecutionContext*, PassRefPtr<SerializedScriptValue>, const MessagePortArray*, ExceptionState&);

    DEFINE_ATTRIBUTE_EVENT_LISTENER(message);

    DedicatedWorkerThread* thread() const;

    DECLARE_VIRTUAL_TRACE();

private:
    DedicatedWorkerGlobalScope(const KURL&, const String& userAgent, DedicatedWorkerThread*, double timeOrigin, PassOwnPtr<SecurityOrigin::PrivilegeData>, RawPtr<WorkerClients>);
};

} // namespace blink

#endif // DedicatedWorkerGlobalScope_h
