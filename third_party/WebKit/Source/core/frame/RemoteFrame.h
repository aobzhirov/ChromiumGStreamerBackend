// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RemoteFrame_h
#define RemoteFrame_h

#include "core/CoreExport.h"
#include "core/dom/RemoteSecurityContext.h"
#include "core/frame/Frame.h"
#include "public/platform/WebFocusType.h"

namespace blink {

class Event;
class IntRect;
class LocalFrame;
class RemoteDOMWindow;
class RemoteFrameClient;
class RemoteFrameView;
class WebLayer;
class WindowProxyManager;
struct FrameLoadRequest;

class CORE_EXPORT RemoteFrame: public Frame {
public:
    static RawPtr<RemoteFrame> create(RemoteFrameClient*, FrameHost*, FrameOwner*);

    ~RemoteFrame() override;

    // Frame overrides:
    DECLARE_VIRTUAL_TRACE();
    bool isRemoteFrame() const override { return true; }
    DOMWindow* domWindow() const override;
    WindowProxy* windowProxy(DOMWrapperWorld&) override;
    void navigate(Document& originDocument, const KURL&, bool replaceCurrentItem, UserGestureStatus) override;
    void navigate(const FrameLoadRequest& passedRequest) override;
    void reload(FrameLoadType, ClientRedirectPolicy) override;
    void detach(FrameDetachType) override;
    RemoteSecurityContext* securityContext() const override;
    void printNavigationErrorMessage(const Frame&, const char* reason) override { }
    bool prepareForCommit() override;
    bool shouldClose() override;

    // FIXME: Remove this method once we have input routing in the browser
    // process. See http://crbug.com/339659.
    void forwardInputEvent(Event*);

    void frameRectsChanged(const IntRect& frameRect);

    void visibilityChanged(bool visible);

    void setRemotePlatformLayer(WebLayer*);
    WebLayer* remotePlatformLayer() const { return m_remotePlatformLayer; }

    void advanceFocus(WebFocusType, LocalFrame* source);

    void setView(RawPtr<RemoteFrameView>);
    void createView();

    RemoteFrameView* view() const;

private:
    RemoteFrame(RemoteFrameClient*, FrameHost*, FrameOwner*);

    // Internal Frame helper overrides:
    WindowProxyManager* getWindowProxyManager() const override { return m_windowProxyManager.get(); }

    RemoteFrameClient* remoteFrameClient() const;

    Member<RemoteFrameView> m_view;
    Member<RemoteSecurityContext> m_securityContext;
    Member<RemoteDOMWindow> m_domWindow;
    Member<WindowProxyManager> m_windowProxyManager;
    WebLayer* m_remotePlatformLayer;
};

inline RemoteFrameView* RemoteFrame::view() const
{
    return m_view.get();
}

DEFINE_TYPE_CASTS(RemoteFrame, Frame, remoteFrame, remoteFrame->isRemoteFrame(), remoteFrame.isRemoteFrame());

} // namespace blink

#endif // RemoteFrame_h
