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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PageTransitionEvent_h
#define PageTransitionEvent_h

#include "core/events/Event.h"
#include "core/events/PageTransitionEventInit.h"

namespace blink {

class PageTransitionEvent final : public Event {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<PageTransitionEvent> create()
    {
        return new PageTransitionEvent;
    }
    static RawPtr<PageTransitionEvent> create(const AtomicString& type, bool persisted)
    {
        return new PageTransitionEvent(type, persisted);
    }
    static RawPtr<PageTransitionEvent> create(const AtomicString& type, const PageTransitionEventInit& initializer)
    {
        return new PageTransitionEvent(type, initializer);
    }

    ~PageTransitionEvent() override;

    const AtomicString& interfaceName() const override;

    bool persisted() const { return m_persisted; }

    DECLARE_VIRTUAL_TRACE();

private:
    PageTransitionEvent();
    PageTransitionEvent(const AtomicString& type, bool persisted);
    PageTransitionEvent(const AtomicString&, const PageTransitionEventInit&);

    bool m_persisted;
};

} // namespace blink

#endif // PageTransitionEvent_h
