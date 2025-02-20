/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * (C) 2002-2003 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2002, 2006, 2008, 2012 Apple Inc. All rights reserved.
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

#ifndef CSSImportRule_h
#define CSSImportRule_h

#include "core/css/CSSRule.h"
#include "platform/heap/Handle.h"

namespace blink {

class MediaList;
class StyleRuleImport;

class CSSImportRule final : public CSSRule {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<CSSImportRule> create(StyleRuleImport* rule, CSSStyleSheet* sheet)
    {
        return new CSSImportRule(rule, sheet);
    }

    ~CSSImportRule() override;

    String cssText() const override;
    void reattach(StyleRuleBase*) override;

    String href() const;
    MediaList* media() const;
    CSSStyleSheet* styleSheet() const;

    DECLARE_VIRTUAL_TRACE();

private:
    CSSImportRule(StyleRuleImport*, CSSStyleSheet*);

    CSSRule::Type type() const override { return IMPORT_RULE; }

    Member<StyleRuleImport> m_importRule;
    mutable Member<MediaList> m_mediaCSSOMWrapper;
    mutable Member<CSSStyleSheet> m_styleSheetCSSOMWrapper;
};

DEFINE_CSS_RULE_TYPE_CASTS(CSSImportRule, IMPORT_RULE);

} // namespace blink

#endif // CSSImportRule_h
