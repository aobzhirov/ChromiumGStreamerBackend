/*
 * Copyright (C) 2007, 2008, 2012 Apple Inc. All rights reserved.
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

#ifndef CSSKeyframesRule_h
#define CSSKeyframesRule_h

#include "core/css/CSSRule.h"
#include "core/css/StyleRule.h"
#include "wtf/Forward.h"
#include "wtf/text/AtomicString.h"

namespace blink {

class CSSRuleList;
class CSSKeyframeRule;
class StyleRuleKeyframe;

class StyleRuleKeyframes final : public StyleRuleBase {
public:
    static RawPtr<StyleRuleKeyframes> create() { return new StyleRuleKeyframes(); }

    ~StyleRuleKeyframes();

    const HeapVector<Member<StyleRuleKeyframe>>& keyframes() const { return m_keyframes; }

    void parserAppendKeyframe(RawPtr<StyleRuleKeyframe>);
    void wrapperAppendKeyframe(RawPtr<StyleRuleKeyframe>);
    void wrapperRemoveKeyframe(unsigned);

    String name() const { return m_name; }
    void setName(const String& name) { m_name = AtomicString(name); }

    bool isVendorPrefixed() const { return m_isPrefixed; }
    void setVendorPrefixed(bool isPrefixed) { m_isPrefixed = isPrefixed; }

    int findKeyframeIndex(const String& key) const;

    RawPtr<StyleRuleKeyframes> copy() const { return new StyleRuleKeyframes(*this); }

    DECLARE_TRACE_AFTER_DISPATCH();

    void styleChanged() { m_version++; }
    unsigned version() const { return m_version; }

private:
    StyleRuleKeyframes();
    explicit StyleRuleKeyframes(const StyleRuleKeyframes&);

    HeapVector<Member<StyleRuleKeyframe>> m_keyframes;
    AtomicString m_name;
    unsigned m_version : 31;
    unsigned m_isPrefixed : 1;
};

DEFINE_STYLE_RULE_TYPE_CASTS(Keyframes);

class CSSKeyframesRule final : public CSSRule {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<CSSKeyframesRule> create(StyleRuleKeyframes* rule, CSSStyleSheet* sheet)
    {
        return new CSSKeyframesRule(rule, sheet);
    }

    ~CSSKeyframesRule() override;

    StyleRuleKeyframes* keyframes() { return m_keyframesRule.get(); }

    String cssText() const override;
    void reattach(StyleRuleBase*) override;

    String name() const { return m_keyframesRule->name(); }
    void setName(const String&);

    CSSRuleList* cssRules() const override;

    void appendRule(const String& rule);
    void deleteRule(const String& key);
    CSSKeyframeRule* findRule(const String& key);

    // For IndexedGetter and CSSRuleList.
    unsigned length() const;
    CSSKeyframeRule* item(unsigned index) const;
    CSSKeyframeRule* anonymousIndexedGetter(unsigned index) const;

    bool isVendorPrefixed() const { return m_isPrefixed; }
    void setVendorPrefixed(bool isPrefixed) { m_isPrefixed = isPrefixed; }

    void styleChanged() { m_keyframesRule->styleChanged(); }

    DECLARE_VIRTUAL_TRACE();

private:
    CSSKeyframesRule(StyleRuleKeyframes*, CSSStyleSheet* parent);

    CSSRule::Type type() const override { return KEYFRAMES_RULE; }

    Member<StyleRuleKeyframes> m_keyframesRule;
    mutable HeapVector<Member<CSSKeyframeRule>> m_childRuleCSSOMWrappers;
    mutable Member<CSSRuleList> m_ruleListCSSOMWrapper;
    bool m_isPrefixed;
};

DEFINE_CSS_RULE_TYPE_CASTS(CSSKeyframesRule, KEYFRAMES_RULE);

} // namespace blink

#endif // CSSKeyframesRule_h
