/*
 * Copyright (C) 2012 Google Inc. All Rights Reserved.
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

#ifndef IdTargetObserverRegistry_h
#define IdTargetObserverRegistry_h

#include "platform/heap/Handle.h"
#include "wtf/Forward.h"
#include "wtf/HashMap.h"
#include "wtf/HashSet.h"
#include "wtf/text/StringHash.h"

namespace blink {

class IdTargetObserver;

class IdTargetObserverRegistry final : public GarbageCollectedFinalized<IdTargetObserverRegistry> {
    WTF_MAKE_NONCOPYABLE(IdTargetObserverRegistry);
    friend class IdTargetObserver;
public:
    static RawPtr<IdTargetObserverRegistry> create();
    DECLARE_TRACE();
    void notifyObservers(const AtomicString& id);
    bool hasObservers(const AtomicString& id) const;

private:
    IdTargetObserverRegistry() : m_notifyingObserversInSet(nullptr) { }
    void addObserver(const AtomicString& id, IdTargetObserver*);
    void removeObserver(const AtomicString& id, IdTargetObserver*);
    void notifyObserversInternal(const AtomicString& id);

    typedef HeapHashSet<Member<IdTargetObserver>> ObserverSet;
    typedef HeapHashMap<StringImpl*, Member<ObserverSet>> IdToObserverSetMap;
    IdToObserverSetMap m_registry;
    Member<ObserverSet> m_notifyingObserversInSet;
};

inline void IdTargetObserverRegistry::notifyObservers(const AtomicString& id)
{
    ASSERT(!m_notifyingObserversInSet);
    if (id.isEmpty() || m_registry.isEmpty())
        return;
    IdTargetObserverRegistry::notifyObserversInternal(id);
}

} // namespace blink

#endif // IdTargetObserverRegistry_h
