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

#ifndef SVGAnimatedString_h
#define SVGAnimatedString_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "core/svg/SVGString.h"
#include "core/svg/properties/SVGAnimatedProperty.h"

namespace blink {

class SVGAnimatedString : public SVGAnimatedProperty<SVGString>, public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<SVGAnimatedString> create(SVGElement* contextElement, const QualifiedName& attributeName, RawPtr<SVGString> initialValue)
    {
        return new SVGAnimatedString(contextElement, attributeName, initialValue);
    }

    virtual String baseVal();
    virtual void setBaseVal(const String&, ExceptionState&);
    virtual String animVal();

protected:
    SVGAnimatedString(SVGElement* contextElement, const QualifiedName& attributeName, RawPtr<SVGString> initialValue)
        : SVGAnimatedProperty<SVGString>(contextElement, attributeName, initialValue)
    {
    }
};

} // namespace blink

#endif // SVGAnimatedString_h
