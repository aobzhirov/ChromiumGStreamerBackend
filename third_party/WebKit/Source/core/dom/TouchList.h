/*
 * Copyright 2008, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
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

#ifndef TouchList_h
#define TouchList_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "core/CoreExport.h"
#include "core/dom/Touch.h"
#include "platform/heap/Handle.h"
#include "wtf/RefCounted.h"
#include "wtf/Vector.h"

namespace blink {

class CORE_EXPORT TouchList final : public GarbageCollected<TouchList>, public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<TouchList> create()
    {
        return new TouchList;
    }

    static RawPtr<TouchList> create(const HeapVector<Member<Touch>>& touches)
    {
        RawPtr<TouchList> list = new TouchList;
        list->m_values.appendVector(touches);
        return list.release();
    }

    static RawPtr<TouchList> adopt(HeapVector<Member<Touch>>& touches)
    {
        return new TouchList(touches);
    }

    unsigned length() const { return m_values.size(); }

    Touch* item(unsigned);
    const Touch* item(unsigned) const;

    void append(const RawPtr<Touch> touch) { m_values.append(touch); }

    DECLARE_TRACE();

private:
    TouchList() { }

    TouchList(HeapVector<Member<Touch>>& touches)
    {
        m_values.swap(touches);
    }

    HeapVector<Member<Touch>> m_values;
};

} // namespace blink

#endif // TouchList_h
