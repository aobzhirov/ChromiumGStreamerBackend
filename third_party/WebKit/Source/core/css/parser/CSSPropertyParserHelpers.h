// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSPropertyParserHelpers_h
#define CSSPropertyParserHelpers_h

#include "core/css/CSSValuePool.h"
#include "core/css/parser/CSSParserMode.h"
#include "core/css/parser/CSSParserTokenRange.h"
#include "platform/Length.h" // For ValueRange
#include "platform/heap/Handle.h"

namespace blink {

class CSSStringValue;
class CSSValuePair;

// When these functions are successful, they will consume all the relevant
// tokens from the range and also consume any whitespace which follows. When
// the start of the range doesn't match the type we're looking for, the range
// will not be modified.
namespace CSSPropertyParserHelpers {

// TODO(timloh): These should probably just be consumeComma and consumeSlash.
bool consumeCommaIncludingWhitespace(CSSParserTokenRange&);
bool consumeSlashIncludingWhitespace(CSSParserTokenRange&);
// consumeFunction expects the range starts with a FunctionToken.
CSSParserTokenRange consumeFunction(CSSParserTokenRange&);

enum class UnitlessQuirk {
    Allow,
    Forbid
};

RawPtr<CSSPrimitiveValue> consumeInteger(CSSParserTokenRange&, double minimumValue = -std::numeric_limits<double>::max());
RawPtr<CSSPrimitiveValue> consumePositiveInteger(CSSParserTokenRange&);
bool consumeNumberRaw(CSSParserTokenRange&, double& result);
RawPtr<CSSPrimitiveValue> consumeNumber(CSSParserTokenRange&, ValueRange);
RawPtr<CSSPrimitiveValue> consumeLength(CSSParserTokenRange&, CSSParserMode, ValueRange, UnitlessQuirk = UnitlessQuirk::Forbid);
RawPtr<CSSPrimitiveValue> consumePercent(CSSParserTokenRange&, ValueRange);
RawPtr<CSSPrimitiveValue> consumeLengthOrPercent(CSSParserTokenRange&, CSSParserMode, ValueRange, UnitlessQuirk = UnitlessQuirk::Forbid);
RawPtr<CSSPrimitiveValue> consumeAngle(CSSParserTokenRange&);
RawPtr<CSSPrimitiveValue> consumeTime(CSSParserTokenRange&, ValueRange);

RawPtr<CSSPrimitiveValue> consumeIdent(CSSParserTokenRange&);
RawPtr<CSSPrimitiveValue> consumeIdentRange(CSSParserTokenRange&, CSSValueID lower, CSSValueID upper);
template<CSSValueID, CSSValueID...> inline bool identMatches(CSSValueID id);
template<CSSValueID... allowedIdents> RawPtr<CSSPrimitiveValue> consumeIdent(CSSParserTokenRange&);

RawPtr<CSSCustomIdentValue> consumeCustomIdent(CSSParserTokenRange&);
RawPtr<CSSStringValue> consumeString(CSSParserTokenRange&);
String consumeUrl(CSSParserTokenRange&);

RawPtr<CSSValue> consumeColor(CSSParserTokenRange&, CSSParserMode, bool acceptQuirkyColors = false);

RawPtr<CSSValuePair> consumePosition(CSSParserTokenRange&, CSSParserMode, UnitlessQuirk);
bool consumePosition(CSSParserTokenRange&, CSSParserMode, UnitlessQuirk, RawPtr<CSSValue>& resultX, RawPtr<CSSValue>& resultY);
bool consumeOneOrTwoValuedPosition(CSSParserTokenRange&, CSSParserMode, UnitlessQuirk, RawPtr<CSSValue>& resultX, RawPtr<CSSValue>& resultY);

// TODO(timloh): Move across consumeImage

// Template implementations are at the bottom of the file for readability.

template<typename... emptyBaseCase> inline bool identMatches(CSSValueID id) { return false; }
template<CSSValueID head, CSSValueID... tail> inline bool identMatches(CSSValueID id)
{
    return id == head || identMatches<tail...>(id);
}

template<CSSValueID... names> RawPtr<CSSPrimitiveValue> consumeIdent(CSSParserTokenRange& range)
{
    if (range.peek().type() != IdentToken || !identMatches<names...>(range.peek().id()))
        return nullptr;
    return cssValuePool().createIdentifierValue(range.consumeIncludingWhitespace().id());
}

static inline bool isCSSWideKeyword(const CSSValueID& id)
{
    return id == CSSValueInitial || id == CSSValueInherit || id == CSSValueUnset || id == CSSValueDefault;
}

} // namespace CSSPropertyParserHelpers

} // namespace blink

#endif // CSSPropertyParserHelpers_h
