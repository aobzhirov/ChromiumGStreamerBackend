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

#ifndef SVGStaticStringList_h
#define SVGStaticStringList_h

#include "core/svg/SVGStringListTearOff.h"
#include "core/svg/properties/SVGAnimatedProperty.h"
#include "platform/heap/Handle.h"

namespace blink {

class SVGElement;

// SVGStringList property implementations for SVGTests properties.
// Inherits SVGAnimatedPropertyBase to enable XML attribute synchronization, but this is never animated.
class SVGStaticStringList final : public SVGAnimatedPropertyBase {
public:
    static RawPtr<SVGStaticStringList> create(SVGElement* contextElement, const QualifiedName& attributeName)
    {
        return new SVGStaticStringList(contextElement, attributeName);
    }

    ~SVGStaticStringList() override;

    // SVGAnimatedPropertyBase:
    SVGPropertyBase* currentValueBase() override;
    const SVGPropertyBase& baseValueBase() const override;
    bool isAnimating() const override;
    RawPtr<SVGPropertyBase> createAnimatedValue() override;
    void setAnimatedValue(RawPtr<SVGPropertyBase>) override;
    void animationEnded() override;
    bool needsSynchronizeAttribute() override;

    SVGParsingError setBaseValueAsString(const String&) override;

    SVGStringList* value() { return m_value.get(); }
    SVGStringListTearOff* tearOff();

    DECLARE_VIRTUAL_TRACE();

private:
    SVGStaticStringList(SVGElement*, const QualifiedName&);

    Member<SVGStringList> m_value;
    Member<SVGStringListTearOff> m_tearOff;
};

} // namespace blink

#endif
