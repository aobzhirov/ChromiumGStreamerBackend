/*
 * Copyright (C) 2014 Google Inc. All rights reserved.
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

#include "core/svg/SVGAngleTearOff.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ExceptionStatePlaceholder.h"
#include "core/dom/ExceptionCode.h"
#include "core/svg/SVGElement.h"

namespace blink {

SVGAngleTearOff::SVGAngleTearOff(RawPtr<SVGAngle> targetProperty, SVGElement* contextElement, PropertyIsAnimValType propertyIsAnimVal, const QualifiedName& attributeName)
    : SVGPropertyTearOff<SVGAngle>(targetProperty, contextElement, propertyIsAnimVal, attributeName)
{
}

SVGAngleTearOff::~SVGAngleTearOff()
{
}

void SVGAngleTearOff::setValue(float value, ExceptionState& exceptionState)
{
    if (isImmutable()) {
        exceptionState.throwDOMException(NoModificationAllowedError, "The attribute is read-only.");
        return;
    }

    target()->setValue(value);
    commitChange();
}

void SVGAngleTearOff::setValueInSpecifiedUnits(float value, ExceptionState& exceptionState)
{
    if (isImmutable()) {
        exceptionState.throwDOMException(NoModificationAllowedError, "The attribute is read-only.");
        return;
    }

    target()->setValueInSpecifiedUnits(value);
    commitChange();
}

void SVGAngleTearOff::newValueSpecifiedUnits(unsigned short unitType, float valueInSpecifiedUnits, ExceptionState& exceptionState)
{
    if (isImmutable()) {
        exceptionState.throwDOMException(NoModificationAllowedError, "The attribute is read-only.");
        return;
    }

    if (unitType == SVGAngle::SVG_ANGLETYPE_UNKNOWN || unitType > SVGAngle::SVG_ANGLETYPE_GRAD) {
        exceptionState.throwDOMException(NotSupportedError, "Cannot set value with unknown or invalid units (" + String::number(unitType) + ").");
        return;
    }

    target()->newValueSpecifiedUnits(static_cast<SVGAngle::SVGAngleType>(unitType), valueInSpecifiedUnits);
    commitChange();
}

void SVGAngleTearOff::convertToSpecifiedUnits(unsigned short unitType, ExceptionState& exceptionState)
{
    if (isImmutable()) {
        exceptionState.throwDOMException(NoModificationAllowedError, "The attribute is read-only.");
        return;
    }

    if (unitType == SVGAngle::SVG_ANGLETYPE_UNKNOWN || unitType > SVGAngle::SVG_ANGLETYPE_GRAD) {
        exceptionState.throwDOMException(NotSupportedError, "Cannot convert to unknown or invalid units (" + String::number(unitType) + ").");
        return;
    }

    if (target()->unitType() == SVGAngle::SVG_ANGLETYPE_UNKNOWN) {
        exceptionState.throwDOMException(NotSupportedError, "Cannot convert from unknown or invalid units.");
        return;
    }

    target()->convertToSpecifiedUnits(static_cast<SVGAngle::SVGAngleType>(unitType));
    commitChange();
}

void SVGAngleTearOff::setValueAsString(const String& value, ExceptionState& exceptionState)
{
    if (isImmutable()) {
        exceptionState.throwDOMException(NoModificationAllowedError, "The attribute is read-only.");
        return;
    }

    String oldValue = target()->valueAsString();

    SVGParsingError status = target()->setValueAsString(value);

    if (status == SVGParseStatus::NoError && !hasExposedAngleUnit()) {
        target()->setValueAsString(oldValue); // rollback to old value
        status = SVGParseStatus::ParsingFailed;
    }
    if (status != SVGParseStatus::NoError) {
        exceptionState.throwDOMException(SyntaxError, "The value provided ('" + value + "') is invalid.");
        return;
    }

    commitChange();
}

} // namespace blink
