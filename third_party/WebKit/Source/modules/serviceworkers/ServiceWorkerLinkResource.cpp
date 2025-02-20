// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/serviceworkers/ServiceWorkerLinkResource.h"

#include "core/dom/Document.h"
#include "core/frame/LocalFrame.h"
#include "core/html/HTMLLinkElement.h"
#include "core/loader/FrameLoaderClient.h"
#include "modules/serviceworkers/NavigatorServiceWorker.h"
#include "modules/serviceworkers/ServiceWorkerContainer.h"
#include "public/platform/Platform.h"
#include "public/platform/WebScheduler.h"

namespace blink {

namespace {

class RegistrationCallback : public WebServiceWorkerProvider::WebServiceWorkerRegistrationCallbacks {
public:
    explicit RegistrationCallback(LinkLoaderClient* client) : m_client(client) {}
    ~RegistrationCallback() override {}

    void onSuccess(WebPassOwnPtr<WebServiceWorkerRegistration::Handle> handle) override
    {
        Platform::current()->currentThread()->scheduler()->timerTaskRunner()->postTask(BLINK_FROM_HERE, bind(&LinkLoaderClient::linkLoaded, m_client));
    }

    void onError(const WebServiceWorkerError& error) override
    {
        Platform::current()->currentThread()->scheduler()->timerTaskRunner()->postTask(BLINK_FROM_HERE, bind(&LinkLoaderClient::linkLoadingErrored, m_client));
    }

private:
    WTF_MAKE_NONCOPYABLE(RegistrationCallback);

    Persistent<LinkLoaderClient> m_client;
};

}

RawPtr<ServiceWorkerLinkResource> ServiceWorkerLinkResource::create(HTMLLinkElement* owner)
{
    return new ServiceWorkerLinkResource(owner);
}

ServiceWorkerLinkResource::~ServiceWorkerLinkResource()
{
}

void ServiceWorkerLinkResource::process()
{
    if (!m_owner || !m_owner->document().frame())
        return;

    if (!m_owner->shouldLoadLink())
        return;

    Document& document = m_owner->document();

    KURL scriptURL = m_owner->href();

    String scope = m_owner->scope();
    KURL scopeURL;
    if (scope.isNull())
        scopeURL = KURL(scriptURL, "./");
    else
        scopeURL = document.completeURL(scope);
    scopeURL.removeFragmentIdentifier();

    TrackExceptionState exceptionState;

    NavigatorServiceWorker::serviceWorker(&document, *document.frame()->domWindow()->navigator(), exceptionState)->registerServiceWorkerImpl(&document, scriptURL, scopeURL, adoptPtr(new RegistrationCallback(m_owner)));
}

bool ServiceWorkerLinkResource::hasLoaded() const
{
    return false;
}

void ServiceWorkerLinkResource::ownerRemoved()
{
    process();
}

ServiceWorkerLinkResource::ServiceWorkerLinkResource(HTMLLinkElement* owner)
    : LinkResource(owner)
{
}

} // namespace blink
