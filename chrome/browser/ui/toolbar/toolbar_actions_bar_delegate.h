// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_ACTIONS_BAR_DELEGATE_H_
#define CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_ACTIONS_BAR_DELEGATE_H_

#include <stddef.h>

#include "base/memory/scoped_ptr.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/size.h"

class ToolbarActionViewController;
class ToolbarActionsBarBubbleDelegate;

namespace extensions {
class ExtensionMessageBubbleController;
}

// The delegate class (which, in production, represents the view) of the
// ToolbarActionsBar.
class ToolbarActionsBarDelegate {
 public:
  enum GetWidthTime {
    GET_WIDTH_CURRENT,          // Returns the current width.
    GET_WIDTH_AFTER_ANIMATION,  // Returns the width after a running animation.
  };

  virtual ~ToolbarActionsBarDelegate() {}

  // Adds a view for the given |action| at |index|.
  virtual void AddViewForAction(ToolbarActionViewController* action,
                                size_t index) = 0;

  // Removes the view for the given |action|.
  virtual void RemoveViewForAction(ToolbarActionViewController* action) = 0;

  // Removes all action views.
  virtual void RemoveAllViews() = 0;

  // Redraws the view for the toolbar actions bar. |order_changed| indicates
  // whether or not the change caused a reordering of the actions.
  virtual void Redraw(bool order_changed) = 0;

  // Resizes the view to the |target_width| and animates with the given
  // |tween_type|. |suppress_chevron| indicates if the chevron should not be
  // shown during the animation.
  virtual void ResizeAndAnimate(gfx::Tween::Type tween_type,
                                int target_width,
                                bool suppress_chevron) = 0;

  // Sets the overflow chevron's visibility.
  virtual void SetChevronVisibility(bool chevron_visible) = 0;

  // Returns the width of the view according to |get_width_time|.
  virtual int GetWidth(GetWidthTime get_width_time) const = 0;

  // Returns true if the view is animating.
  virtual bool IsAnimating() const = 0;

  // Stops the current animation (width remains where it currently is).
  virtual void StopAnimating() = 0;

  // Returns the width (including padding) for the overflow chevron.
  virtual int GetChevronWidth() const = 0;

  // Displays the bubble for the passed ExtensionMessageBubbleController,
  // anchored to |anchor_action|. If |anchor_action| is null, it should be
  // anchored to the hotdog menu.
  virtual void ShowExtensionMessageBubble(
      scoped_ptr<extensions::ExtensionMessageBubbleController> controller,
      ToolbarActionViewController* anchor_action) = 0;

  // Shows the given |bubble| if no other bubbles are showing.
  virtual void ShowToolbarActionBubble(
      scoped_ptr<ToolbarActionsBarBubbleDelegate> bubble) = 0;
};

#endif  // CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_ACTIONS_BAR_DELEGATE_H_
