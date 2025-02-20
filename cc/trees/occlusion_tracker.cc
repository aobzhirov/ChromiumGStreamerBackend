// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/trees/occlusion_tracker.h"

#include <stddef.h>

#include <algorithm>

#include "cc/base/math_util.h"
#include "cc/base/region.h"
#include "cc/layers/layer.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/render_surface_impl.h"
#include "cc/trees/layer_tree_impl.h"
#include "ui/gfx/geometry/quad_f.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace cc {

OcclusionTracker::OcclusionTracker(const gfx::Rect& screen_space_clip_rect)
    : screen_space_clip_rect_(screen_space_clip_rect) {
}

OcclusionTracker::~OcclusionTracker() {
}

Occlusion OcclusionTracker::GetCurrentOcclusionForLayer(
    const gfx::Transform& draw_transform) const {
  DCHECK(!stack_.empty());
  const StackObject& back = stack_.back();
  return Occlusion(draw_transform,
                   back.occlusion_from_outside_target,
                   back.occlusion_from_inside_target);
}

Occlusion OcclusionTracker::GetCurrentOcclusionForContributingSurface(
    const gfx::Transform& draw_transform) const {
  DCHECK(!stack_.empty());
  if (stack_.size() < 2)
    return Occlusion();
  // A contributing surface doesn't get occluded by things inside its own
  // surface, so only things outside the surface can occlude it. That occlusion
  // is found just below the top of the stack (if it exists).
  const StackObject& second_last = stack_[stack_.size() - 2];
  return Occlusion(draw_transform, second_last.occlusion_from_outside_target,
                   second_last.occlusion_from_inside_target);
}

void OcclusionTracker::EnterLayer(const LayerIteratorPosition& layer_iterator) {
  LayerImpl* render_target = layer_iterator.target_render_surface_layer;

  if (layer_iterator.represents_itself)
    EnterRenderTarget(render_target);
  else if (layer_iterator.represents_target_render_surface)
    FinishedRenderTarget(render_target);
}

void OcclusionTracker::LeaveLayer(const LayerIteratorPosition& layer_iterator) {
  LayerImpl* render_target = layer_iterator.target_render_surface_layer;

  if (layer_iterator.represents_itself)
    MarkOccludedBehindLayer(layer_iterator.current_layer);
  // TODO(danakj): This should be done when entering the contributing surface,
  // but in a way that the surface's own occlusion won't occlude itself.
  else if (layer_iterator.represents_contributing_render_surface)
    LeaveToRenderTarget(render_target);
}

static gfx::Rect ScreenSpaceClipRectInTargetSurface(
    const RenderSurfaceImpl* target_surface,
    const gfx::Rect& screen_space_clip_rect) {
  gfx::Transform inverse_screen_space_transform(
      gfx::Transform::kSkipInitialization);
  if (!target_surface->screen_space_transform().GetInverse(
          &inverse_screen_space_transform))
    return target_surface->content_rect();

  return MathUtil::ProjectEnclosingClippedRect(inverse_screen_space_transform,
                                               screen_space_clip_rect);
}

static SimpleEnclosedRegion TransformSurfaceOpaqueRegion(
    const SimpleEnclosedRegion& region,
    bool have_clip_rect,
    const gfx::Rect& clip_rect_in_new_target,
    const gfx::Transform& transform) {
  if (region.IsEmpty())
    return region;

  // Verify that rects within the |surface| will remain rects in its target
  // surface after applying |transform|. If this is true, then apply |transform|
  // to each rect within |region| in order to transform the entire Region.

  // TODO(danakj): Find a rect interior to each transformed quad.
  if (!transform.Preserves2dAxisAlignment())
    return SimpleEnclosedRegion();

  SimpleEnclosedRegion transformed_region;
  for (size_t i = 0; i < region.GetRegionComplexity(); ++i) {
    gfx::Rect transformed_rect =
        MathUtil::MapEnclosedRectWith2dAxisAlignedTransform(transform,
                                                            region.GetRect(i));
    if (have_clip_rect)
      transformed_rect.Intersect(clip_rect_in_new_target);
    transformed_region.Union(transformed_rect);
  }
  return transformed_region;
}

void OcclusionTracker::EnterRenderTarget(const LayerImpl* new_target) {
  if (!stack_.empty() && stack_.back().target == new_target)
    return;

  const LayerImpl* old_target = NULL;
  const RenderSurfaceImpl* old_occlusion_immune_ancestor = NULL;
  if (!stack_.empty()) {
    old_target = stack_.back().target;
    old_occlusion_immune_ancestor =
        old_target->render_surface()->nearest_occlusion_immune_ancestor();
  }
  const RenderSurfaceImpl* new_occlusion_immune_ancestor =
      new_target->render_surface()->nearest_occlusion_immune_ancestor();

  stack_.push_back(StackObject(new_target));

  // We copy the screen occlusion into the new RenderSurfaceImpl subtree, but we
  // never copy in the occlusion from inside the target, since we are looking
  // at a new RenderSurfaceImpl target.

  // If entering an unoccluded subtree, do not carry forward the outside
  // occlusion calculated so far.
  bool entering_unoccluded_subtree =
      new_occlusion_immune_ancestor &&
      new_occlusion_immune_ancestor != old_occlusion_immune_ancestor;

  gfx::Transform inverse_new_target_screen_space_transform(
      // Note carefully, not used if screen space transform is uninvertible.
      gfx::Transform::kSkipInitialization);
  bool have_transform_from_screen_to_new_target =
      new_target->render_surface()->screen_space_transform().GetInverse(
          &inverse_new_target_screen_space_transform);

  bool entering_root_target =
      new_target->layer_tree_impl()->IsRootLayer(new_target);

  bool copy_outside_occlusion_forward =
      stack_.size() > 1 &&
      !entering_unoccluded_subtree &&
      have_transform_from_screen_to_new_target &&
      !entering_root_target;
  if (!copy_outside_occlusion_forward)
    return;

  size_t last_index = stack_.size() - 1;
  gfx::Transform old_target_to_new_target_transform(
      inverse_new_target_screen_space_transform,
      old_target->render_surface()->screen_space_transform());
  stack_[last_index].occlusion_from_outside_target =
      TransformSurfaceOpaqueRegion(
          stack_[last_index - 1].occlusion_from_outside_target, false,
          gfx::Rect(), old_target_to_new_target_transform);
  stack_[last_index].occlusion_from_outside_target.Union(
      TransformSurfaceOpaqueRegion(
          stack_[last_index - 1].occlusion_from_inside_target, false,
          gfx::Rect(), old_target_to_new_target_transform));
}

void OcclusionTracker::FinishedRenderTarget(const LayerImpl* finished_target) {
  // Make sure we know about the target surface.
  EnterRenderTarget(finished_target);

  RenderSurfaceImpl* surface = finished_target->render_surface();

  // Readbacks always happen on render targets so we only need to check
  // for readbacks here.
  bool target_is_only_for_copy_request =
      finished_target->HasCopyRequest() && finished_target->IsHidden();

  // If the occlusion within the surface can not be applied to things outside of
  // the surface's subtree, then clear the occlusion here so it won't be used.
  if (finished_target->mask_layer() || surface->draw_opacity() < 1 ||
      !finished_target->uses_default_blend_mode() ||
      target_is_only_for_copy_request ||
      finished_target->filters().HasFilterThatAffectsOpacity()) {
    stack_.back().occlusion_from_outside_target.Clear();
    stack_.back().occlusion_from_inside_target.Clear();
  }
}

static void ReduceOcclusionBelowSurface(
    const LayerImpl* contributing_layer,
    const gfx::Rect& surface_rect,
    const gfx::Transform& surface_transform,
    const LayerImpl* render_target,
    SimpleEnclosedRegion* occlusion_from_inside_target) {
  if (surface_rect.IsEmpty())
    return;

  gfx::Rect affected_area_in_target =
      MathUtil::MapEnclosingClippedRect(surface_transform, surface_rect);
  if (contributing_layer->render_surface()->is_clipped()) {
    affected_area_in_target.Intersect(
        contributing_layer->render_surface()->clip_rect());
  }
  if (affected_area_in_target.IsEmpty())
    return;

  int outset_top, outset_right, outset_bottom, outset_left;
  contributing_layer->background_filters().GetOutsets(
      &outset_top, &outset_right, &outset_bottom, &outset_left);

  // The filter can move pixels from outside of the clip, so allow affected_area
  // to expand outside the clip.
  affected_area_in_target.Inset(
      -outset_left, -outset_top, -outset_right, -outset_bottom);
  SimpleEnclosedRegion affected_occlusion = *occlusion_from_inside_target;
  affected_occlusion.Intersect(affected_area_in_target);

  occlusion_from_inside_target->Subtract(affected_area_in_target);
  for (size_t i = 0; i < affected_occlusion.GetRegionComplexity(); ++i) {
    gfx::Rect occlusion_rect = affected_occlusion.GetRect(i);

    // Shrink the rect by expanding the non-opaque pixels outside the rect.

    // The left outset of the filters moves pixels on the right side of
    // the occlusion_rect into it, shrinking its right edge.
    int shrink_left =
        occlusion_rect.x() == affected_area_in_target.x() ? 0 : outset_right;
    int shrink_top =
        occlusion_rect.y() == affected_area_in_target.y() ? 0 : outset_bottom;
    int shrink_right =
        occlusion_rect.right() == affected_area_in_target.right() ?
        0 : outset_left;
    int shrink_bottom =
        occlusion_rect.bottom() == affected_area_in_target.bottom() ?
        0 : outset_top;

    occlusion_rect.Inset(shrink_left, shrink_top, shrink_right, shrink_bottom);

    occlusion_from_inside_target->Union(occlusion_rect);
  }
}

void OcclusionTracker::LeaveToRenderTarget(const LayerImpl* new_target) {
  DCHECK(!stack_.empty());
  size_t last_index = stack_.size() - 1;
  bool surface_will_be_at_top_after_pop =
      stack_.size() > 1 && stack_[last_index - 1].target == new_target;

  // We merge the screen occlusion from the current RenderSurfaceImpl subtree
  // out to its parent target RenderSurfaceImpl. The target occlusion can be
  // merged out as well but needs to be transformed to the new target.

  const LayerImpl* old_target = stack_[last_index].target;
  const RenderSurfaceImpl* old_surface = old_target->render_surface();

  SimpleEnclosedRegion old_occlusion_from_inside_target_in_new_target =
      TransformSurfaceOpaqueRegion(
          stack_[last_index].occlusion_from_inside_target,
          old_surface->is_clipped(), old_surface->clip_rect(),
          old_surface->draw_transform());
  if (old_target->has_replica() && !old_target->replica_has_mask()) {
    old_occlusion_from_inside_target_in_new_target.Union(
        TransformSurfaceOpaqueRegion(
            stack_[last_index].occlusion_from_inside_target,
            old_surface->is_clipped(), old_surface->clip_rect(),
            old_surface->replica_draw_transform()));
  }

  SimpleEnclosedRegion old_occlusion_from_outside_target_in_new_target =
      TransformSurfaceOpaqueRegion(
          stack_[last_index].occlusion_from_outside_target, false, gfx::Rect(),
          old_surface->draw_transform());

  gfx::Rect unoccluded_surface_rect;
  gfx::Rect unoccluded_replica_rect;
  if (old_target->background_filters().HasFilterThatMovesPixels()) {
    Occlusion surface_occlusion = GetCurrentOcclusionForContributingSurface(
        old_surface->draw_transform());
    unoccluded_surface_rect =
        surface_occlusion.GetUnoccludedContentRect(old_surface->content_rect());
    if (old_target->has_replica()) {
      Occlusion replica_occlusion = GetCurrentOcclusionForContributingSurface(
          old_surface->replica_draw_transform());
      unoccluded_replica_rect = replica_occlusion.GetUnoccludedContentRect(
          old_surface->content_rect());
    }
  }

  if (surface_will_be_at_top_after_pop) {
    // Merge the top of the stack down.
    stack_[last_index - 1].occlusion_from_inside_target.Union(
        old_occlusion_from_inside_target_in_new_target);
    // TODO(danakj): Strictly this should subtract the inside target occlusion
    // before union.
    if (!new_target->layer_tree_impl()->IsRootLayer(new_target)) {
      stack_[last_index - 1].occlusion_from_outside_target.Union(
          old_occlusion_from_outside_target_in_new_target);
    }
    stack_.pop_back();
  } else {
    // Replace the top of the stack with the new pushed surface.
    stack_.back().target = new_target;
    stack_.back().occlusion_from_inside_target =
        old_occlusion_from_inside_target_in_new_target;
    if (!new_target->layer_tree_impl()->IsRootLayer(new_target)) {
      stack_.back().occlusion_from_outside_target =
          old_occlusion_from_outside_target_in_new_target;
    } else {
      stack_.back().occlusion_from_outside_target.Clear();
    }
  }

  if (!old_target->background_filters().HasFilterThatMovesPixels())
    return;

  ReduceOcclusionBelowSurface(old_target,
                              unoccluded_surface_rect,
                              old_surface->draw_transform(),
                              new_target,
                              &stack_.back().occlusion_from_inside_target);
  ReduceOcclusionBelowSurface(old_target,
                              unoccluded_surface_rect,
                              old_surface->draw_transform(),
                              new_target,
                              &stack_.back().occlusion_from_outside_target);

  if (!old_target->has_replica())
    return;
  ReduceOcclusionBelowSurface(old_target,
                              unoccluded_replica_rect,
                              old_surface->replica_draw_transform(),
                              new_target,
                              &stack_.back().occlusion_from_inside_target);
  ReduceOcclusionBelowSurface(old_target,
                              unoccluded_replica_rect,
                              old_surface->replica_draw_transform(),
                              new_target,
                              &stack_.back().occlusion_from_outside_target);
}

void OcclusionTracker::MarkOccludedBehindLayer(const LayerImpl* layer) {
  DCHECK(!stack_.empty());
  DCHECK_EQ(layer->render_target(), stack_.back().target);

  if (layer->draw_opacity() < 1)
    return;

  if (!layer->uses_default_blend_mode())
    return;

  if (layer->Is3dSorted())
    return;

  SimpleEnclosedRegion opaque_layer_region = layer->VisibleOpaqueRegion();
  if (opaque_layer_region.IsEmpty())
    return;

  DCHECK(layer->visible_layer_rect().Contains(opaque_layer_region.bounds()));

  gfx::Transform draw_transform = layer->DrawTransform();
  // TODO(danakj): Find a rect interior to each transformed quad.
  if (!draw_transform.Preserves2dAxisAlignment())
    return;

  gfx::Rect clip_rect_in_target = ScreenSpaceClipRectInTargetSurface(
      layer->render_target()->render_surface(), screen_space_clip_rect_);
  if (layer->is_clipped()) {
    clip_rect_in_target.Intersect(layer->clip_rect());
  } else {
    clip_rect_in_target.Intersect(
        layer->render_target()->render_surface()->content_rect());
  }

  for (size_t i = 0; i < opaque_layer_region.GetRegionComplexity(); ++i) {
    gfx::Rect transformed_rect =
        MathUtil::MapEnclosedRectWith2dAxisAlignedTransform(
            draw_transform, opaque_layer_region.GetRect(i));
    transformed_rect.Intersect(clip_rect_in_target);
    if (transformed_rect.width() < minimum_tracking_size_.width() &&
        transformed_rect.height() < minimum_tracking_size_.height())
      continue;
    stack_.back().occlusion_from_inside_target.Union(transformed_rect);
  }
}

Region OcclusionTracker::ComputeVisibleRegionInScreen() const {
  DCHECK(stack_.back().target->layer_tree_impl()->IsRootLayer(
      stack_.back().target));
  const SimpleEnclosedRegion& occluded =
      stack_.back().occlusion_from_inside_target;
  Region visible_region(screen_space_clip_rect_);
  for (size_t i = 0; i < occluded.GetRegionComplexity(); ++i)
    visible_region.Subtract(occluded.GetRect(i));
  return visible_region;
}

}  // namespace cc
