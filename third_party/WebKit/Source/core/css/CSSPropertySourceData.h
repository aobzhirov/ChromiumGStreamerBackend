/*
 * Copyright (c) 2010 Google Inc. All rights reserved.
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

#ifndef CSSPropertySourceData_h
#define CSSPropertySourceData_h

#include "core/css/StyleRule.h"
#include "platform/heap/Handle.h"
#include "wtf/Forward.h"
#include "wtf/RefCounted.h"
#include "wtf/Vector.h"
#include "wtf/text/WTFString.h"

namespace blink {

struct SourceRange {
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
public:
    SourceRange();
    SourceRange(unsigned start, unsigned end);
    unsigned length() const;

    DEFINE_INLINE_TRACE() { }

    unsigned start;
    unsigned end;
};

struct CSSPropertySourceData {
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
public:
    CSSPropertySourceData(const String& name, const String& value, bool important, bool disabled, bool parsedOk, const SourceRange& range);
    CSSPropertySourceData(const CSSPropertySourceData& other);

    DEFINE_INLINE_TRACE() { visitor->trace(range); }

    String name;
    String value;
    bool important;
    bool disabled;
    bool parsedOk;
    SourceRange range;
};

struct CSSStyleSourceData : public GarbageCollected<CSSStyleSourceData> {
    static RawPtr<CSSStyleSourceData> create()
    {
        return new CSSStyleSourceData();
    }

    DEFINE_INLINE_TRACE()
    {
#if ENABLE(OILPAN)
        visitor->trace(propertyData);
#endif
    }

    HeapVector<CSSPropertySourceData> propertyData;
};

struct CSSMediaQueryExpSourceData {
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
public:
    CSSMediaQueryExpSourceData(const SourceRange& valueRange)
        : valueRange(valueRange) { }

    DEFINE_INLINE_TRACE() { visitor->trace(valueRange); }

    SourceRange valueRange;
};

struct CSSMediaQuerySourceData : public GarbageCollected<CSSMediaQuerySourceData> {
    static RawPtr<CSSMediaQuerySourceData> create()
    {
        return new CSSMediaQuerySourceData();
    }

    DEFINE_INLINE_TRACE()
    {
#if ENABLE(OILPAN)
        visitor->trace(expData);
#endif
    }

    HeapVector<CSSMediaQueryExpSourceData> expData;
};

struct CSSMediaSourceData : public GarbageCollected<CSSMediaSourceData> {
    static RawPtr<CSSMediaSourceData> create()
    {
        return new CSSMediaSourceData();
    }

    DEFINE_INLINE_TRACE()
    {
#if ENABLE(OILPAN)
        visitor->trace(queryData);
#endif
    }

    HeapVector<Member<CSSMediaQuerySourceData>> queryData;
};

struct CSSRuleSourceData;
using RuleSourceDataList = HeapVector<Member<CSSRuleSourceData>>;
using SelectorRangeList = HeapVector<SourceRange>;

struct CSSRuleSourceData : public GarbageCollected<CSSRuleSourceData> {
    static RawPtr<CSSRuleSourceData> create(StyleRule::RuleType type)
    {
        return new CSSRuleSourceData(type);
    }

    CSSRuleSourceData(StyleRule::RuleType type)
        : type(type)
    {
        if (type == StyleRule::Style || type == StyleRule::FontFace || type == StyleRule::Page || type == StyleRule::Keyframe)
            styleSourceData = CSSStyleSourceData::create();
        if (type == StyleRule::Media || type == StyleRule::Import)
            mediaSourceData = CSSMediaSourceData::create();
    }

    DECLARE_TRACE();

    StyleRule::RuleType type;

    // Range of the selector list in the enclosing source.
    SourceRange ruleHeaderRange;

    // Range of the rule body (e.g. style text for style rules) in the enclosing source.
    SourceRange ruleBodyRange;

    // Only for CSSStyleRules.
    SelectorRangeList selectorRanges;

    // Only for CSSStyleRules, CSSFontFaceRules, and CSSPageRules.
    Member<CSSStyleSourceData> styleSourceData;

    // Only for CSSMediaRules.
    RuleSourceDataList childRules;

    // Only for CSSMediaRules and CSSImportRules.
    Member<CSSMediaSourceData> mediaSourceData;
};

} // namespace blink

WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::SourceRange);
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::CSSPropertySourceData);
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::CSSMediaQueryExpSourceData);

#endif // CSSPropertySourceData_h
