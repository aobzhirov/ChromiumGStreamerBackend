/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SpeechSynthesisEvent_h
#define SpeechSynthesisEvent_h

#include "modules/EventModules.h"
#include "modules/speech/SpeechSynthesisUtterance.h"

namespace blink {

class SpeechSynthesisEvent final : public Event {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<SpeechSynthesisEvent> create();
    static RawPtr<SpeechSynthesisEvent> create(const AtomicString& type, SpeechSynthesisUtterance*, unsigned charIndex, float elapsedTime, const String& name);

    SpeechSynthesisUtterance* utterance() const { return m_utterance; }
    unsigned charIndex() const { return m_charIndex; }
    float elapsedTime() const { return m_elapsedTime; }
    const String& name() const { return m_name; }

    const AtomicString& interfaceName() const override { return EventNames::SpeechSynthesisEvent; }

    DECLARE_VIRTUAL_TRACE();

private:
    SpeechSynthesisEvent();
    SpeechSynthesisEvent(const AtomicString& type, SpeechSynthesisUtterance*, unsigned charIndex, float elapsedTime, const String& name);

    Member<SpeechSynthesisUtterance> m_utterance;
    unsigned m_charIndex;
    float m_elapsedTime;
    String m_name;
};

} // namespace blink

#endif // SpeechSynthesisEvent_h
