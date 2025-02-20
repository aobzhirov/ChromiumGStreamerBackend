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

#include "core/svg/SVGBoolean.h"

#include "core/svg/SVGAnimationElement.h"

namespace blink {

String SVGBoolean::valueAsString() const
{
    return m_value ? "true" : "false";
}

SVGParsingError SVGBoolean::setValueAsString(const String& value)
{
    if (value == "true") {
        m_value = true;
        return SVGParseStatus::NoError;
    }
    if (value == "false") {
        m_value = false;
        return SVGParseStatus::NoError;
    }
    return SVGParseStatus::ExpectedBoolean;
}

void SVGBoolean::add(RawPtr<SVGPropertyBase>, SVGElement*)
{
    ASSERT_NOT_REACHED();
}

void SVGBoolean::calculateAnimatedValue(SVGAnimationElement* animationElement, float percentage, unsigned repeatCount, RawPtr<SVGPropertyBase> from, RawPtr<SVGPropertyBase> to, RawPtr<SVGPropertyBase>, SVGElement*)
{
    ASSERT(animationElement);
    bool fromBoolean = animationElement->getAnimationMode() == ToAnimation ? m_value : toSVGBoolean(from)->value();
    bool toBoolean = toSVGBoolean(to)->value();

    animationElement->animateDiscreteType<bool>(percentage, fromBoolean, toBoolean, m_value);
}

float SVGBoolean::calculateDistance(RawPtr<SVGPropertyBase>, SVGElement*)
{
    // No paced animations for boolean.
    return -1;
}

} // namespace blink
