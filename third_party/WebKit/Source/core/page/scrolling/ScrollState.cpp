// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/page/scrolling/ScrollState.h"

#include "core/dom/DOMNodeIds.h"
#include "core/dom/Element.h"
#include "core/dom/ExceptionCode.h"

namespace blink {

namespace {
Element* elementForId(int elementId)
{
    Node* node = DOMNodeIds::nodeForId(elementId);
    ASSERT(node);
    if (!node)
        return nullptr;
    ASSERT(node->isElementNode());
    if (!node->isElementNode())
        return nullptr;
    return static_cast<Element*>(node);
}
} // namespace

RawPtr<ScrollState> ScrollState::create(ScrollStateInit init)
{
    OwnPtr<ScrollStateData> scrollStateData = adoptPtr(new ScrollStateData());
    scrollStateData->delta_x = init.deltaX();
    scrollStateData->delta_y = init.deltaY();
    scrollStateData->start_position_x = init.startPositionX();
    scrollStateData->start_position_y = init.startPositionY();
    scrollStateData->velocity_x = init.velocityX();
    scrollStateData->velocity_y = init.velocityY();
    scrollStateData->is_beginning = init.isBeginning();
    scrollStateData->is_in_inertial_phase = init.isInInertialPhase();
    scrollStateData->is_ending = init.isEnding();
    scrollStateData->should_propagate = init.shouldPropagate();
    scrollStateData->from_user_input = init.fromUserInput();
    scrollStateData->is_direct_manipulation = init.isDirectManipulation();
    scrollStateData->delta_granularity = init.deltaGranularity();
    ScrollState* scrollState = new ScrollState(scrollStateData.release());
    return scrollState;
}

RawPtr<ScrollState> ScrollState::create(PassOwnPtr<ScrollStateData> data)
{
    ScrollState* scrollState = new ScrollState(data);
    return scrollState;
}

ScrollState::ScrollState(PassOwnPtr<ScrollStateData> data)
    : m_data(data)
{
}

void ScrollState::consumeDelta(double x, double y, ExceptionState& exceptionState)
{
    if ((m_data->delta_x > 0 && 0 > x) || (m_data->delta_x < 0 && 0 < x) || (m_data->delta_y > 0 && 0 > y) || (m_data->delta_y < 0 && 0 < y)) {
        exceptionState.throwDOMException(InvalidModificationError, "Can't increase delta using consumeDelta");
        return;
    }
    if (fabs(x) > fabs(m_data->delta_x) || fabs(y) > fabs(m_data->delta_y)) {
        exceptionState.throwDOMException(InvalidModificationError, "Can't change direction of delta using consumeDelta");
        return;
    }
    consumeDeltaNative(x, y);
}

void ScrollState::distributeToScrollChainDescendant()
{
    if (!m_scrollChain.empty()) {
        int descendantId = m_scrollChain.front();
        m_scrollChain.pop_front();
        elementForId(descendantId)->callDistributeScroll(*this);
    }
}

void ScrollState::consumeDeltaNative(double x, double y)
{
    m_data->delta_x -= x;
    m_data->delta_y -= y;

    if (x)
        m_data->caused_scroll_x = true;
    if (y)
        m_data->caused_scroll_y = true;
    if (x || y)
        m_data->delta_consumed_for_scroll_sequence = true;
}

Element* ScrollState::currentNativeScrollingElement() const
{
    uint64_t elementId = m_data->current_native_scrolling_element();
    if (elementId == 0)
        return nullptr;
    return elementForId(elementId);
}

void ScrollState::setCurrentNativeScrollingElement(Element* element)
{
    m_data->set_current_native_scrolling_element(DOMNodeIds::idForNode(element));
}

void ScrollState::setCurrentNativeScrollingElementById(int elementId)
{
    m_data->set_current_native_scrolling_element(elementId);
}

} // namespace blink
