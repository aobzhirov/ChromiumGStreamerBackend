// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/extended_mouse_warp_controller.h"

#include <cmath>

#include "ash/display/display_manager.h"
#include "ash/display/display_util.h"
#include "ash/display/shared_display_edge_indicator.h"
#include "ash/display/window_tree_host_manager.h"
#include "ash/root_window_controller.h"
#include "ash/screen_util.h"
#include "ash/shell.h"
#include "ui/aura/window.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/screen.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace ash {

namespace {

// Maximum size on the display edge that initiate snapping phantom window,
// from the corner of the display.
const int kMaximumSnapHeight = 16;

// Minimum height of an indicator on the display edge that allows
// dragging a window.  If two displays shares the edge smaller than
// this, entire edge will be used as a draggable space.
const int kMinimumIndicatorHeight = 200;

// Helper method that maps a gfx::Display to an aura::Window.
aura::Window* GetRootWindowForDisplayId(int64_t display_id) {
  return Shell::GetInstance()
      ->window_tree_host_manager()
      ->GetRootWindowForDisplayId(display_id);
}

// Helper method that maps an aura::Window to display id;
int64_t GetDisplayIdFromWindow(aura::Window* window) {
  return gfx::Screen::GetScreen()->GetDisplayNearestWindow(window).id();
}

// Adjust the edge so that it has |barrier_size| gap at the top to
// trigger snap window action.
void AdjustSourceEdgeBounds(const gfx::Rect& display_bounds,
                            int barrier_size,
                            gfx::Rect* edge) {
  DCHECK_GT(edge->height(), edge->width());
  int target_y = display_bounds.y() + barrier_size;
  if (target_y < edge->y())
    return;

  int available_height = edge->height() - kMinimumIndicatorHeight;
  if (available_height <= 0)
    return;
  edge->Inset(0, std::min(available_height, target_y - edge->y()), 0, 0);
}

}  // namespace

ExtendedMouseWarpController::WarpRegion::WarpRegion(
    int64_t a_display_id,
    int64_t b_display_id,
    const gfx::Rect& a_indicator_bounds,
    const gfx::Rect& b_indicator_bounds)
    : a_display_id_(a_display_id),
      b_display_id_(b_display_id),
      a_indicator_bounds_(a_indicator_bounds),
      b_indicator_bounds_(b_indicator_bounds),
      shared_display_edge_indicator_(nullptr) {
  // Initialize edge bounds from indicator bounds.
  aura::Window* a_window = GetRootWindowForDisplayId(a_display_id);
  aura::Window* b_window = GetRootWindowForDisplayId(b_display_id);

  AshWindowTreeHost* a_ash_host = GetRootWindowController(a_window)->ash_host();
  AshWindowTreeHost* b_ash_host = GetRootWindowController(b_window)->ash_host();

  a_edge_bounds_in_native_ =
      GetNativeEdgeBounds(a_ash_host, a_indicator_bounds);
  b_edge_bounds_in_native_ =
      GetNativeEdgeBounds(b_ash_host, b_indicator_bounds);
}

ExtendedMouseWarpController::WarpRegion::~WarpRegion() {}

const gfx::Rect&
ExtendedMouseWarpController::WarpRegion::GetIndicatorBoundsForTest(
    int64_t id) const {
  if (a_display_id_ == id)
    return a_indicator_bounds_;
  CHECK_EQ(b_display_id_, id);
  return b_indicator_bounds_;
}

ExtendedMouseWarpController::ExtendedMouseWarpController(
    aura::Window* drag_source)
    : drag_source_root_(drag_source),
      allow_non_native_event_(false) {
  ash::DisplayManager* display_manager =
      Shell::GetInstance()->display_manager();
  int64_t drag_source_id = drag_source ? GetDisplayIdFromWindow(drag_source)
                                       : gfx::Display::kInvalidDisplayID;
  DisplayList display_list = display_manager->active_display_list();
  // Try to create a Warp region for all possible two displays combination.
  // The following code does it by poping the last element in the list
  // and then pairing with remaining displays in the list, until the list
  // becomes single element.
  while (display_list.size() > 1) {
    gfx::Display display = display_list.back();
    display_list.pop_back();
    for (const gfx::Display& peer : display_list) {
      scoped_ptr<WarpRegion> region =
          CreateWarpRegion(display, peer, drag_source_id);
      if (region)
        AddWarpRegion(std::move(region), drag_source != nullptr);
    }
  }
}

ExtendedMouseWarpController::~ExtendedMouseWarpController() {
}

bool ExtendedMouseWarpController::WarpMouseCursor(ui::MouseEvent* event) {
  if (gfx::Screen::GetScreen()->GetNumDisplays() <= 1 || !enabled_)
    return false;

  aura::Window* target = static_cast<aura::Window*>(event->target());
  gfx::Point point_in_screen = event->location();
  ::wm::ConvertPointToScreen(target, &point_in_screen);

  // A native event may not exist in unit test. Generate the native point
  // from the screen point instead.
  if (!event->HasNativeEvent()) {
    if (!allow_non_native_event_)
      return false;
    aura::Window* target_root = target->GetRootWindow();
    gfx::Point point_in_native = point_in_screen;
    ::wm::ConvertPointFromScreen(target_root, &point_in_native);
    target_root->GetHost()->ConvertPointToNativeScreen(&point_in_native);
    return WarpMouseCursorInNativeCoords(point_in_native, point_in_screen,
                                         true);
  }

  gfx::Point point_in_native =
      ui::EventSystemLocationFromNative(event->native_event());

#if defined(USE_OZONE)
  // TODO(dnicoara): crbug.com/415680 Move cursor warping into Ozone once Ozone
  // has access to the logical display layout.
  // Native events in Ozone are in the native window coordinate system. We need
  // to translate them to get the global position.
  point_in_native.Offset(target->GetHost()->GetBounds().x(),
                         target->GetHost()->GetBounds().y());
#endif

  return WarpMouseCursorInNativeCoords(point_in_native, point_in_screen, false);
}

void ExtendedMouseWarpController::SetEnabled(bool enabled) {
  enabled_ = enabled;
}

void ExtendedMouseWarpController::AddWarpRegion(
    scoped_ptr<WarpRegion> warp_region,
    bool has_drag_source) {
  if (has_drag_source) {
    warp_region->shared_display_edge_indicator_.reset(
        new SharedDisplayEdgeIndicator);
    warp_region->shared_display_edge_indicator_->Show(
        warp_region->a_indicator_bounds_, warp_region->b_indicator_bounds_);
  }

  warp_regions_.emplace_back(std::move(warp_region));
}

bool ExtendedMouseWarpController::WarpMouseCursorInNativeCoords(
    const gfx::Point& point_in_native,
    const gfx::Point& point_in_screen,
    bool update_mouse_location_now) {
  for (const scoped_ptr<WarpRegion>& warp : warp_regions_) {
    bool in_a_edge = warp->a_edge_bounds_in_native_.Contains(point_in_native);
    bool in_b_edge = warp->b_edge_bounds_in_native_.Contains(point_in_native);
    if (!in_a_edge && !in_b_edge)
      continue;

    // The mouse must move.
    aura::Window* dst_window = GetRootWindowForDisplayId(
        in_a_edge ? warp->b_display_id_ : warp->a_display_id_);
    AshWindowTreeHost* target_ash_host =
        GetRootWindowController(dst_window)->ash_host();

    MoveCursorTo(target_ash_host, point_in_screen, update_mouse_location_now);
    return true;
  }

  return false;
}

scoped_ptr<ExtendedMouseWarpController::WarpRegion>
ExtendedMouseWarpController::CreateWarpRegion(const gfx::Display& a,
                                              const gfx::Display& b,
                                              int64_t drag_source_id) {
  gfx::Rect a_edge;
  gfx::Rect b_edge;
  int snap_barrier = drag_source_id == gfx::Display::kInvalidDisplayID
                         ? 0
                         : kMaximumSnapHeight;

  if (!ComputeBoundary(a, b, &a_edge, &b_edge))
    return nullptr;

  // Creates the snap window barrirer only when horizontally connected.
  if (a_edge.height() > a_edge.width()) {
    if (drag_source_id == a.id())
      AdjustSourceEdgeBounds(a.bounds(), snap_barrier, &a_edge);
    else if (drag_source_id == b.id())
      AdjustSourceEdgeBounds(b.bounds(), snap_barrier, &b_edge);
  }

  return make_scoped_ptr(new WarpRegion(a.id(), b.id(), a_edge, b_edge));
}

}  // namespace ash
