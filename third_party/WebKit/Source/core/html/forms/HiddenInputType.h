/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#ifndef HiddenInputType_h
#define HiddenInputType_h

#include "core/html/forms/InputType.h"

namespace blink {

class HiddenInputType final : public InputType {
public:
    static RawPtr<InputType> create(HTMLInputElement&);

private:
    HiddenInputType(HTMLInputElement& element) : InputType(element) { }
    const AtomicString& formControlType() const override;
    FormControlState saveFormControlState() const override;
    void restoreFormControlState(const FormControlState&) override;
    bool supportsValidation() const override;
    LayoutObject* createLayoutObject(const ComputedStyle&) const override;
    void accessKeyAction(bool sendMouseEvents) override;
    bool layoutObjectIsNeeded() override;
    bool storesValueSeparateFromAttribute() override;
    bool isInteractiveContent() const override { return false; }
    bool shouldRespectHeightAndWidthAttributes() override;
    void setValue(const String&, bool, TextFieldEventBehavior) override;
    void appendToFormData(FormData&) const override;
};

} // namespace blink

#endif // HiddenInputType_h
