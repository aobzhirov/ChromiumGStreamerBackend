/*
 * Copyright (C) 2011, 2012 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/css/CSSValuePool.h"

#include "core/css/CSSValueList.h"
#include "core/css/parser/CSSParser.h"
#include "core/style/ComputedStyle.h"
#include "wtf/Threading.h"

namespace blink {

CSSValuePool& cssValuePool()
{
    DEFINE_STATIC_LOCAL(Persistent<CSSValuePool>, pool, (new CSSValuePool()));
    return *pool;
}

CSSValuePool::CSSValuePool()
    : m_inheritedValue(CSSInheritedValue::create())
    , m_implicitInitialValue(CSSInitialValue::createImplicit())
    , m_explicitInitialValue(CSSInitialValue::createExplicit())
    , m_unsetValue(CSSUnsetValue::create())
    , m_colorTransparent(CSSColorValue::create(Color::transparent))
    , m_colorWhite(CSSColorValue::create(Color::white))
    , m_colorBlack(CSSColorValue::create(Color::black))
{
    m_identifierValueCache.resize(numCSSValueKeywords);
    m_pixelValueCache.resize(maximumCacheableIntegerValue + 1);
    m_percentValueCache.resize(maximumCacheableIntegerValue + 1);
    m_numberValueCache.resize(maximumCacheableIntegerValue + 1);
}

RawPtr<CSSPrimitiveValue> CSSValuePool::createIdentifierValue(CSSValueID ident)
{
    if (ident <= 0)
        return CSSPrimitiveValue::createIdentifier(ident);

    if (!m_identifierValueCache[ident])
        m_identifierValueCache[ident] = CSSPrimitiveValue::createIdentifier(ident);
    return m_identifierValueCache[ident];
}

RawPtr<CSSCustomIdentValue> CSSValuePool::createIdentifierValue(CSSPropertyID ident)
{
    return CSSCustomIdentValue::create(ident);
}

RawPtr<CSSColorValue> CSSValuePool::createColorValue(RGBA32 rgbValue)
{
    // These are the empty and deleted values of the hash table.
    if (rgbValue == Color::transparent)
        return m_colorTransparent;
    if (rgbValue == Color::white)
        return m_colorWhite;
    // Just because it is common.
    if (rgbValue == Color::black)
        return m_colorBlack;

    if (!isMainThread()) {
        // TODO (crbug.com/599659): Make CSS color parsing work properly in a
        // worker thread.
        // Currently, ColorValueCache is not thread-safe; so we avoid interacting
        // with it on a non-main thread.
        return CSSColorValue::create(rgbValue);
    }

    // Just wipe out the cache and start rebuilding if it gets too big.
    const unsigned maximumColorCacheSize = 512;
    if (m_colorValueCache.size() > maximumColorCacheSize)
        m_colorValueCache.clear();

    RawPtr<CSSColorValue> dummyValue = nullptr;
    ColorValueCache::AddResult entry = m_colorValueCache.add(rgbValue, dummyValue);
    if (entry.isNewEntry)
        entry.storedValue->value = CSSColorValue::create(rgbValue);

    return entry.storedValue->value;
}

RawPtr<CSSPrimitiveValue> CSSValuePool::createValue(double value, CSSPrimitiveValue::UnitType type)
{
    if (std::isinf(value))
        value = 0;

    if (value < 0 || value > maximumCacheableIntegerValue)
        return CSSPrimitiveValue::create(value, type);

    int intValue = static_cast<int>(value);
    if (value != intValue)
        return CSSPrimitiveValue::create(value, type);

    switch (type) {
    case CSSPrimitiveValue::UnitType::Pixels:
        if (!m_pixelValueCache[intValue])
            m_pixelValueCache[intValue] = CSSPrimitiveValue::create(value, type);
        return m_pixelValueCache[intValue];
    case CSSPrimitiveValue::UnitType::Percentage:
        if (!m_percentValueCache[intValue])
            m_percentValueCache[intValue] = CSSPrimitiveValue::create(value, type);
        return m_percentValueCache[intValue];
    case CSSPrimitiveValue::UnitType::Number:
    case CSSPrimitiveValue::UnitType::Integer:
        if (!m_numberValueCache[intValue])
            m_numberValueCache[intValue] = CSSPrimitiveValue::create(value, CSSPrimitiveValue::UnitType::Integer);
        return m_numberValueCache[intValue];
    default:
        return CSSPrimitiveValue::create(value, type);
    }
}

RawPtr<CSSPrimitiveValue> CSSValuePool::createValue(const Length& value, const ComputedStyle& style)
{
    return CSSPrimitiveValue::create(value, style.effectiveZoom());
}

RawPtr<CSSFontFamilyValue> CSSValuePool::createFontFamilyValue(const String& familyName)
{
    if (familyName.isNull())
        return CSSFontFamilyValue::create(familyName);
    Member<CSSFontFamilyValue>& value = m_fontFamilyValueCache.add(familyName, nullptr).storedValue->value;
    if (!value)
        value = CSSFontFamilyValue::create(familyName);
    return value;
}

RawPtr<CSSValueList> CSSValuePool::createFontFaceValue(const AtomicString& string)
{
    // Just wipe out the cache and start rebuilding if it gets too big.
    const unsigned maximumFontFaceCacheSize = 128;
    if (m_fontFaceValueCache.size() > maximumFontFaceCacheSize)
        m_fontFaceValueCache.clear();

    Member<CSSValueList>& value = m_fontFaceValueCache.add(string, nullptr).storedValue->value;
    if (!value) {
        RawPtr<CSSValue> parsedValue = CSSParser::parseSingleValue(CSSPropertyFontFamily, string);
        if (parsedValue && parsedValue->isValueList())
            value = toCSSValueList(parsedValue.get());
    }
    return value;
}

DEFINE_TRACE(CSSValuePool)
{
#if ENABLE(OILPAN)
    visitor->trace(m_inheritedValue);
    visitor->trace(m_implicitInitialValue);
    visitor->trace(m_explicitInitialValue);
    visitor->trace(m_unsetValue);
    visitor->trace(m_identifierValueCache);
    visitor->trace(m_colorValueCache);
    visitor->trace(m_colorTransparent);
    visitor->trace(m_colorWhite);
    visitor->trace(m_colorBlack);
    visitor->trace(m_pixelValueCache);
    visitor->trace(m_percentValueCache);
    visitor->trace(m_numberValueCache);
    visitor->trace(m_fontFaceValueCache);
    visitor->trace(m_fontFamilyValueCache);
#endif
}

} // namespace blink
