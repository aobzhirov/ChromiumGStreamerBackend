/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
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

#ifndef LifecycleObserver_h
#define LifecycleObserver_h

#include "platform/heap/Handle.h"
#include "wtf/Assertions.h"

namespace blink {

template<typename T, typename Observer, typename Notifier>
class LifecycleObserver : public GarbageCollectedMixin {
public:
    using Context = T;

#if !ENABLE(OILPAN)
    virtual ~LifecycleObserver()
    {
        clearContext();
    }
#endif

    DEFINE_INLINE_VIRTUAL_TRACE()
    {
        visitor->trace(m_lifecycleContext);
    }

    virtual void contextDestroyed() { }

    Context* lifecycleContext() const { return m_lifecycleContext; }
    void clearLifecycleContext() { m_lifecycleContext = nullptr; }

protected:
    explicit LifecycleObserver(Context* context)
        : m_lifecycleContext(nullptr)
    {
        setContext(context);
    }

    void setContext(Context*);

    void clearContext()
    {
        setContext(nullptr);
    }

private:
    WeakMember<Context> m_lifecycleContext;
};

template<typename T, typename Observer, typename Notifier>
inline void LifecycleObserver<T, Observer, Notifier>::setContext(typename LifecycleObserver<T, Observer, Notifier>::Context* context)
{
    if (m_lifecycleContext)
        static_cast<Notifier*>(m_lifecycleContext.get())->removeObserver(static_cast<Observer*>(this));

    m_lifecycleContext = context;

    if (m_lifecycleContext)
        static_cast<Notifier*>(m_lifecycleContext.get())->addObserver(static_cast<Observer*>(this));
}

} // namespace blink

#endif // LifecycleObserver_h
