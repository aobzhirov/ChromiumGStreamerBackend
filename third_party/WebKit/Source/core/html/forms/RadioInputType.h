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

#ifndef RadioInputType_h
#define RadioInputType_h

#include "core/CoreExport.h"
#include "core/html/forms/BaseCheckableInputType.h"

namespace blink {

class RadioInputType final : public BaseCheckableInputType {
public:
    static RawPtr<InputType> create(HTMLInputElement&);
    CORE_EXPORT static HTMLInputElement* nextRadioButtonInGroup(HTMLInputElement* , bool forward);

private:
    RadioInputType(HTMLInputElement& element) : BaseCheckableInputType(element) { }
    const AtomicString& formControlType() const override;
    bool valueMissing(const String&) const override;
    String valueMissingText() const override;
    void handleClickEvent(MouseEvent*) override;
    void handleKeydownEvent(KeyboardEvent*) override;
    void handleKeyupEvent(KeyboardEvent*) override;
    bool isKeyboardFocusable() const override;
    bool shouldSendChangeEventAfterCheckedChanged() override;
    RawPtr<ClickHandlingState> willDispatchClick() override;
    void didDispatchClick(Event*, const ClickHandlingState&) override;
    bool shouldAppearIndeterminate() const override;

    HTMLInputElement* findNextFocusableRadioButtonInGroup(HTMLInputElement*, bool);
};

} // namespace blink

#endif // RadioInputType_h
