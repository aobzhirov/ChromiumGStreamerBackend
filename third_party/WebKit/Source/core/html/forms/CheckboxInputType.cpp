/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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

#include "core/html/forms/CheckboxInputType.h"

#include "core/InputTypeNames.h"
#include "core/events/KeyboardEvent.h"
#include "core/html/HTMLInputElement.h"
#include "platform/text/PlatformLocale.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

RawPtr<InputType> CheckboxInputType::create(HTMLInputElement& element)
{
    return new CheckboxInputType(element);
}

const AtomicString& CheckboxInputType::formControlType() const
{
    return InputTypeNames::checkbox;
}

bool CheckboxInputType::valueMissing(const String&) const
{
    return element().isRequired() && !element().checked();
}

String CheckboxInputType::valueMissingText() const
{
    return locale().queryString(WebLocalizedString::ValidationValueMissingForCheckbox);
}

void CheckboxInputType::handleKeyupEvent(KeyboardEvent* event)
{
    const String& key = event->keyIdentifier();
    if (key != "U+0020")
        return;
    dispatchSimulatedClickIfActive(event);
}

RawPtr<ClickHandlingState> CheckboxInputType::willDispatchClick()
{
    // An event handler can use preventDefault or "return false" to reverse the checking we do here.
    // The ClickHandlingState object contains what we need to undo what we did here in didDispatchClick.

    RawPtr<ClickHandlingState> state = new ClickHandlingState;

    state->checked = element().checked();
    state->indeterminate = element().indeterminate();

    if (state->indeterminate)
        element().setIndeterminate(false);

    element().setChecked(!state->checked, DispatchChangeEvent);
    m_isInClickHandler = true;
    return state.release();
}

void CheckboxInputType::didDispatchClick(Event* event, const ClickHandlingState& state)
{
    if (event->defaultPrevented() || event->defaultHandled()) {
        element().setIndeterminate(state.indeterminate);
        element().setChecked(state.checked);
    } else {
        element().dispatchChangeEventIfNeeded();
    }
    m_isInClickHandler = false;
    // The work we did in willDispatchClick was default handling.
    event->setDefaultHandled();
}

bool CheckboxInputType::shouldAppearIndeterminate() const
{
    return element().indeterminate();
}

} // namespace blink
