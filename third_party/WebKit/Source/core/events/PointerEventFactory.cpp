// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/events/PointerEventFactory.h"

namespace blink {

namespace {

inline int toInt(WebPointerProperties::PointerType t) { return static_cast<int>(t); }

const char* pointerTypeNameForWebPointPointerType(WebPointerProperties::PointerType type)
{
    switch (type) {
    case WebPointerProperties::PointerType::Unknown:
        return "";
    case WebPointerProperties::PointerType::Touch:
        return "touch";
    case WebPointerProperties::PointerType::Pen:
        return "pen";
    case WebPointerProperties::PointerType::Mouse:
        return "mouse";
    }
    ASSERT_NOT_REACHED();
    return "";
}

const AtomicString& pointerEventNameForMouseEventName(
    const AtomicString& mouseEventName)
{
#define RETURN_CORRESPONDING_PE_NAME(eventSuffix) \
    if (mouseEventName == EventTypeNames::mouse##eventSuffix) {\
        return EventTypeNames::pointer##eventSuffix;\
    }

    RETURN_CORRESPONDING_PE_NAME(down);
    RETURN_CORRESPONDING_PE_NAME(enter);
    RETURN_CORRESPONDING_PE_NAME(leave);
    RETURN_CORRESPONDING_PE_NAME(move);
    RETURN_CORRESPONDING_PE_NAME(out);
    RETURN_CORRESPONDING_PE_NAME(over);
    RETURN_CORRESPONDING_PE_NAME(up);

#undef RETURN_CORRESPONDING_PE_NAME

    ASSERT_NOT_REACHED();
    return emptyAtom;
}

} // namespace

const int PointerEventFactory::s_invalidId = 0;

// Mouse id is 1 to behave the same as MS Edge for compatibility reasons.
const int PointerEventFactory::s_mouseId = 1;

float getPointerEventPressure(float force, int buttons)
{
    if (std::isnan(force))
        return buttons ? 0.5 : 0;
    return force;
}

void PointerEventFactory::setIdTypeButtons(PointerEventInit &pointerEventInit,
    const WebPointerProperties &pointerProperties, unsigned buttons)
{
    const WebPointerProperties::PointerType pointerType = pointerProperties.pointerType;
    const IncomingId incomingId(pointerType, pointerProperties.id);
    int pointerId = addIdAndActiveButtons(incomingId, buttons != 0);

    pointerEventInit.setButtons(buttons);
    pointerEventInit.setPointerId(pointerId);
    pointerEventInit.setPointerType(pointerTypeNameForWebPointPointerType(pointerType));
    pointerEventInit.setIsPrimary(isPrimary(pointerId));
}

RawPtr<PointerEvent> PointerEventFactory::create(
    const AtomicString& mouseEventName, const PlatformMouseEvent& mouseEvent,
    RawPtr<EventTarget> relatedTarget,
    RawPtr<AbstractView> view)
{
    AtomicString pointerEventName = pointerEventNameForMouseEventName(mouseEventName);
    unsigned buttons = MouseEvent::platformModifiersToButtons(mouseEvent.getModifiers());
    PointerEventInit pointerEventInit;

    setIdTypeButtons(pointerEventInit, mouseEvent.pointerProperties(), buttons);

    pointerEventInit.setScreenX(mouseEvent.globalPosition().x());
    pointerEventInit.setScreenY(mouseEvent.globalPosition().y());
    pointerEventInit.setClientX(mouseEvent.position().x());
    pointerEventInit.setClientY(mouseEvent.position().y());

    if (pointerEventName == EventTypeNames::pointerdown
        || pointerEventName == EventTypeNames::pointerup) {
        pointerEventInit.setButton(mouseEvent.button());
    } else {
        // TODO(crbug.com/587955): We are setting NoButton for transition
        // pointerevents should be resolved as part of this bug
        pointerEventInit.setButton(NoButton);
    }
    pointerEventInit.setPressure(getPointerEventPressure(
        mouseEvent.pointerProperties().force, pointerEventInit.buttons()));

    UIEventWithKeyState::setFromPlatformModifiers(pointerEventInit, mouseEvent.getModifiers());

    // Make sure chorded buttons fire pointermove instead of pointerup/down.
    if ((pointerEventName == EventTypeNames::pointerdown
        && (buttons & ~MouseEvent::buttonToButtons(mouseEvent.button())) != 0)
        || (pointerEventName == EventTypeNames::pointerup && buttons != 0))
        pointerEventName = EventTypeNames::pointermove;

    pointerEventInit.setBubbles(
        pointerEventName != EventTypeNames::pointerenter
        && pointerEventName != EventTypeNames::pointerleave);
    pointerEventInit.setCancelable(
        pointerEventName != EventTypeNames::pointerenter
        && pointerEventName != EventTypeNames::pointerleave
        && pointerEventName != EventTypeNames::pointercancel);

    pointerEventInit.setView(view);
    if (relatedTarget)
        pointerEventInit.setRelatedTarget(relatedTarget);

    return PointerEvent::create(pointerEventName, pointerEventInit);
}

RawPtr<PointerEvent> PointerEventFactory::create(const AtomicString& type,
    const PlatformTouchPoint& touchPoint, PlatformEvent::Modifiers modifiers,
    const double width, const double height,
    const double clientX, const double clientY)
{
    const PlatformTouchPoint::TouchState pointState = touchPoint.state();

    bool pointerReleasedOrCancelled =
        pointState == PlatformTouchPoint::TouchReleased
        || pointState == PlatformTouchPoint::TouchCancelled;
    bool pointerPressedOrReleased =
        pointState == PlatformTouchPoint::TouchPressed
        || pointState == PlatformTouchPoint::TouchReleased;

    bool isEnterOrLeave = false;

    PointerEventInit pointerEventInit;

    setIdTypeButtons(pointerEventInit, touchPoint.pointerProperties(),
        pointerReleasedOrCancelled ? 0 : 1);

    pointerEventInit.setWidth(width);
    pointerEventInit.setHeight(height);
    pointerEventInit.setTiltX(touchPoint.pointerProperties().tiltX);
    pointerEventInit.setTiltY(touchPoint.pointerProperties().tiltY);
    pointerEventInit.setScreenX(touchPoint.screenPos().x());
    pointerEventInit.setScreenY(touchPoint.screenPos().y());
    pointerEventInit.setClientX(clientX);
    pointerEventInit.setClientY(clientY);
    pointerEventInit.setButton(pointerPressedOrReleased ? LeftButton: NoButton);
    pointerEventInit.setPressure(getPointerEventPressure(
        touchPoint.force(), pointerEventInit.buttons()));

    UIEventWithKeyState::setFromPlatformModifiers(pointerEventInit, modifiers);

    pointerEventInit.setBubbles(!isEnterOrLeave);
    pointerEventInit.setCancelable(!isEnterOrLeave && pointState != PlatformTouchPoint::TouchCancelled);

    return PointerEvent::create(type, pointerEventInit);
}


RawPtr<PointerEvent> PointerEventFactory::createPointerCancelEvent(const PlatformTouchPoint& touchPoint)
{
    PointerEventInit pointerEventInit;

    setIdTypeButtons(pointerEventInit, touchPoint.pointerProperties(), 0);

    pointerEventInit.setBubbles(true);
    pointerEventInit.setCancelable(false);

    return PointerEvent::create(EventTypeNames::pointercancel, pointerEventInit);
}

RawPtr<PointerEvent> PointerEventFactory::createPointerCaptureEvent(
    RawPtr<PointerEvent> pointerEvent,
    const AtomicString& type)
{
    ASSERT(type == EventTypeNames::gotpointercapture
        || type == EventTypeNames::lostpointercapture);

    PointerEventInit pointerEventInit;
    pointerEventInit.setPointerId(pointerEvent->pointerId());
    pointerEventInit.setPointerType(pointerEvent->pointerType());
    pointerEventInit.setIsPrimary(pointerEvent->isPrimary());
    pointerEventInit.setBubbles(true);
    pointerEventInit.setCancelable(false);

    return PointerEvent::create(type, pointerEventInit);
}

RawPtr<PointerEvent> PointerEventFactory::createPointerTransitionEvent(
    RawPtr<PointerEvent> pointerEvent,
    const AtomicString& type,
    RawPtr<EventTarget> relatedTarget)
{
    ASSERT(type == EventTypeNames::pointerout
        || type == EventTypeNames::pointerleave
        || type == EventTypeNames::pointerover
        || type == EventTypeNames::pointerenter);

    PointerEventInit pointerEventInit;

    pointerEventInit.setPointerId(pointerEvent->pointerId());
    pointerEventInit.setPointerType(pointerEvent->pointerType());
    pointerEventInit.setIsPrimary(pointerEvent->isPrimary());
    pointerEventInit.setWidth(pointerEvent->width());
    pointerEventInit.setHeight(pointerEvent->height());
    pointerEventInit.setTiltX(pointerEvent->tiltX());
    pointerEventInit.setTiltY(pointerEvent->tiltY());
    pointerEventInit.setScreenX(pointerEvent->screenX());
    pointerEventInit.setScreenY(pointerEvent->screenY());
    pointerEventInit.setClientX(pointerEvent->clientX());
    pointerEventInit.setClientY(pointerEvent->clientY());
    pointerEventInit.setButton(pointerEvent->button());
    pointerEventInit.setButtons(pointerEvent->buttons());
    pointerEventInit.setPressure(pointerEvent->pressure());

    pointerEventInit.setBubbles(type != EventTypeNames::pointerenter
        && type != EventTypeNames::pointerleave);
    pointerEventInit.setCancelable(type != EventTypeNames::pointerenter
        && type != EventTypeNames::pointerleave
        && type != EventTypeNames::pointercancel);
    if (relatedTarget)
        pointerEventInit.setRelatedTarget(relatedTarget);

    return PointerEvent::create(type, pointerEventInit);
}

PointerEventFactory::PointerEventFactory()
{
    clear();
}

PointerEventFactory::~PointerEventFactory()
{
    clear();
}

void PointerEventFactory::clear()
{
    for (int type = 0;
        type <= toInt(WebPointerProperties::PointerType::LastEntry); type++) {
        m_primaryId[type] = PointerEventFactory::s_invalidId;
        m_idCount[type] = 0;
    }
    m_pointerIncomingIdMapping.clear();
    m_pointerIdMapping.clear();

    // Always add mouse pointer in initialization and never remove it.
    // No need to add it to m_pointerIncomingIdMapping as it is not going to be
    // used with the existing APIs
    m_primaryId[toInt(WebPointerProperties::PointerType::Mouse)] = s_mouseId;
    m_pointerIdMapping.add(s_mouseId, PointerAttributes(
        IncomingId(WebPointerProperties::PointerType::Mouse, 0), 0));

    m_currentId = PointerEventFactory::s_mouseId+1;
}

int PointerEventFactory::addIdAndActiveButtons(const IncomingId p,
    bool isActiveButtons)
{
    // Do not add extra mouse pointer as it was added in initialization
    if (p.pointerType() == toInt(WebPointerProperties::PointerType::Mouse)) {
        m_pointerIdMapping.set(s_mouseId, PointerAttributes(p, isActiveButtons));
        return s_mouseId;
    }

    int type = p.pointerType();
    if (m_pointerIncomingIdMapping.contains(p)) {
        int mappedId = m_pointerIncomingIdMapping.get(p);
        m_pointerIdMapping.set(mappedId, PointerAttributes(p, isActiveButtons));
        return mappedId;
    }
    // We do not handle the overflow of m_currentId as it should be very rare
    int mappedId = m_currentId++;
    if (!m_idCount[type])
        m_primaryId[type] = mappedId;
    m_idCount[type]++;
    m_pointerIncomingIdMapping.add(p, mappedId);
    m_pointerIdMapping.add(mappedId, PointerAttributes(p, isActiveButtons));
    return mappedId;
}

bool PointerEventFactory::remove(
    const RawPtr<PointerEvent> pointerEvent)
{
    int mappedId = pointerEvent->pointerId();
    // Do not remove mouse pointer id as it should always be there
    if (mappedId == s_mouseId || !m_pointerIdMapping.contains(mappedId))
        return false;

    IncomingId p = m_pointerIdMapping.get(mappedId).incomingId;
    int type = p.pointerType();
    m_pointerIdMapping.remove(mappedId);
    m_pointerIncomingIdMapping.remove(p);
    if (m_primaryId[type] == mappedId)
        m_primaryId[type] = PointerEventFactory::s_invalidId;
    m_idCount[type]--;
    return true;
}

bool PointerEventFactory::isPrimary(int mappedId) const
{
    if (!m_pointerIdMapping.contains(mappedId))
        return false;

    IncomingId p = m_pointerIdMapping.get(mappedId).incomingId;
    return m_primaryId[p.pointerType()] == mappedId;
}

bool PointerEventFactory::isActive(const int pointerId)
{
    return m_pointerIdMapping.contains(pointerId);
}

bool PointerEventFactory::isActiveButtonsState(const int pointerId)
{
    return m_pointerIdMapping.contains(pointerId)
        && m_pointerIdMapping.get(pointerId).isActiveButtons;
}

} // namespace blink
