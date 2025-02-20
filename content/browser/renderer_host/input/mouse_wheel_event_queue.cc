// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/input/mouse_wheel_event_queue.h"

#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/trace_event/trace_event.h"

using blink::WebInputEvent;
using blink::WebMouseWheelEvent;
using ui::LatencyInfo;

namespace content {

// This class represents a single queued mouse wheel event. Its main use
// is that it is reported via trace events.
class QueuedWebMouseWheelEvent : public MouseWheelEventWithLatencyInfo {
 public:
  QueuedWebMouseWheelEvent(const MouseWheelEventWithLatencyInfo& original_event)
      : MouseWheelEventWithLatencyInfo(original_event) {
    TRACE_EVENT_ASYNC_BEGIN0("input", "MouseWheelEventQueue::QueueEvent", this);
  }

  ~QueuedWebMouseWheelEvent() {
    TRACE_EVENT_ASYNC_END0("input", "MouseWheelEventQueue::QueueEvent", this);
  }

 private:
  bool original_can_scroll_;
  DISALLOW_COPY_AND_ASSIGN(QueuedWebMouseWheelEvent);
};

MouseWheelEventQueue::MouseWheelEventQueue(MouseWheelEventQueueClient* client,
                                           bool send_gestures,
                                           int64_t scroll_transaction_ms)
    : client_(client),
      needs_scroll_begin_(true),
      needs_scroll_end_(false),
      send_gestures_(send_gestures),
      scroll_transaction_ms_(scroll_transaction_ms),
      scrolling_device_(blink::WebGestureDeviceUninitialized) {
  DCHECK(client);
}

MouseWheelEventQueue::~MouseWheelEventQueue() {
  if (!wheel_queue_.empty())
    STLDeleteElements(&wheel_queue_);
}

void MouseWheelEventQueue::QueueEvent(
    const MouseWheelEventWithLatencyInfo& event) {
  TRACE_EVENT0("input", "MouseWheelEventQueue::QueueEvent");

  if (event_sent_for_gesture_ack_ && !wheel_queue_.empty()) {
    QueuedWebMouseWheelEvent* last_event = wheel_queue_.back();
    if (last_event->CanCoalesceWith(event)) {
      last_event->CoalesceWith(event);
      TRACE_EVENT_INSTANT2("input", "MouseWheelEventQueue::CoalescedWheelEvent",
                           TRACE_EVENT_SCOPE_THREAD, "total_dx",
                           last_event->event.deltaX, "total_dy",
                           last_event->event.deltaY);
      return;
    }
  }

  wheel_queue_.push_back(new QueuedWebMouseWheelEvent(event));
  TryForwardNextEventToRenderer();
  LOCAL_HISTOGRAM_COUNTS_100("Renderer.WheelQueueSize", wheel_queue_.size());
}

void MouseWheelEventQueue::ProcessMouseWheelAck(
    InputEventAckState ack_result,
    const LatencyInfo& latency_info) {
  TRACE_EVENT0("input", "MouseWheelEventQueue::ProcessMouseWheelAck");
  if (!event_sent_for_gesture_ack_)
    return;

  event_sent_for_gesture_ack_->latency.AddNewLatencyFrom(latency_info);
  client_->OnMouseWheelEventAck(*event_sent_for_gesture_ack_, ack_result);

  // If event wasn't consumed then generate a gesture scroll for it.
  if (send_gestures_ && ack_result != INPUT_EVENT_ACK_STATE_CONSUMED &&
      event_sent_for_gesture_ack_->event.canScroll &&
      (scrolling_device_ == blink::WebGestureDeviceUninitialized ||
       scrolling_device_ == blink::WebGestureDeviceTouchpad)) {
    GestureEventWithLatencyInfo scroll_update;
    scroll_update.event.timeStampSeconds =
        event_sent_for_gesture_ack_->event.timeStampSeconds;

    scroll_update.event.x = event_sent_for_gesture_ack_->event.x;
    scroll_update.event.y = event_sent_for_gesture_ack_->event.y;
    scroll_update.event.globalX = event_sent_for_gesture_ack_->event.globalX;
    scroll_update.event.globalY = event_sent_for_gesture_ack_->event.globalY;
    scroll_update.event.type = WebInputEvent::GestureScrollUpdate;
    scroll_update.event.sourceDevice = blink::WebGestureDeviceTouchpad;
    scroll_update.event.resendingPluginId = -1;
    scroll_update.event.data.scrollUpdate.deltaX =
        event_sent_for_gesture_ack_->event.deltaX;
    scroll_update.event.data.scrollUpdate.deltaY =
        event_sent_for_gesture_ack_->event.deltaY;
    // Only OSX populates the momentumPhase; so expect this to
    // always be PhaseNone on all other platforms.
    scroll_update.event.data.scrollUpdate.inertial =
        event_sent_for_gesture_ack_->event.momentumPhase !=
        blink::WebMouseWheelEvent::PhaseNone;
    if (event_sent_for_gesture_ack_->event.scrollByPage) {
      scroll_update.event.data.scrollUpdate.deltaUnits =
          blink::WebGestureEvent::Page;

      // Turn page scrolls into a *single* page scroll because
      // the magnitude the number of ticks is lost when coalescing.
      if (scroll_update.event.data.scrollUpdate.deltaX)
        scroll_update.event.data.scrollUpdate.deltaX =
            scroll_update.event.data.scrollUpdate.deltaX > 0 ? 1 : -1;
      if (scroll_update.event.data.scrollUpdate.deltaY)
        scroll_update.event.data.scrollUpdate.deltaY =
            scroll_update.event.data.scrollUpdate.deltaY > 0 ? 1 : -1;
    } else {
      scroll_update.event.data.scrollUpdate.deltaUnits =
          event_sent_for_gesture_ack_->event.hasPreciseScrollingDeltas
              ? blink::WebGestureEvent::PrecisePixels
              : blink::WebGestureEvent::Pixels;

      if (event_sent_for_gesture_ack_->event.railsMode ==
          WebInputEvent::RailsModeVertical)
        scroll_update.event.data.scrollUpdate.deltaX = 0;
      if (event_sent_for_gesture_ack_->event.railsMode ==
          WebInputEvent::RailsModeHorizontal)
        scroll_update.event.data.scrollUpdate.deltaY = 0;
    }

    bool current_phase_ended = false;
    bool has_phase_info = false;

    if (event_sent_for_gesture_ack_->event.phase !=
            blink::WebMouseWheelEvent::PhaseNone ||
        event_sent_for_gesture_ack_->event.momentumPhase !=
            blink::WebMouseWheelEvent::PhaseNone) {
      has_phase_info = true;
      current_phase_ended = event_sent_for_gesture_ack_->event.phase ==
                                blink::WebMouseWheelEvent::PhaseEnded ||
                            event_sent_for_gesture_ack_->event.phase ==
                                blink::WebMouseWheelEvent::PhaseCancelled ||
                            event_sent_for_gesture_ack_->event.momentumPhase ==
                                blink::WebMouseWheelEvent::PhaseEnded ||
                            event_sent_for_gesture_ack_->event.momentumPhase ==
                                blink::WebMouseWheelEvent::PhaseCancelled;
    }

    bool needs_update = scroll_update.event.data.scrollUpdate.deltaX != 0 ||
                        scroll_update.event.data.scrollUpdate.deltaY != 0;

    // If there is no update to send and the current phase is ended yet a GSB
    // needs to be sent, this event sequence doesn't need to be generated
    // because the events generated will be a GSB (non-synthetic) and GSE
    // (non-synthetic). This situation arises when OSX generates double
    // phase end information.
    bool empty_sequence =
        !needs_update && needs_scroll_begin_ && current_phase_ended;

    if (needs_update || !empty_sequence) {
      if (needs_scroll_begin_) {
        // If no GSB has been sent, it will be a non-synthetic GSB.
        SendScrollBegin(scroll_update, false);
      } else if (has_phase_info) {
        // If a GSB has been sent, generate a synthetic GSB if we have phase
        // information. This should be removed once crbug.com/526463 is fully
        // implemented.
        SendScrollBegin(scroll_update, true);
      }

      if (needs_update)
        client_->SendGestureEvent(scroll_update);

      if (current_phase_ended) {
        // Non-synthetic GSEs are sent when the current phase is canceled or
        // ended.
        SendScrollEnd(scroll_update.event, false);
      } else if (has_phase_info) {
        // Generate a synthetic GSE for every update to force hit testing so
        // that the non-latching behavior is preserved. Remove once
        // crbug.com/526463 is fully implemented.
        SendScrollEnd(scroll_update.event, true);
      } else {
        scroll_end_timer_.Start(
            FROM_HERE,
            base::TimeDelta::FromMilliseconds(scroll_transaction_ms_),
            base::Bind(&MouseWheelEventQueue::SendScrollEnd,
                       base::Unretained(this), scroll_update.event, false));
      }
    }
  }

  event_sent_for_gesture_ack_.reset();
  TryForwardNextEventToRenderer();
}

void MouseWheelEventQueue::OnGestureScrollEvent(
    const GestureEventWithLatencyInfo& gesture_event) {
  if (gesture_event.event.type == blink::WebInputEvent::GestureScrollBegin) {
    // If there is a current scroll going on and a new scroll that isn't
    // wheel based cancel current one by sending a ScrollEnd.
    if (scroll_end_timer_.IsRunning() &&
        gesture_event.event.sourceDevice != blink::WebGestureDeviceTouchpad) {
      base::Closure task = scroll_end_timer_.user_task();
      scroll_end_timer_.Reset();
      task.Run();
    }
    scrolling_device_ = gesture_event.event.sourceDevice;
  } else if (scrolling_device_ == gesture_event.event.sourceDevice &&
             (gesture_event.event.type ==
                  blink::WebInputEvent::GestureScrollEnd ||
              gesture_event.event.type ==
                  blink::WebInputEvent::GestureFlingStart)) {
    scrolling_device_ = blink::WebGestureDeviceUninitialized;
  }
}

void MouseWheelEventQueue::TryForwardNextEventToRenderer() {
  TRACE_EVENT0("input", "MouseWheelEventQueue::TryForwardNextEventToRenderer");

  if (wheel_queue_.empty() || event_sent_for_gesture_ack_)
    return;

  event_sent_for_gesture_ack_.reset(wheel_queue_.front());
  wheel_queue_.pop_front();

  client_->SendMouseWheelEventImmediately(*event_sent_for_gesture_ack_);
}

void MouseWheelEventQueue::SendScrollEnd(blink::WebGestureEvent update_event,
                                         bool synthetic) {
  DCHECK((synthetic && !needs_scroll_end_) || needs_scroll_end_);

  GestureEventWithLatencyInfo scroll_end(update_event);
  scroll_end.event.timeStampSeconds =
      (base::TimeTicks::Now() - base::TimeTicks()).InSecondsF();
  scroll_end.event.type = WebInputEvent::GestureScrollEnd;
  scroll_end.event.resendingPluginId = -1;
  scroll_end.event.data.scrollEnd.synthetic = synthetic;
  scroll_end.event.data.scrollEnd.inertial =
      update_event.data.scrollUpdate.inertial;
  scroll_end.event.data.scrollEnd.deltaUnits =
      update_event.data.scrollUpdate.deltaUnits;

  if (!synthetic) {
    needs_scroll_begin_ = true;
    needs_scroll_end_ = false;

    if (scroll_end_timer_.IsRunning())
      scroll_end_timer_.Reset();
  }
  client_->SendGestureEvent(scroll_end);
}

void MouseWheelEventQueue::SendScrollBegin(
    const GestureEventWithLatencyInfo& gesture_update,
    bool synthetic) {
  DCHECK((synthetic && !needs_scroll_begin_) || needs_scroll_begin_);

  GestureEventWithLatencyInfo scroll_begin(gesture_update);
  scroll_begin.event.type = WebInputEvent::GestureScrollBegin;
  scroll_begin.event.data.scrollBegin.synthetic = synthetic;
  scroll_begin.event.data.scrollBegin.inertial =
      gesture_update.event.data.scrollUpdate.inertial;
  scroll_begin.event.data.scrollBegin.deltaXHint =
      gesture_update.event.data.scrollUpdate.deltaX;
  scroll_begin.event.data.scrollBegin.deltaYHint =
      gesture_update.event.data.scrollUpdate.deltaY;
  scroll_begin.event.data.scrollBegin.targetViewport = false;
  scroll_begin.event.data.scrollBegin.deltaHintUnits =
      gesture_update.event.data.scrollUpdate.deltaUnits;

  needs_scroll_begin_ = false;
  needs_scroll_end_ = true;
  client_->SendGestureEvent(scroll_begin);
}

}  // namespace content
