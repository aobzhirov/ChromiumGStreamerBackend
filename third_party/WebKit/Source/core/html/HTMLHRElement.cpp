/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2010 Apple Inc. All rights reserved.
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
 *
 */

#include "core/html/HTMLHRElement.h"

#include "core/CSSPropertyNames.h"
#include "core/CSSValueKeywords.h"
#include "core/HTMLNames.h"
#include "core/css/CSSValuePool.h"
#include "core/css/StylePropertySet.h"

namespace blink {

using namespace HTMLNames;

inline HTMLHRElement::HTMLHRElement(Document& document)
    : HTMLElement(hrTag, document)
{
}

DEFINE_NODE_FACTORY(HTMLHRElement)

bool HTMLHRElement::isPresentationAttribute(const QualifiedName& name) const
{
    if (name == alignAttr || name == widthAttr || name == colorAttr || name == noshadeAttr || name == sizeAttr)
        return true;
    return HTMLElement::isPresentationAttribute(name);
}

void HTMLHRElement::collectStyleForPresentationAttribute(const QualifiedName& name, const AtomicString& value, MutableStylePropertySet* style)
{
    if (name == alignAttr) {
        if (equalIgnoringCase(value, "left")) {
            addPropertyToPresentationAttributeStyle(style, CSSPropertyMarginLeft, 0, CSSPrimitiveValue::UnitType::Pixels);
            addPropertyToPresentationAttributeStyle(style, CSSPropertyMarginRight, CSSValueAuto);
        } else if (equalIgnoringCase(value, "right")) {
            addPropertyToPresentationAttributeStyle(style, CSSPropertyMarginLeft, CSSValueAuto);
            addPropertyToPresentationAttributeStyle(style, CSSPropertyMarginRight, 0, CSSPrimitiveValue::UnitType::Pixels);
        } else {
            addPropertyToPresentationAttributeStyle(style, CSSPropertyMarginLeft, CSSValueAuto);
            addPropertyToPresentationAttributeStyle(style, CSSPropertyMarginRight, CSSValueAuto);
        }
    } else if (name == widthAttr) {
        bool ok;
        int v = value.toInt(&ok);
        if (ok && !v)
            addPropertyToPresentationAttributeStyle(style, CSSPropertyWidth, 1, CSSPrimitiveValue::UnitType::Pixels);
        else
            addHTMLLengthToStyle(style, CSSPropertyWidth, value);
    } else if (name == colorAttr) {
        addPropertyToPresentationAttributeStyle(style, CSSPropertyBorderStyle, CSSValueSolid);
        addHTMLColorToStyle(style, CSSPropertyBorderColor, value);
        addHTMLColorToStyle(style, CSSPropertyBackgroundColor, value);
    } else if (name == noshadeAttr) {
        if (!hasAttribute(colorAttr)) {
            addPropertyToPresentationAttributeStyle(style, CSSPropertyBorderStyle, CSSValueSolid);

            RawPtr<CSSColorValue> darkGrayValue = cssValuePool().createColorValue(Color::darkGray);
            style->setProperty(CSSPropertyBorderColor, darkGrayValue);
            style->setProperty(CSSPropertyBackgroundColor, darkGrayValue);
        }
    } else if (name == sizeAttr) {
        StringImpl* si = value.impl();
        int size = si->toInt();
        if (size <= 1)
            addPropertyToPresentationAttributeStyle(style, CSSPropertyBorderBottomWidth, 0, CSSPrimitiveValue::UnitType::Pixels);
        else
            addPropertyToPresentationAttributeStyle(style, CSSPropertyHeight, size - 2, CSSPrimitiveValue::UnitType::Pixels);
    } else {
        HTMLElement::collectStyleForPresentationAttribute(name, value, style);
    }
}

} // namespace blink
