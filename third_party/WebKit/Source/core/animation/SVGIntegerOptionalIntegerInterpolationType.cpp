// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/animation/SVGIntegerOptionalIntegerInterpolationType.h"

#include "core/animation/InterpolationEnvironment.h"
#include "core/svg/SVGIntegerOptionalInteger.h"

namespace blink {

InterpolationValue SVGIntegerOptionalIntegerInterpolationType::maybeConvertNeutral(const InterpolationValue&, ConversionCheckers&) const
{
    OwnPtr<InterpolableList> result = InterpolableList::create(2);
    result->set(0, InterpolableNumber::create(0));
    result->set(1, InterpolableNumber::create(0));
    return InterpolationValue(result.release());
}

InterpolationValue SVGIntegerOptionalIntegerInterpolationType::maybeConvertSVGValue(const SVGPropertyBase& svgValue) const
{
    if (svgValue.type() != AnimatedIntegerOptionalInteger)
        return nullptr;

    const SVGIntegerOptionalInteger& integerOptionalInteger = toSVGIntegerOptionalInteger(svgValue);
    OwnPtr<InterpolableList> result = InterpolableList::create(2);
    result->set(0, InterpolableNumber::create(integerOptionalInteger.firstInteger()->value()));
    result->set(1, InterpolableNumber::create(integerOptionalInteger.secondInteger()->value()));
    return InterpolationValue(result.release());
}

static RawPtr<SVGInteger> toPositiveInteger(const InterpolableValue* number)
{
    return SVGInteger::create(clampTo<int>(roundf(toInterpolableNumber(number)->value()), 1));
}

RawPtr<SVGPropertyBase> SVGIntegerOptionalIntegerInterpolationType::appliedSVGValue(const InterpolableValue& interpolableValue, const NonInterpolableValue*) const
{
    const InterpolableList& list = toInterpolableList(interpolableValue);
    return SVGIntegerOptionalInteger::create(
        toPositiveInteger(list.get(0)),
        toPositiveInteger(list.get(1)));
}

} // namespace blink
