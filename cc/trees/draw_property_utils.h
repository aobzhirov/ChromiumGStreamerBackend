// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_DRAW_PROPERTY_UTILS_H_
#define CC_TREES_DRAW_PROPERTY_UTILS_H_

#include "cc/base/cc_export.h"
#include "cc/layers/layer_collections.h"

namespace gfx {
class Rect;
class Transform;
class Vector2dF;
}  // namespace gfx

namespace cc {

class ClipTree;
struct DrawProperties;
class Layer;
class LayerImpl;
class RenderSurfaceImpl;
class EffectTree;
class TransformTree;
class PropertyTrees;

namespace draw_property_utils {

// Computes combined clips for every node in |clip_tree|. This function requires
// that |transform_tree| has been updated via |ComputeTransforms|.
void CC_EXPORT ComputeClips(ClipTree* clip_tree,
                            const TransformTree& transform_tree,
                            bool non_root_surfaces_enabled);

// Computes combined (screen space) transforms for every node in the transform
// tree. This must be done prior to calling |ComputeClips|.
void CC_EXPORT ComputeTransforms(TransformTree* transform_tree);

// Computes screen space opacity for every node in the opacity tree.
void CC_EXPORT ComputeEffects(EffectTree* effect_tree);

// Computes the visible content rect for every layer under |root_layer|. The
// visible content rect is the clipped content space rect that will be used for
// recording.
void CC_EXPORT BuildPropertyTreesAndComputeVisibleRects(
    Layer* root_layer,
    const Layer* page_scale_layer,
    const Layer* inner_viewport_scroll_layer,
    const Layer* outer_viewport_scroll_layer,
    const Layer* overscroll_elasticity_layer,
    const gfx::Vector2dF& elastic_overscroll,
    float page_scale_factor,
    float device_scale_factor,
    const gfx::Rect& viewport,
    const gfx::Transform& device_transform,
    bool can_render_to_separate_surface,
    PropertyTrees* property_trees,
    LayerList* update_layer_list);

void CC_EXPORT BuildPropertyTreesAndComputeVisibleRects(
    LayerImpl* root_layer,
    const LayerImpl* page_scale_layer,
    const LayerImpl* inner_viewport_scroll_layer,
    const LayerImpl* outer_viewport_scroll_layer,
    const LayerImpl* overscroll_elasticity_layer,
    const gfx::Vector2dF& elastic_overscroll,
    float page_scale_factor,
    float device_scale_factor,
    const gfx::Rect& viewport,
    const gfx::Transform& device_transform,
    bool can_render_to_separate_surface,
    PropertyTrees* property_trees,
    LayerImplList* visible_layer_list);

void CC_EXPORT ComputeVisibleRects(Layer* root_layer,
                                   PropertyTrees* property_trees,
                                   bool can_render_to_separate_surface,
                                   LayerList* update_layer_list);

void CC_EXPORT ComputeVisibleRects(LayerImpl* root_layer,
                                   PropertyTrees* property_trees,
                                   bool can_render_to_separate_surface,
                                   LayerImplList* visible_layer_list);

void CC_EXPORT ComputeLayerDrawProperties(LayerImpl* layer,
                                          const PropertyTrees* property_trees,
                                          bool layers_always_allowed_lcd_text,
                                          bool can_use_lcd_text);

void CC_EXPORT ComputeSurfaceDrawProperties(const PropertyTrees* property_trees,
                                            RenderSurfaceImpl* render_surface);

gfx::Transform CC_EXPORT DrawTransform(const Layer* layer,
                                       const TransformTree& tree);

gfx::Transform CC_EXPORT DrawTransform(const LayerImpl* layer,
                                       const TransformTree& tree);

gfx::Transform CC_EXPORT ScreenSpaceTransform(const Layer* layer,
                                              const TransformTree& tree);

gfx::Transform CC_EXPORT ScreenSpaceTransform(const LayerImpl* layer,
                                              const TransformTree& tree);

void CC_EXPORT UpdatePageScaleFactor(PropertyTrees* property_trees,
                                     const LayerImpl* page_scale_layer,
                                     float page_scale_factor,
                                     float device_scale_factor,
                                     const gfx::Transform device_transform);

void CC_EXPORT UpdatePageScaleFactor(PropertyTrees* property_trees,
                                     const Layer* page_scale_layer,
                                     float page_scale_factor,
                                     float device_scale_factor,
                                     const gfx::Transform device_transform);

void CC_EXPORT
UpdateElasticOverscroll(PropertyTrees* property_trees,
                        const LayerImpl* overscroll_elasticity_layer,
                        const gfx::Vector2dF& elastic_overscroll);

void CC_EXPORT
UpdateElasticOverscroll(PropertyTrees* property_trees,
                        const Layer* overscroll_elasticity_layer,
                        const gfx::Vector2dF& elastic_overscroll);

}  // namespace draw_property_utils
}  // namespace cc

#endif  // CC_TREES_DRAW_PROPERTY_UTILS_H_
