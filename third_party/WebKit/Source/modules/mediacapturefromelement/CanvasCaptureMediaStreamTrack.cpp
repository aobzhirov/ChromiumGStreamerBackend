// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/mediacapturefromelement/CanvasCaptureMediaStreamTrack.h"

#include "core/html/HTMLCanvasElement.h"
#include "modules/mediacapturefromelement/AutoCanvasDrawListener.h"
#include "modules/mediacapturefromelement/OnRequestCanvasDrawListener.h"
#include "modules/mediacapturefromelement/TimedCanvasDrawListener.h"
#include "platform/mediastream/MediaStreamCenter.h"

namespace blink {

CanvasCaptureMediaStreamTrack* CanvasCaptureMediaStreamTrack::create(MediaStreamComponent* component, RawPtr<HTMLCanvasElement> element, const PassOwnPtr<WebCanvasCaptureHandler> handler)
{
    return new CanvasCaptureMediaStreamTrack(component, element, handler);
}

CanvasCaptureMediaStreamTrack* CanvasCaptureMediaStreamTrack::create(MediaStreamComponent* component, RawPtr<HTMLCanvasElement> element, const PassOwnPtr<WebCanvasCaptureHandler> handler, double frameRate)
{
    return new CanvasCaptureMediaStreamTrack(component, element, handler, frameRate);
}

HTMLCanvasElement* CanvasCaptureMediaStreamTrack::canvas() const
{
    return m_canvasElement.get();
}

void CanvasCaptureMediaStreamTrack::requestFrame()
{
    m_drawListener->requestFrame();
}

CanvasCaptureMediaStreamTrack* CanvasCaptureMediaStreamTrack::clone(ExecutionContext* context)
{
    MediaStreamComponent* clonedComponent = MediaStreamComponent::create(component()->source());
    CanvasCaptureMediaStreamTrack* clonedTrack = new CanvasCaptureMediaStreamTrack(*this, clonedComponent);
    MediaStreamCenter::instance().didCreateMediaStreamTrack(clonedComponent);
    return clonedTrack;
}

DEFINE_TRACE(CanvasCaptureMediaStreamTrack)
{
    visitor->trace(m_canvasElement);
    visitor->trace(m_drawListener);
    MediaStreamTrack::trace(visitor);
}

CanvasCaptureMediaStreamTrack::CanvasCaptureMediaStreamTrack(const CanvasCaptureMediaStreamTrack& track, MediaStreamComponent* component)
    :MediaStreamTrack(track.m_canvasElement->getExecutionContext(), component)
    , m_canvasElement(track.m_canvasElement)
    , m_drawListener(track.m_drawListener)
{
    suspendIfNeeded();
    m_canvasElement->addListener(m_drawListener.get());
}

CanvasCaptureMediaStreamTrack::CanvasCaptureMediaStreamTrack(MediaStreamComponent* component, RawPtr<HTMLCanvasElement> element, const PassOwnPtr<WebCanvasCaptureHandler> handler)
    : MediaStreamTrack(element->getExecutionContext(), component)
    , m_canvasElement(element)
{
    suspendIfNeeded();
    m_drawListener = AutoCanvasDrawListener::create(handler);
    m_canvasElement->addListener(m_drawListener.get());
}

CanvasCaptureMediaStreamTrack::CanvasCaptureMediaStreamTrack(MediaStreamComponent* component, RawPtr<HTMLCanvasElement> element, const PassOwnPtr<WebCanvasCaptureHandler> handler, double frameRate)
    : MediaStreamTrack(element->getExecutionContext(), component)
    , m_canvasElement(element)
{
    suspendIfNeeded();
    if (frameRate == 0) {
        m_drawListener = OnRequestCanvasDrawListener::create(handler);
    } else {
        m_drawListener = TimedCanvasDrawListener::create(handler, frameRate);
    }
    m_canvasElement->addListener(m_drawListener.get());
}

} // namespace blink
