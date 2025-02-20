/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#include "modules/webmidi/MIDIController.h"

#include "modules/webmidi/MIDIAccessInitializer.h"
#include "modules/webmidi/MIDIClient.h"

namespace blink {

const char* MIDIController::supplementName()
{
    return "MIDIController";
}

MIDIController::MIDIController(PassOwnPtr<MIDIClient> client)
    : m_client(client)
{
    ASSERT(m_client);
}

MIDIController::~MIDIController()
{
}

RawPtr<MIDIController> MIDIController::create(PassOwnPtr<MIDIClient> client)
{
    return new MIDIController(client);
}

void MIDIController::requestPermission(MIDIAccessInitializer* initializer, const MIDIOptions& options)
{
    m_client->requestPermission(initializer, options);
}

void MIDIController::cancelPermissionRequest(MIDIAccessInitializer* initializer)
{
    m_client->cancelPermissionRequest(initializer);
}

void provideMIDITo(LocalFrame& frame, PassOwnPtr<MIDIClient> client)
{
    MIDIController::provideTo(frame, MIDIController::supplementName(), MIDIController::create(client));
}

} // namespace blink
