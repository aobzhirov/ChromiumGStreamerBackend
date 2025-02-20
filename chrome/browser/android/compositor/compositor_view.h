// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_COMPOSITOR_COMPOSITOR_VIEW_H_
#define CHROME_BROWSER_ANDROID_COMPOSITOR_COMPOSITOR_VIEW_H_

#include <jni.h>

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "base/containers/scoped_ptr_hash_map.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "cc/layers/layer_collections.h"
#include "cc/resources/ui_resource_client.h"
#include "content/public/browser/android/compositor_client.h"
#include "content/public/browser/browser_child_process_observer.h"
#include "third_party/skia/include/core/SkColor.h"

class DecorationBackground;
class DecorationCounter;
class SkBitmap;
class TabLayerContainer;

namespace cc {
class Layer;
class SolidColorLayer;
class TextureLayer;
class UIResourceBitmap;
}

namespace gfx {
class JavaBitmap;
}

namespace content {
class Compositor;
}

namespace ui {
class WindowAndroid;
class ResourceManager;
class UIResourceProvider;
}

namespace chrome {
namespace android {

class LayerTitleCache;
class SceneLayer;
class TabContentManager;
class ToolbarLayer;

class CompositorView : public content::CompositorClient,
                       public content::BrowserChildProcessObserver {
 public:
  CompositorView(JNIEnv* env,
                 jobject obj,
                 jint empty_background_color,
                 jboolean low_mem_device,
                 ui::WindowAndroid* window_android,
                 LayerTitleCache* layer_title_cache,
                 TabContentManager* tab_content_manager);

  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& object);

  ui::ResourceManager* GetResourceManager();
  base::android::ScopedJavaLocalRef<jobject> GetResourceManager(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jobj);
  void SetNeedsComposite(JNIEnv* env,
                         const base::android::JavaParamRef<jobject>& object);
  void FinalizeLayers(JNIEnv* env,
                      const base::android::JavaParamRef<jobject>& jobj);
  void SetLayoutViewport(JNIEnv* env,
                         const base::android::JavaParamRef<jobject>& object,
                         jfloat x,
                         jfloat y,
                         jfloat width,
                         jfloat height,
                         jfloat visible_x_offset,
                         jfloat visible_y_offset,
                         jfloat dp_to_pixel);
  void UpdateToolbarLayer(JNIEnv* env,
                          const base::android::JavaParamRef<jobject>& object,
                          jint toolbar_resource_id,
                          jint toolbar_background_color,
                          jint url_bar_resource_id,
                          jfloat url_bar_alpha,
                          jfloat top_offset,
                          jfloat brightness,
                          bool visible,
                          bool show_shadow);
  void UpdateProgressBar(JNIEnv* env,
                         const base::android::JavaParamRef<jobject>& object,
                         jint progress_bar_x,
                         jint progress_bar_y,
                         jint progress_bar_width,
                         jint progress_bar_height,
                         jint progress_bar_color,
                         jint progress_bar_background_x,
                         jint progress_bar_background_y,
                         jint progress_bar_background_width,
                         jint progress_bar_background_height,
                         jint progress_bar_background_color);

  void SurfaceCreated(JNIEnv* env,
                      const base::android::JavaParamRef<jobject>& object);
  void SurfaceDestroyed(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& object);
  void SurfaceChanged(JNIEnv* env,
                      const base::android::JavaParamRef<jobject>& object,
                      jint format,
                      jint width,
                      jint height,
                      const base::android::JavaParamRef<jobject>& surface);

  void SetOverlayVideoMode(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& object,
                           bool enabled);
  void SetSceneLayer(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& object,
                     const base::android::JavaParamRef<jobject>& jscene_layer);

  // CompositorClient implementation:
  void UpdateLayerTreeHost() override;
  void OnSwapBuffersCompleted(int pending_swap_buffers) override;
  ui::UIResourceProvider* GetUIResourceProvider();

 private:
  ~CompositorView() override;

  // content::BrowserChildProcessObserver implementation:
  void BrowserChildProcessHostDisconnected(
      const content::ChildProcessData& data) override;
  void BrowserChildProcessCrashed(const content::ChildProcessData& data,
                                  int exit_code) override;

  void SetBackground(bool visible, SkColor color);

  base::android::ScopedJavaGlobalRef<jobject> obj_;
  scoped_ptr<content::Compositor> compositor_;
  LayerTitleCache* layer_title_cache_;
  TabContentManager* tab_content_manager_;

  scoped_refptr<cc::SolidColorLayer> root_layer_;
  scoped_refptr<ToolbarLayer> toolbar_layer_;
  SceneLayer* scene_layer_;
  scoped_refptr<cc::Layer> scene_layer_layer_;

  int current_surface_format_;
  int content_width_;
  int content_height_;
  bool overlay_video_mode_;
  SkColor empty_background_color_;

  base::WeakPtrFactory<CompositorView> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(CompositorView);
};

bool RegisterCompositorView(JNIEnv* env);

}  // namespace android
}  // namespace chrome

#endif  // CHROME_BROWSER_ANDROID_COMPOSITOR_COMPOSITOR_VIEW_H_
