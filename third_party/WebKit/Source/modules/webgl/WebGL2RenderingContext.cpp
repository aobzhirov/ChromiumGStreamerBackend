// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/webgl/WebGL2RenderingContext.h"

#include "bindings/modules/v8/UnionTypesModules.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/FrameLoaderClient.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "modules/webgl/CHROMIUMSubscribeUniform.h"
#include "modules/webgl/EXTColorBufferFloat.h"
#include "modules/webgl/EXTDisjointTimerQuery.h"
#include "modules/webgl/EXTTextureFilterAnisotropic.h"
#include "modules/webgl/OESTextureFloatLinear.h"
#include "modules/webgl/WebGLCompressedTextureASTC.h"
#include "modules/webgl/WebGLCompressedTextureATC.h"
#include "modules/webgl/WebGLCompressedTextureETC1.h"
#include "modules/webgl/WebGLCompressedTexturePVRTC.h"
#include "modules/webgl/WebGLCompressedTextureS3TC.h"
#include "modules/webgl/WebGLContextAttributeHelpers.h"
#include "modules/webgl/WebGLContextEvent.h"
#include "modules/webgl/WebGLDebugRendererInfo.h"
#include "modules/webgl/WebGLDebugShaders.h"
#include "modules/webgl/WebGLLoseContext.h"
#include "platform/graphics/gpu/DrawingBuffer.h"
#include "public/platform/Platform.h"
#include "public/platform/WebGraphicsContext3DProvider.h"

namespace blink {

RawPtr<CanvasRenderingContext> WebGL2RenderingContext::Factory::create(HTMLCanvasElement* canvas, const CanvasContextCreationAttributes& attrs, Document&)
{
    if (!RuntimeEnabledFeatures::unsafeES3APIsEnabled()) {
        canvas->dispatchEvent(WebGLContextEvent::create(EventTypeNames::webglcontextcreationerror, false, true, "Creation of WebGL2 contexts disabled."));
        return nullptr;
    }

    WebGLContextAttributes attributes = toWebGLContextAttributes(attrs);
    OwnPtr<WebGraphicsContext3DProvider> contextProvider(createWebGraphicsContext3DProvider(canvas, attributes, 2));
    if (!contextProvider)
        return nullptr;
    gpu::gles2::GLES2Interface* gl = contextProvider->contextGL();
    OwnPtr<Extensions3DUtil> extensionsUtil = Extensions3DUtil::create(gl);
    if (!extensionsUtil)
        return nullptr;
    if (extensionsUtil->supportsExtension("GL_EXT_debug_marker")) {
        String contextLabel(String::format("WebGL2RenderingContext-%p", contextProvider.get()));
        gl->PushGroupMarkerEXT(0, contextLabel.ascii().data());
    }

    RawPtr<WebGL2RenderingContext> renderingContext = new WebGL2RenderingContext(canvas, contextProvider.release(), attributes);

    if (!renderingContext->drawingBuffer()) {
        canvas->dispatchEvent(WebGLContextEvent::create(EventTypeNames::webglcontextcreationerror, false, true, "Could not create a WebGL2 context."));
        return nullptr;
    }

    renderingContext->initializeNewContext();
    renderingContext->registerContextExtensions();

    return renderingContext.release();
}

void WebGL2RenderingContext::Factory::onError(HTMLCanvasElement* canvas, const String& error)
{
    canvas->dispatchEvent(WebGLContextEvent::create(EventTypeNames::webglcontextcreationerror, false, true, error));
}

WebGL2RenderingContext::WebGL2RenderingContext(HTMLCanvasElement* passedCanvas, PassOwnPtr<WebGraphicsContext3DProvider> contextProvider, const WebGLContextAttributes& requestedAttributes)
    : WebGL2RenderingContextBase(passedCanvas, contextProvider, requestedAttributes)
{
}

WebGL2RenderingContext::~WebGL2RenderingContext()
{

}

void WebGL2RenderingContext::setCanvasGetContextResult(RenderingContext& result)
{
    result.setWebGL2RenderingContext(RawPtr<WebGL2RenderingContext>(this));
}

void WebGL2RenderingContext::registerContextExtensions()
{
    // Register extensions.
    registerExtension<CHROMIUMSubscribeUniform>(m_chromiumSubscribeUniform);
    registerExtension<EXTColorBufferFloat>(m_extColorBufferFloat, DraftExtension);
    registerExtension<EXTDisjointTimerQuery>(m_extDisjointTimerQuery);
    registerExtension<EXTTextureFilterAnisotropic>(m_extTextureFilterAnisotropic);
    registerExtension<OESTextureFloatLinear>(m_oesTextureFloatLinear);
    registerExtension<WebGLCompressedTextureASTC>(m_webglCompressedTextureASTC);
    registerExtension<WebGLCompressedTextureATC>(m_webglCompressedTextureATC);
    registerExtension<WebGLCompressedTextureETC1>(m_webglCompressedTextureETC1);
    registerExtension<WebGLCompressedTexturePVRTC>(m_webglCompressedTexturePVRTC);
    registerExtension<WebGLCompressedTextureS3TC>(m_webglCompressedTextureS3TC);
    registerExtension<WebGLDebugRendererInfo>(m_webglDebugRendererInfo);
    registerExtension<WebGLDebugShaders>(m_webglDebugShaders);
    registerExtension<WebGLLoseContext>(m_webglLoseContext);
}

DEFINE_TRACE(WebGL2RenderingContext)
{
    visitor->trace(m_chromiumSubscribeUniform);
    visitor->trace(m_extColorBufferFloat);
    visitor->trace(m_extDisjointTimerQuery);
    visitor->trace(m_extTextureFilterAnisotropic);
    visitor->trace(m_oesTextureFloatLinear);
    visitor->trace(m_webglCompressedTextureASTC);
    visitor->trace(m_webglCompressedTextureATC);
    visitor->trace(m_webglCompressedTextureETC1);
    visitor->trace(m_webglCompressedTexturePVRTC);
    visitor->trace(m_webglCompressedTextureS3TC);
    visitor->trace(m_webglDebugRendererInfo);
    visitor->trace(m_webglDebugShaders);
    visitor->trace(m_webglLoseContext);
    WebGL2RenderingContextBase::trace(visitor);
}

} // namespace blink
