// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_TOAST_TOAST_OVERLAY_H_
#define ASH_SYSTEM_TOAST_TOAST_OVERLAY_H_

#include "ash/ash_export.h"
#include "base/memory/scoped_ptr.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"

namespace gfx {
class Rect;
}

namespace views {
class Widget;
}

namespace ash {

class ToastManagerTest;
class ToastOverlayView;
class ToastOverlayButton;

class ASH_EXPORT ToastOverlay : public ui::LayerAnimationObserver {
 public:
  class ASH_EXPORT Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnClosed() = 0;
  };

  ToastOverlay(Delegate* delegate, const std::string& text);
  ~ToastOverlay() override;

  // Shows or hides the overlay.
  void Show(bool visible);

 private:
  friend class ToastManagerTest;

  // Returns the current bounds of the overlay, which is based on visibility.
  gfx::Rect CalculateOverlayBounds();

  // gfx::LayerAnimationObserver overrides:
  void OnLayerAnimationEnded(ui::LayerAnimationSequence* sequence) override;
  void OnLayerAnimationAborted(ui::LayerAnimationSequence* sequence) override;
  void OnLayerAnimationScheduled(ui::LayerAnimationSequence* sequence) override;

  views::Widget* widget_for_testing();
  void ClickDismissButtonForTesting(const ui::Event& event);

  bool is_visible_ = false;
  Delegate* const delegate_;
  const std::string text_;
  scoped_ptr<views::Widget> overlay_widget_;
  scoped_ptr<ToastOverlayView> overlay_view_;
  gfx::Size widget_size_;

  DISALLOW_COPY_AND_ASSIGN(ToastOverlay);
};

}  // namespace ash

#endif  // ASH_SYSTEM_TOAST_TOAST_OVERLAY_H_
