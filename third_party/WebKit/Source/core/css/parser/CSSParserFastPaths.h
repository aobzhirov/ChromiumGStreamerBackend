// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSParserFastPaths_h
#define CSSParserFastPaths_h

#include "core/CSSPropertyNames.h"
#include "core/CSSValueKeywords.h"
#include "platform/graphics/Color.h"
#include "platform/heap/Handle.h"
#include "wtf/Allocator.h"
#include "wtf/Forward.h"

namespace blink {

class CSSValue;

class CSSParserFastPaths {
    STATIC_ONLY(CSSParserFastPaths);
public:
    // Parses simple values like '10px' or 'green', but makes no guarantees
    // about handling any property completely.
    static RawPtr<CSSValue> maybeParseValue(CSSPropertyID, const String&, CSSParserMode);

    // Properties handled here shouldn't be explicitly handled in CSSPropertyParser
    static bool isKeywordPropertyID(CSSPropertyID);
    static bool isValidKeywordPropertyAndValue(CSSPropertyID, CSSValueID);

    static RawPtr<CSSValue> parseColor(const String&, CSSParserMode);
};

} // namespace blink

#endif // CSSParserFastPaths_h
