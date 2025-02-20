// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ComputedStyleCSSValueMapping_h
#define ComputedStyleCSSValueMapping_h

#include "core/CSSPropertyNames.h"
#include "core/css/CSSValue.h"
#include "wtf/Allocator.h"

namespace blink {

class LayoutObject;
class ComputedStyle;
class FilterOperations;
class ShadowData;
class ShadowList;
class StyleColor;
class Node;
class CSSVariableData;

class ComputedStyleCSSValueMapping {
    STATIC_ONLY(ComputedStyleCSSValueMapping);
public:
    // FIXME: Resolve computed auto alignment in applyProperty/ComputedStyle and remove this non-const styledNode parameter.
    static RawPtr<CSSValue> get(CSSPropertyID, const ComputedStyle&, const LayoutObject* = nullptr, Node* styledNode = nullptr, bool allowVisitedStyle = false);
    static RawPtr<CSSValue> get(const AtomicString customPropertyName, const ComputedStyle&);
    static const HashMap<AtomicString, RefPtr<CSSVariableData>>* getVariables(const ComputedStyle&);
private:
    static RawPtr<CSSValue> currentColorOrValidColor(const ComputedStyle&, const StyleColor&);
    static RawPtr<CSSValue> valueForShadowData(const ShadowData&, const ComputedStyle&, bool useSpread);
    static RawPtr<CSSValue> valueForShadowList(const ShadowList*, const ComputedStyle&, bool useSpread);
    static RawPtr<CSSValue> valueForFilter(const ComputedStyle&, const FilterOperations&);
    static RawPtr<CSSValue> valueForFont(const ComputedStyle&);
};

} // namespace blink

#endif // ComputedStyleCSSValueMapping_h
