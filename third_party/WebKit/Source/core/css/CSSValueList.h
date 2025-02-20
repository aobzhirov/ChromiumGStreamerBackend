/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CSSValueList_h
#define CSSValueList_h

#include "core/CoreExport.h"
#include "core/css/CSSValue.h"
#include "wtf/PassRefPtr.h"
#include "wtf/Vector.h"

namespace blink {

class CORE_EXPORT CSSValueList : public CSSValue {
public:
    using iterator = HeapVector<Member<CSSValue>, 4>::iterator;
    using const_iterator = HeapVector<Member<CSSValue>, 4>::const_iterator;

    static RawPtr<CSSValueList> createCommaSeparated()
    {
        return new CSSValueList(CommaSeparator);
    }
    static RawPtr<CSSValueList> createSpaceSeparated()
    {
        return new CSSValueList(SpaceSeparator);
    }
    static RawPtr<CSSValueList> createSlashSeparated()
    {
        return new CSSValueList(SlashSeparator);
    }

    iterator begin() { return m_values.begin(); }
    iterator end() { return m_values.end(); }
    const_iterator begin() const { return m_values.begin(); }
    const_iterator end() const { return m_values.end(); }

    size_t length() const { return m_values.size(); }
    CSSValue* item(size_t index) { return m_values[index].get(); }
    const CSSValue* item(size_t index) const { return m_values[index].get(); }
    CSSValue* itemWithBoundsCheck(size_t index) { return index < m_values.size() ? m_values[index].get() : nullptr; }
    const CSSValue* itemWithBoundsCheck(size_t index) const { return index < m_values.size() ? m_values[index].get() : nullptr; }

    void append(RawPtr<CSSValue> value) { m_values.append(value); }
    void prepend(RawPtr<CSSValue> value) { m_values.prepend(value); }
    bool removeAll(CSSValue*);
    bool hasValue(CSSValue*) const;
    RawPtr<CSSValueList> copy();

    String customCSSText() const;
    bool equals(const CSSValueList&) const;

    bool hasFailedOrCanceledSubresources() const;

    DECLARE_TRACE_AFTER_DISPATCH();

protected:
    CSSValueList(ClassType, ValueListSeparator);

private:
    explicit CSSValueList(ValueListSeparator);

    HeapVector<Member<CSSValue>, 4> m_values;
};

DEFINE_CSS_VALUE_TYPE_CASTS(CSSValueList, isValueList());

} // namespace blink

#endif // CSSValueList_h
