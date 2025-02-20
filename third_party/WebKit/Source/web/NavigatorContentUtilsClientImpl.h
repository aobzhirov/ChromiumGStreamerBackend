// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NavigatorContentUtilsClientImpl_h
#define NavigatorContentUtilsClientImpl_h

#include "modules/navigatorcontentutils/NavigatorContentUtilsClient.h"
#include "platform/heap/Handle.h"
#include "platform/weborigin/KURL.h"

namespace blink {

class WebLocalFrameImpl;

class NavigatorContentUtilsClientImpl final : public NavigatorContentUtilsClient {
public:
    static RawPtr<NavigatorContentUtilsClientImpl> create(WebLocalFrameImpl*);
    ~NavigatorContentUtilsClientImpl() override { }

    void registerProtocolHandler(const String& scheme, const KURL&, const String& title) override;
    CustomHandlersState isProtocolHandlerRegistered(const String& scheme, const KURL&) override;
    void unregisterProtocolHandler(const String& scheme, const KURL&) override;

    DECLARE_VIRTUAL_TRACE();

private:
    explicit NavigatorContentUtilsClientImpl(WebLocalFrameImpl*);

    Member<WebLocalFrameImpl> m_webFrame;
};

} // namespace blink

#endif // NavigatorContentUtilsClientImpl_h
