/*
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
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

#ifndef SVGGeometryElement_h
#define SVGGeometryElement_h

#include "core/svg/SVGGraphicsElement.h"

namespace blink {

class SVGPointTearOff;

class SVGGeometryElement : public SVGGraphicsElement {
    DEFINE_WRAPPERTYPEINFO();
public:
    virtual Path asPath() const = 0;
    bool isPointInFill(RawPtr<SVGPointTearOff>) const;
    bool isPointInStroke(RawPtr<SVGPointTearOff>) const;

    void toClipPath(Path&) const;

    LayoutObject* createLayoutObject(const ComputedStyle&) override;

protected:
    SVGGeometryElement(const QualifiedName&, Document&, ConstructionType = CreateSVGElement);

private:
    bool isSVGGeometryElement() const final { return true; }
};

inline bool isSVGGeometryElement(const SVGElement& element)
{
    return element.isSVGGeometryElement();
}

DEFINE_SVGELEMENT_TYPE_CASTS_WITH_FUNCTION(SVGGeometryElement);

} // namespace blink

#endif // SVGGeometryElement_h
