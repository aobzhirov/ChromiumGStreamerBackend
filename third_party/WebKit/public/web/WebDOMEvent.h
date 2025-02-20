/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#ifndef WebDOMEvent_h
#define WebDOMEvent_h

#include "../platform/WebCommon.h"
#include "../platform/WebPrivatePtr.h"

namespace blink {

class Event;

class WebDOMEvent {
public:
    ~WebDOMEvent() { reset(); }

    WebDOMEvent() { }
    WebDOMEvent(const WebDOMEvent& other) { assign(other); }
    WebDOMEvent& operator=(const WebDOMEvent& e)
    {
        assign(e);
        return *this;
    }

    BLINK_EXPORT void reset();
    BLINK_EXPORT void assign(const WebDOMEvent&);

    bool isNull() const { return m_private.isNull(); }

#if BLINK_IMPLEMENTATION
    WebDOMEvent(const RawPtr<Event>&);
    operator RawPtr<Event>() const;
#endif

    template<typename T> T to()
    {
        T res;
        res.WebDOMEvent::assign(*this);
        return res;
    }

    template<typename T> const T toConst() const
    {
        T res;
        res.WebDOMEvent::assign(*this);
        return res;
    }

protected:
#if BLINK_IMPLEMENTATION
    void assign(const RawPtr<Event>&);

    template<typename T> T* unwrap()
    {
        return static_cast<T*>(m_private.get());
    }

    template<typename T> const T* constUnwrap() const
    {
        return static_cast<const T*>(m_private.get());
    }
#endif

    WebPrivatePtr<Event> m_private;
};

} // namespace blink

#endif
