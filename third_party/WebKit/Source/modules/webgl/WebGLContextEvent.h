/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebGLContextEvent_h
#define WebGLContextEvent_h

#include "modules/EventModules.h"
#include "modules/webgl/WebGLContextEventInit.h"

namespace blink {

class WebGLContextEvent final : public Event {
    DEFINE_WRAPPERTYPEINFO();

public:
    static RawPtr<WebGLContextEvent> create()
    {
        return new WebGLContextEvent;
    }
    static RawPtr<WebGLContextEvent> create(const AtomicString& type, bool canBubble, bool cancelable, const String& statusMessage)
    {
        return new WebGLContextEvent(type, canBubble, cancelable, statusMessage);
    }
    static RawPtr<WebGLContextEvent> create(const AtomicString& type, const WebGLContextEventInit& initializer)
    {
        return new WebGLContextEvent(type, initializer);
    }
    ~WebGLContextEvent() override;

    const String& statusMessage() const { return m_statusMessage; }

    const AtomicString& interfaceName() const override;

    DECLARE_VIRTUAL_TRACE();

private:
    WebGLContextEvent();
    WebGLContextEvent(const AtomicString& type, bool canBubble, bool cancelable, const String& statusMessage);
    WebGLContextEvent(const AtomicString&, const WebGLContextEventInit&);

    String m_statusMessage;
};

} // namespace blink

#endif // WebGLContextEvent_h
