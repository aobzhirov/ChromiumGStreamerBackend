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

#include "public/web/WebPluginDocument.h"

#include "core/dom/Document.h"
#include "core/html/PluginDocument.h"

#include "web/WebPluginContainerImpl.h"

#include "wtf/PassRefPtr.h"


namespace blink {


WebPlugin* WebPluginDocument::plugin()
{
    if (!isPluginDocument())
        return 0;
    PluginDocument* doc = unwrap<PluginDocument>();
    WebPluginContainerImpl* container = toWebPluginContainerImpl(doc->pluginWidget());
    return container ? container->plugin() : 0;
}


WebPluginDocument::WebPluginDocument(const RawPtr<PluginDocument>& elem)
    : WebDocument(elem)
{
}

DEFINE_WEB_NODE_TYPE_CASTS(WebPluginDocument, isDocumentNode() && constUnwrap<Document>()->isPluginDocument());

WebPluginDocument& WebPluginDocument::operator=(const RawPtr<PluginDocument>& elem)
{
    m_private = elem;
    return *this;
}

WebPluginDocument::operator RawPtr<PluginDocument>() const
{
    return static_cast<PluginDocument*>(m_private.get());
}

} // namespace blink
