// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_BLINK_WEBGRAPHICSCONTEXT3D_IN_PROCESS_COMMAND_BUFFER_IMPL_H_
#define GPU_BLINK_WEBGRAPHICSCONTEXT3D_IN_PROCESS_COMMAND_BUFFER_IMPL_H_

#include <stddef.h>

#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "gpu/blink/gpu_blink_export.h"
#include "gpu/blink/webgraphicscontext3d_impl.h"
#include "gpu/command_buffer/client/gl_in_process_context.h"
#include "third_party/WebKit/public/platform/WebGraphicsContext3D.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "ui/gfx/native_widget_types.h"

namespace gpu {
class ContextSupport;
class GLInProcessContext;

namespace gles2 {
class GLES2Interface;
class GLES2Implementation;
struct ContextCreationAttribHelper;
}
}

namespace gpu_blink {

class GPU_BLINK_EXPORT WebGraphicsContext3DInProcessCommandBufferImpl
    : public WebGraphicsContext3DImpl {
 public:
  enum MappedMemoryReclaimLimit {
    kNoLimit = 0,
  };

  static scoped_ptr<WebGraphicsContext3DInProcessCommandBufferImpl>
  CreateOffscreenContext(
      const gpu::gles2::ContextCreationAttribHelper& attributes,
      bool share_resources);

  static scoped_ptr<WebGraphicsContext3DInProcessCommandBufferImpl> WrapContext(
      scoped_ptr<::gpu::GLInProcessContext> context,
      const gpu::gles2::ContextCreationAttribHelper& attributes);

  ~WebGraphicsContext3DInProcessCommandBufferImpl() override;

  size_t GetMappedMemoryLimit();

  bool InitializeOnCurrentThread();
  void SetLock(base::Lock* lock);

  ::gpu::ContextSupport* GetContextSupport();

  ::gpu::gles2::GLES2Implementation* GetImplementation() {
    return real_gl_;
  }

 private:
  WebGraphicsContext3DInProcessCommandBufferImpl(
      scoped_ptr<::gpu::GLInProcessContext> context,
      const gpu::gles2::ContextCreationAttribHelper& attributes,
      bool share_resources,
      bool is_offscreen,
      gfx::AcceleratedWidget window);

  void OnContextLost();

  bool MaybeInitializeGL();

  // Used to try to find bugs in code that calls gl directly through the gl api
  // instead of going through WebGraphicsContext3D.
  void ClearContext();

  ::gpu::gles2::ContextCreationAttribHelper attributes_;
  bool share_resources_;

  bool is_offscreen_;
  // Only used when not offscreen.
  gfx::AcceleratedWidget window_;

  // The context we use for OpenGL rendering.
  scoped_ptr< ::gpu::GLInProcessContext> context_;
  // The GLES2Implementation we use for OpenGL rendering.
  ::gpu::gles2::GLES2Implementation* real_gl_;
};

}  // namespace gpu_blink

#endif  // GPU_BLINK_WEBGRAPHICSCONTEXT3D_IN_PROCESS_COMMAND_BUFFER_IMPL_H_
