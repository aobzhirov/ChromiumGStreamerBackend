// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_ANIMATION_INK_DROP_ANIMATION_H_
#define UI_VIEWS_ANIMATION_INK_DROP_ANIMATION_H_

#include "base/macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/animation/ink_drop_animation_observer.h"
#include "ui/views/animation/ink_drop_state.h"
#include "ui/views/views_export.h"

namespace ui {
class CallbackLayerAnimationObserver;
class Layer;
class LayerAnimationObserver;
}  // namespace ui

namespace views {

// Simple base class for animations that provide visual feedback for View state.
// Manages the attached InkDropAnimationObservers.
//
// TODO(bruthig): Document the ink drop ripple on chromium.org and add a link to
// the doc here.
class VIEWS_EXPORT InkDropAnimation {
 public:
  // TODO(bruthig): Remove UseFastAnimations() and kSlowAnimationDurationFactor.
  // See http://crbug.com/584681

  // Checks CommandLine switches to determine if the visual feedback should have
  // a fast animations speed.
  static bool UseFastAnimations();

  // The factor at which to increase the animation durations if
  // UseFastAnimations() returns true.
  static const double kSlowAnimationDurationFactor;

  // The opacity of the ink drop when it is not visible.
  static const float kHiddenOpacity;

  // The opacity of the ink drop when it is visible.
  static const float kVisibleOpacity;

  InkDropAnimation();
  virtual ~InkDropAnimation();

  void set_observer(InkDropAnimationObserver* observer) {
    observer_ = observer;
  }

  // Animates from the current InkDropState to the new |ink_drop_state|.
  //
  // NOTE: GetTargetInkDropState() should return the new |ink_drop_state| value
  // to any observers being notified as a result of the call.
  void AnimateToState(InkDropState ink_drop_state);

  InkDropState target_ink_drop_state() const { return target_ink_drop_state_; }

  // Immediately aborts all in-progress animations and hides the ink drop.
  //
  // NOTE: This will NOT raise InkDropAnimation(Started|Ended) events for the
  // state transition to HIDDEN!
  void HideImmediately();

  // Immediately snaps the ink drop to the ACTIVATED target state. All pending
  // animations are aborted. Events will be raised for the pending animations
  // as well as the transition to the ACTIVATED state.
  virtual void SnapToActivated();

  // The root Layer that can be added in to a Layer tree.
  virtual ui::Layer* GetRootLayer() = 0;

  // Returns true when the ripple is visible. This is different from checking if
  // the ink_drop_state() == HIDDEN because the ripple may be visible while it
  // animates to the target HIDDEN state.
  virtual bool IsVisible() const = 0;

 protected:
  // Animates the ripple from the |old_ink_drop_state| to the
  // |new_ink_drop_state|. |observer| is added to all LayerAnimationSequence's
  // used if not null.
  virtual void AnimateStateChange(InkDropState old_ink_drop_state,
                                  InkDropState new_ink_drop_state,
                                  ui::LayerAnimationObserver* observer) = 0;

  // Updates the transforms, opacity, and visibility to a HIDDEN state.
  virtual void SetStateToHidden() = 0;

  virtual void AbortAllAnimations() = 0;

 private:
  // The Callback invoked when all of the animation sequences for the specific
  // |ink_drop_state| animation have started. |observer| is the
  // ui::CallbackLayerAnimationObserver which is notifying the callback.
  void AnimationStartedCallback(
      InkDropState ink_drop_state,
      const ui::CallbackLayerAnimationObserver& observer);

  // The Callback invoked when all of the animation sequences for the specific
  // |ink_drop_state| animation have finished. |observer| is the
  // ui::CallbackLayerAnimationObserver which is notifying the callback.
  bool AnimationEndedCallback(
      InkDropState ink_drop_state,
      const ui::CallbackLayerAnimationObserver& observer);

  // The target InkDropState.
  InkDropState target_ink_drop_state_;

  InkDropAnimationObserver* observer_;

  DISALLOW_COPY_AND_ASSIGN(InkDropAnimation);
};

}  // namespace views

#endif  // UI_VIEWS_ANIMATION_INK_DROP_ANIMATION_H_
