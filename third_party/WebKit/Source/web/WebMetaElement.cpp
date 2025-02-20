// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/web/WebMetaElement.h"

#include "core/HTMLNames.h"
#include "core/html/HTMLMetaElement.h"
#include "public/platform/WebString.h"
#include "wtf/PassRefPtr.h"

namespace blink {

WebString WebMetaElement::computeEncoding() const
{
    return String(constUnwrap<HTMLMetaElement>()->computeEncoding().name());
}

WebMetaElement::WebMetaElement(const RawPtr<HTMLMetaElement>& element)
    : WebElement(element)
{
}

DEFINE_WEB_NODE_TYPE_CASTS(WebMetaElement, isHTMLMetaElement(constUnwrap<Node>()));

WebMetaElement& WebMetaElement::operator=(const RawPtr<HTMLMetaElement>& element)
{
    m_private = element;
    return *this;
}

WebMetaElement::operator RawPtr<HTMLMetaElement>() const
{
    return toHTMLMetaElement(m_private.get());
}

} // namespace blink
