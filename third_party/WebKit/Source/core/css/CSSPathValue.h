// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSPathValue_h
#define CSSPathValue_h

#include "core/css/CSSValue.h"
#include "core/style/StylePath.h"
#include "core/svg/SVGPathByteStream.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefPtr.h"

namespace blink {

class StylePath;

class CSSPathValue : public CSSValue {
public:
    static RawPtr<CSSPathValue> create(PassRefPtr<StylePath>);
    static RawPtr<CSSPathValue> create(PassOwnPtr<SVGPathByteStream>);

    static CSSPathValue* emptyPathValue();

    StylePath* stylePath() const { return m_stylePath.get(); }
    String customCSSText() const;

    bool equals(const CSSPathValue&) const;

    DECLARE_TRACE_AFTER_DISPATCH();

    const SVGPathByteStream& byteStream() const { return m_stylePath->byteStream(); }

private:
    CSSPathValue(PassRefPtr<StylePath>);

    RefPtr<StylePath> m_stylePath;
};

DEFINE_CSS_VALUE_TYPE_CASTS(CSSPathValue, isPathValue());

} // namespace blink

#endif // CSSPathValue_h
