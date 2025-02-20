/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (c) 2010, Google Inc. All rights reserved.
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

#ifndef TypedArrayBase_h
#define TypedArrayBase_h

#include "wtf/ArrayBuffer.h"
#include "wtf/ArrayBufferView.h"

namespace WTF {

template <typename T>
class TypedArrayBase : public ArrayBufferView {
public:
    typedef T ValueType;

    T* data() const { return static_cast<T*>(baseAddress()); }

    bool set(TypedArrayBase<T>* array, unsigned offset)
    {
        return setImpl(array, offset * sizeof(T));
    }

    // Overridden from ArrayBufferView. This must be public because of
    // rules about inheritance of members in template classes, and
    // because it is accessed via pointers to subclasses.
    unsigned length() const
    {
        return m_length;
    }

    unsigned byteLength() const final
    {
        return m_length * sizeof(T);
    }

    // Invoked by the indexed getter. Does not perform range checks; caller
    // is responsible for doing so and returning undefined as necessary.
    T item(unsigned index) const
    {
        ASSERT_WITH_SECURITY_IMPLICATION(index < TypedArrayBase<T>::m_length);
        return TypedArrayBase<T>::data()[index];
    }

protected:
    TypedArrayBase(PassRefPtr<ArrayBuffer> buffer, unsigned byteOffset, unsigned length)
        : ArrayBufferView(buffer, byteOffset)
        , m_length(length)
    {
    }

    template <class Subclass>
    static PassRefPtr<Subclass> create(unsigned length)
    {
        RefPtr<ArrayBuffer> buffer = ArrayBuffer::create(length, sizeof(T));
        return create<Subclass>(buffer.release(), 0, length);
    }

    template <class Subclass>
    static PassRefPtr<Subclass> create(const T* array, unsigned length)
    {
        RefPtr<Subclass> a = create<Subclass>(length);
        if (a)
            for (unsigned i = 0; i < length; ++i)
                a->set(i, array[i]);
        return a;
    }

    template <class Subclass>
    static PassRefPtr<Subclass> create(PassRefPtr<ArrayBuffer> buffer, unsigned byteOffset, unsigned length)
    {
        RefPtr<ArrayBuffer> buf(buffer);
        CHECK(verifySubRange<T>(buf, byteOffset, length));
        return adoptRef(new Subclass(buf.release(), byteOffset, length));
    }

    template <class Subclass>
    static PassRefPtr<Subclass> createOrNull(unsigned length)
    {
        RefPtr<ArrayBuffer> buffer = ArrayBuffer::createOrNull(length, sizeof(T));
        if (!buffer)
            return nullptr;
        return create<Subclass>(buffer.release(), 0, length);
    }

    void neuter() final
    {
        ArrayBufferView::neuter();
        m_length = 0;
    }

    // We do not want to have to access this via a virtual function in subclasses,
    // which is why it is protected rather than private.
    unsigned m_length;
};

} // namespace WTF

using WTF::TypedArrayBase;

#endif // TypedArrayBase_h
