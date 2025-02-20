/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
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
 *
 */

#ifndef CompositionEvent_h
#define CompositionEvent_h

#include "core/events/CompositionEventInit.h"
#include "core/events/UIEvent.h"

namespace blink {

class CompositionEvent final : public UIEvent {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<CompositionEvent> create()
    {
        return new CompositionEvent;
    }

    static RawPtr<CompositionEvent> create(const AtomicString& type, RawPtr<AbstractView> view, const String& data)
    {
        return new CompositionEvent(type, view, data);
    }

    static RawPtr<CompositionEvent> create(const AtomicString& type, const CompositionEventInit& initializer)
    {
        return new CompositionEvent(type, initializer);
    }

    ~CompositionEvent() override;

    void initCompositionEvent(const AtomicString& type, bool canBubble, bool cancelable, RawPtr<AbstractView>, const String& data);

    String data() const { return m_data; }

    const AtomicString& interfaceName() const override;

    DECLARE_VIRTUAL_TRACE();

private:
    CompositionEvent();
    CompositionEvent(const AtomicString& type, RawPtr<AbstractView>, const String&);
    CompositionEvent(const AtomicString& type, const CompositionEventInit&);

    String m_data;
};

} // namespace blink

#endif // CompositionEvent_h
