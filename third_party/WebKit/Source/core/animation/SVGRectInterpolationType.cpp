// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "core/animation/SVGRectInterpolationType.h"

#include "core/animation/InterpolationEnvironment.h"
#include "core/animation/StringKeyframe.h"
#include "core/svg/SVGRect.h"
#include "wtf/StdLibExtras.h"

namespace blink {

enum RectComponentIndex {
    RectX,
    RectY,
    RectWidth,
    RectHeight,
    RectComponentIndexCount,
};

InterpolationValue SVGRectInterpolationType::maybeConvertNeutral(const InterpolationValue&, ConversionCheckers&) const
{
    OwnPtr<InterpolableList> result = InterpolableList::create(RectComponentIndexCount);
    for (size_t i = 0; i < RectComponentIndexCount; i++)
        result->set(i, InterpolableNumber::create(0));
    return InterpolationValue(result.release());
}

InterpolationValue SVGRectInterpolationType::maybeConvertSVGValue(const SVGPropertyBase& svgValue) const
{
    if (svgValue.type() != AnimatedRect)
        return nullptr;

    const SVGRect& rect = toSVGRect(svgValue);
    OwnPtr<InterpolableList> result = InterpolableList::create(RectComponentIndexCount);
    result->set(RectX, InterpolableNumber::create(rect.x()));
    result->set(RectY, InterpolableNumber::create(rect.y()));
    result->set(RectWidth, InterpolableNumber::create(rect.width()));
    result->set(RectHeight, InterpolableNumber::create(rect.height()));
    return InterpolationValue(result.release());
}

RawPtr<SVGPropertyBase> SVGRectInterpolationType::appliedSVGValue(const InterpolableValue& interpolableValue, const NonInterpolableValue*) const
{
    const InterpolableList& list = toInterpolableList(interpolableValue);
    RawPtr<SVGRect> result = SVGRect::create();
    result->setX(toInterpolableNumber(list.get(RectX))->value());
    result->setY(toInterpolableNumber(list.get(RectY))->value());
    result->setWidth(toInterpolableNumber(list.get(RectWidth))->value());
    result->setHeight(toInterpolableNumber(list.get(RectHeight))->value());
    return result;
}

} // namespace blink
