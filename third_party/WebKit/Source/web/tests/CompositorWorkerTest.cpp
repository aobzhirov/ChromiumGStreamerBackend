// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/FrameView.h"
#include "core/layout/LayoutView.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/Page.h"
#include "platform/graphics/CompositorMutableProperties.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/testing/URLTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebLayer.h"
#include "public/platform/WebLayerTreeView.h"
#include "public/platform/WebUnitTestSupport.h"
#include "public/web/WebSettings.h"
#include "public/web/WebViewClient.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebViewImpl.h"
#include "web/tests/FrameTestHelpers.h"
#include <gtest/gtest.h>

namespace blink {

class CompositorWorkerTest : public testing::Test {
public:
    CompositorWorkerTest()
        : m_baseURL("http://www.test.com/")
    {
        RuntimeEnabledFeatures::setCompositorWorkerEnabled(true);
        m_helper.initialize(true, 0, &m_mockWebViewClient, &configureSettings);
        webViewImpl()->resize(IntSize(320, 240));
    }

    ~CompositorWorkerTest() override
    {
        Platform::current()->unitTestSupport()->unregisterAllMockedURLs();
    }

    void navigateTo(const String& url)
    {
        FrameTestHelpers::loadFrame(webViewImpl()->mainFrame(), url.utf8().data());
    }

    void forceFullCompositingUpdate()
    {
        webViewImpl()->updateAllLifecyclePhases();
    }

    void registerMockedHttpURLLoad(const std::string& fileName)
    {
        URLTestHelpers::registerMockedURLFromBaseURL(m_baseURL, WebString::fromUTF8(fileName.c_str()));
    }

    WebLayer* getRootScrollLayer()
    {
        PaintLayerCompositor* compositor = frame()->contentLayoutObject()->compositor();
        DCHECK(compositor);
        DCHECK(compositor->scrollLayer());

        WebLayer* webScrollLayer = compositor->scrollLayer()->platformLayer();
        return webScrollLayer;
    }

    WebViewImpl* webViewImpl() const { return m_helper.webViewImpl(); }
    LocalFrame* frame() const { return m_helper.webViewImpl()->mainFrameImpl()->frame(); }

protected:
    String m_baseURL;
    FrameTestHelpers::TestWebViewClient m_mockWebViewClient;

private:
    static void configureSettings(WebSettings* settings)
    {
        settings->setJavaScriptEnabled(true);
        settings->setAcceleratedCompositingEnabled(true);
        settings->setPreferCompositingToLCDTextEnabled(true);
    }

    FrameTestHelpers::WebViewHelper m_helper;
    FrameTestHelpers::UseMockScrollbarSettings m_mockScrollbarSettings;
};

static CompositedLayerMapping* mappingFromElement(Element* element)
{
    if (!element)
        return nullptr;
    LayoutObject* layoutObject = element->layoutObject();
    if (!layoutObject || !layoutObject->isBoxModelObject())
        return nullptr;
    PaintLayer* layer = toLayoutBoxModelObject(layoutObject)->layer();
    if (!layer)
        return nullptr;
    if (!layer->hasCompositedLayerMapping())
        return nullptr;
    return layer->compositedLayerMapping();
}

static WebLayer* webLayerFromGraphicsLayer(GraphicsLayer* graphicsLayer)
{
    if (!graphicsLayer)
        return nullptr;
    return graphicsLayer->platformLayer();
}

static WebLayer* scrollingWebLayerFromElement(Element* element)
{
    CompositedLayerMapping* compositedLayerMapping = mappingFromElement(element);
    if (!compositedLayerMapping)
        return nullptr;
    return webLayerFromGraphicsLayer(compositedLayerMapping->scrollingContentsLayer());
}

static WebLayer* webLayerFromElement(Element* element)
{
    CompositedLayerMapping* compositedLayerMapping = mappingFromElement(element);
    if (!compositedLayerMapping)
        return nullptr;
    return webLayerFromGraphicsLayer(compositedLayerMapping->mainGraphicsLayer());
}

TEST_F(CompositorWorkerTest, plumbingElementIdAndMutableProperties)
{
    registerMockedHttpURLLoad("compositor-proxy-basic.html");
    navigateTo(m_baseURL + "compositor-proxy-basic.html");

    forceFullCompositingUpdate();

    Document* document = frame()->document();

    Element* tallElement = document->getElementById("tall");
    WebLayer* tallLayer = webLayerFromElement(tallElement);
    EXPECT_TRUE(!tallLayer);

    Element* proxiedElement = document->getElementById("proxied");
    WebLayer* proxiedLayer = webLayerFromElement(proxiedElement);
    EXPECT_TRUE(proxiedLayer->compositorMutableProperties() & CompositorMutableProperty::kTransform);
    EXPECT_FALSE(proxiedLayer->compositorMutableProperties() & (CompositorMutableProperty::kScrollLeft | CompositorMutableProperty::kScrollTop | CompositorMutableProperty::kOpacity));
    EXPECT_NE(0UL, proxiedLayer->elementId());

    Element* scrollElement = document->getElementById("proxied-scroller");
    WebLayer* scrollLayer = scrollingWebLayerFromElement(scrollElement);
    EXPECT_TRUE(scrollLayer->compositorMutableProperties() & (CompositorMutableProperty::kScrollLeft | CompositorMutableProperty::kScrollTop));
    EXPECT_FALSE(scrollLayer->compositorMutableProperties() & (CompositorMutableProperty::kTransform | CompositorMutableProperty::kOpacity));
    EXPECT_NE(0UL, scrollLayer->elementId());

    WebLayer* rootScrollLayer = getRootScrollLayer();
    EXPECT_TRUE(rootScrollLayer->compositorMutableProperties() & (CompositorMutableProperty::kScrollLeft | CompositorMutableProperty::kScrollTop));
    EXPECT_FALSE(rootScrollLayer->compositorMutableProperties() & (CompositorMutableProperty::kTransform | CompositorMutableProperty::kOpacity));

    EXPECT_NE(0UL, rootScrollLayer->elementId());
}

TEST_F(CompositorWorkerTest, noProxies)
{
    // This case is identical to compositor-proxy-basic, but no proxies have
    // actually been created.
    registerMockedHttpURLLoad("compositor-proxy-plumbing-no-proxies.html");
    navigateTo(m_baseURL + "compositor-proxy-plumbing-no-proxies.html");

    forceFullCompositingUpdate();

    Document* document = frame()->document();

    Element* tallElement = document->getElementById("tall");
    WebLayer* tallLayer = webLayerFromElement(tallElement);
    EXPECT_TRUE(!tallLayer);

    Element* proxiedElement = document->getElementById("proxied");
    WebLayer* proxiedLayer = webLayerFromElement(proxiedElement);
    EXPECT_TRUE(!proxiedLayer);

    Element* scrollElement = document->getElementById("proxied-scroller");
    WebLayer* scrollLayer = scrollingWebLayerFromElement(scrollElement);
    EXPECT_FALSE(!!scrollLayer->compositorMutableProperties());
    EXPECT_EQ(0UL, scrollLayer->elementId());

    WebLayer* rootScrollLayer = getRootScrollLayer();
    EXPECT_FALSE(!!rootScrollLayer->compositorMutableProperties());
    EXPECT_EQ(0UL, rootScrollLayer->elementId());
}

TEST_F(CompositorWorkerTest, disconnectedProxies)
{
    // This case is identical to compositor-proxy-basic, but the proxies are
    // disconnected (the result should be the same as compositor-proxy-plumbing-no-proxies).
    registerMockedHttpURLLoad("compositor-proxy-basic-disconnected.html");
    navigateTo(m_baseURL + "compositor-proxy-basic-disconnected.html");

    forceFullCompositingUpdate();

    Document* document = frame()->document();

    Element* tallElement = document->getElementById("tall");
    WebLayer* tallLayer = webLayerFromElement(tallElement);
    EXPECT_TRUE(!tallLayer);

    Element* proxiedElement = document->getElementById("proxied");
    WebLayer* proxiedLayer = webLayerFromElement(proxiedElement);
    EXPECT_TRUE(!proxiedLayer);

    Element* scrollElement = document->getElementById("proxied-scroller");
    WebLayer* scrollLayer = scrollingWebLayerFromElement(scrollElement);
    EXPECT_FALSE(!!scrollLayer->compositorMutableProperties());
    EXPECT_EQ(0UL, scrollLayer->elementId());

    WebLayer* rootScrollLayer = getRootScrollLayer();
    EXPECT_FALSE(!!rootScrollLayer->compositorMutableProperties());
    EXPECT_EQ(0UL, rootScrollLayer->elementId());
}

} // namespace blink
