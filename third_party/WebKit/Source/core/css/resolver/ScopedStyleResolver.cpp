/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/css/resolver/ScopedStyleResolver.h"

#include "core/HTMLNames.h"
#include "core/css/CSSFontSelector.h"
#include "core/css/CSSStyleSheet.h"
#include "core/css/FontFace.h"
#include "core/css/PageRuleCollector.h"
#include "core/css/RuleFeature.h"
#include "core/css/StyleRule.h"
#include "core/css/StyleSheetContents.h"
#include "core/css/resolver/MatchRequest.h"
#include "core/css/resolver/ViewportStyleResolver.h"
#include "core/dom/Document.h"
#include "core/dom/StyleEngine.h"
#include "core/dom/shadow/ElementShadow.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/html/HTMLStyleElement.h"
#include "core/svg/SVGStyleElement.h"

namespace blink {

ScopedStyleResolver* ScopedStyleResolver::parent() const
{
    for (TreeScope* scope = treeScope().parentTreeScope(); scope; scope = scope->parentTreeScope()) {
        if (ScopedStyleResolver* resolver = scope->scopedStyleResolver())
            return resolver;
    }
    return nullptr;
}

void ScopedStyleResolver::addKeyframeRules(const RuleSet& ruleSet)
{
    const HeapVector<Member<StyleRuleKeyframes>> keyframesRules = ruleSet.keyframesRules();
    for (unsigned i = 0; i < keyframesRules.size(); ++i)
        addKeyframeStyle(keyframesRules[i]);
}

void ScopedStyleResolver::addFontFaceRules(const RuleSet& ruleSet)
{
    // FIXME(BUG 72461): We don't add @font-face rules of scoped style sheets for the moment.
    if (!treeScope().rootNode().isDocumentNode())
        return;

    Document& document = treeScope().document();
    CSSFontSelector* cssFontSelector = document.styleEngine().fontSelector();
    const HeapVector<Member<StyleRuleFontFace>> fontFaceRules = ruleSet.fontFaceRules();
    for (auto& fontFaceRule : fontFaceRules) {
        if (RawPtr<FontFace> fontFace = FontFace::create(&document, fontFaceRule))
            cssFontSelector->fontFaceCache()->add(cssFontSelector, fontFaceRule, fontFace);
    }
    if (fontFaceRules.size())
        document.styleResolver()->invalidateMatchedPropertiesCache();
}

void ScopedStyleResolver::appendCSSStyleSheet(CSSStyleSheet& cssSheet, const MediaQueryEvaluator& medium)
{
    unsigned index = m_authorStyleSheets.size();
    m_authorStyleSheets.append(&cssSheet);
    StyleSheetContents* sheet = cssSheet.contents();
    AddRuleFlags addRuleFlags = treeScope().document().getSecurityOrigin()->canRequest(sheet->baseURL()) ? RuleHasDocumentSecurityOrigin : RuleHasNoSpecialState;
    const RuleSet& ruleSet = sheet->ensureRuleSet(medium, addRuleFlags);

    addKeyframeRules(ruleSet);
    addFontFaceRules(ruleSet);
    addTreeBoundaryCrossingRules(ruleSet, &cssSheet, index);
    treeScope().document().styleResolver()->addViewportDependentMediaQueries(ruleSet.viewportDependentMediaQueryResults());
    treeScope().document().styleResolver()->addDeviceDependentMediaQueries(ruleSet.deviceDependentMediaQueryResults());
}

void ScopedStyleResolver::collectFeaturesTo(RuleFeatureSet& features, HeapHashSet<Member<const StyleSheetContents>>& visitedSharedStyleSheetContents) const
{
    for (size_t i = 0; i < m_authorStyleSheets.size(); ++i) {
        ASSERT(m_authorStyleSheets[i]->ownerNode());
        StyleSheetContents* contents = m_authorStyleSheets[i]->contents();
        if (contents->hasOneClient() || visitedSharedStyleSheetContents.add(contents).isNewEntry)
            features.add(contents->ruleSet().features());
    }

    if (!m_treeBoundaryCrossingRuleSet)
        return;

    for (const auto& rules : *m_treeBoundaryCrossingRuleSet)
        features.add(rules->m_ruleSet->features());
}

void ScopedStyleResolver::resetAuthorStyle()
{
    m_authorStyleSheets.clear();
    m_keyframesRuleMap.clear();
    m_treeBoundaryCrossingRuleSet = nullptr;
    m_hasDeepOrShadowSelector = false;
}

StyleRuleKeyframes* ScopedStyleResolver::keyframeStylesForAnimation(const StringImpl* animationName)
{
    if (m_keyframesRuleMap.isEmpty())
        return nullptr;

    KeyframesRuleMap::iterator it = m_keyframesRuleMap.find(animationName);
    if (it == m_keyframesRuleMap.end())
        return nullptr;

    return it->value.get();
}

void ScopedStyleResolver::addKeyframeStyle(RawPtr<StyleRuleKeyframes> rule)
{
    AtomicString s(rule->name());

    if (rule->isVendorPrefixed()) {
        KeyframesRuleMap::iterator it = m_keyframesRuleMap.find(s.impl());
        if (it == m_keyframesRuleMap.end())
            m_keyframesRuleMap.set(s.impl(), rule);
        else if (it->value->isVendorPrefixed())
            m_keyframesRuleMap.set(s.impl(), rule);
    } else {
        m_keyframesRuleMap.set(s.impl(), rule);
    }
}

void ScopedStyleResolver::collectMatchingAuthorRules(ElementRuleCollector& collector, CascadeOrder cascadeOrder)
{
    for (size_t i = 0; i < m_authorStyleSheets.size(); ++i) {
        ASSERT(m_authorStyleSheets[i]->ownerNode());
        MatchRequest matchRequest(&m_authorStyleSheets[i]->contents()->ruleSet(), &m_scope->rootNode(), m_authorStyleSheets[i], i);
        collector.collectMatchingRules(matchRequest, cascadeOrder);
    }
}

void ScopedStyleResolver::collectMatchingShadowHostRules(ElementRuleCollector& collector, CascadeOrder cascadeOrder)
{
    for (size_t i = 0; i < m_authorStyleSheets.size(); ++i) {
        ASSERT(m_authorStyleSheets[i]->ownerNode());
        MatchRequest matchRequest(&m_authorStyleSheets[i]->contents()->ruleSet(), &m_scope->rootNode(), m_authorStyleSheets[i], i);
        collector.collectMatchingShadowHostRules(matchRequest, cascadeOrder);
    }
}

void ScopedStyleResolver::collectMatchingTreeBoundaryCrossingRules(ElementRuleCollector& collector, CascadeOrder cascadeOrder)
{
    if (!m_treeBoundaryCrossingRuleSet)
        return;

    for (const auto& rules : *m_treeBoundaryCrossingRuleSet) {
        MatchRequest request(rules->m_ruleSet.get(), &treeScope().rootNode(), rules->m_parentStyleSheet, rules->m_parentIndex);
        collector.collectMatchingRules(request, cascadeOrder, true);
    }
}

void ScopedStyleResolver::matchPageRules(PageRuleCollector& collector)
{
    // Only consider the global author RuleSet for @page rules, as per the HTML5 spec.
    ASSERT(m_scope->rootNode().isDocumentNode());
    for (size_t i = 0; i < m_authorStyleSheets.size(); ++i)
        collector.matchPageRules(&m_authorStyleSheets[i]->contents()->ruleSet());
}

void ScopedStyleResolver::collectViewportRulesTo(ViewportStyleResolver* resolver) const
{
    if (!m_scope->rootNode().isDocumentNode())
        return;
    for (size_t i = 0; i < m_authorStyleSheets.size(); ++i)
        resolver->collectViewportRules(&m_authorStyleSheets[i]->contents()->ruleSet(), ViewportStyleResolver::AuthorOrigin);
}

DEFINE_TRACE(ScopedStyleResolver)
{
#if ENABLE(OILPAN)
    visitor->trace(m_scope);
    visitor->trace(m_authorStyleSheets);
    visitor->trace(m_keyframesRuleMap);
    visitor->trace(m_treeBoundaryCrossingRuleSet);
#endif
}

static void addRules(RuleSet* ruleSet, const HeapVector<MinimalRuleData>& rules)
{
    for (unsigned i = 0; i < rules.size(); ++i) {
        const MinimalRuleData& info = rules[i];
        ruleSet->addRule(info.m_rule, info.m_selectorIndex, info.m_flags);
    }
}

void ScopedStyleResolver::addTreeBoundaryCrossingRules(const RuleSet& authorRules, CSSStyleSheet* parentStyleSheet, unsigned sheetIndex)
{
    bool isDocumentScope = treeScope().rootNode().isDocumentNode();
    if (authorRules.deepCombinatorOrShadowPseudoRules().isEmpty()
        && (isDocumentScope || (authorRules.contentPseudoElementRules().isEmpty() && authorRules.slottedPseudoElementRules().isEmpty())))
        return;

    if (!authorRules.deepCombinatorOrShadowPseudoRules().isEmpty())
        m_hasDeepOrShadowSelector = true;

    RawPtr<RuleSet> ruleSetForScope = RuleSet::create();
    addRules(ruleSetForScope.get(), authorRules.deepCombinatorOrShadowPseudoRules());

    if (!isDocumentScope) {
        addRules(ruleSetForScope.get(), authorRules.contentPseudoElementRules());
        addRules(ruleSetForScope.get(), authorRules.slottedPseudoElementRules());
    }

    if (!m_treeBoundaryCrossingRuleSet) {
        m_treeBoundaryCrossingRuleSet = new CSSStyleSheetRuleSubSet();
        treeScope().document().styleResolver()->addTreeBoundaryCrossingScope(treeScope().rootNode());
    }

    m_treeBoundaryCrossingRuleSet->append(RuleSubSet::create(parentStyleSheet, sheetIndex, ruleSetForScope.release()));
}

DEFINE_TRACE(ScopedStyleResolver::RuleSubSet)
{
    visitor->trace(m_parentStyleSheet);
    visitor->trace(m_ruleSet);
}

} // namespace blink
