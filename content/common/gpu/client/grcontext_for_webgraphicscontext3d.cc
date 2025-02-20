// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/client/grcontext_for_webgraphicscontext3d.h"

#include <stddef.h>
#include <string.h>
#include <utility>

#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/trace_event/trace_event.h"
#include "gpu/blink/webgraphicscontext3d_impl.h"
#include "gpu/command_buffer/client/gles2_lib.h"
#include "gpu/skia_bindings/gl_bindings_skia_cmd_buffer.h"
#include "third_party/skia/include/gpu/GrContext.h"

using gpu_blink::WebGraphicsContext3DImpl;

namespace content {

GrContextForWebGraphicsContext3D::GrContextForWebGraphicsContext3D(
    skia::RefPtr<GrGLInterfaceForWebGraphicsContext3D> gr_interface) {
  if (!gr_interface || !gr_interface->WebContext3D())
    return;

  skia_bindings::InitGLES2InterfaceBindings(
      gr_interface.get(), gr_interface->WebContext3D()->GetGLInterface());

  gr_context_ = skia::AdoptRef(GrContext::Create(
      kOpenGL_GrBackend,
      reinterpret_cast<GrBackendContext>(gr_interface.get())));
  if (gr_context_) {
    // The limit of the number of GPU resources we hold in the GrContext's
    // GPU cache.
    static const int kMaxGaneshResourceCacheCount = 2048;
    // The limit of the bytes allocated toward GPU resources in the GrContext's
    // GPU cache.
    static const size_t kMaxGaneshResourceCacheBytes = 96 * 1024 * 1024;

    gr_context_->setResourceCacheLimits(kMaxGaneshResourceCacheCount,
                                        kMaxGaneshResourceCacheBytes);
  }
}

GrContextForWebGraphicsContext3D::~GrContextForWebGraphicsContext3D() {
}

void GrContextForWebGraphicsContext3D::OnLostContext() {
  if (gr_context_)
    gr_context_->abandonContext();
}

void GrContextForWebGraphicsContext3D::FreeGpuResources() {
  if (gr_context_) {
    TRACE_EVENT_INSTANT0("gpu", "GrContext::freeGpuResources", \
        TRACE_EVENT_SCOPE_THREAD);
    gr_context_->freeGpuResources();
  }
}

GrGLInterfaceForWebGraphicsContext3D::GrGLInterfaceForWebGraphicsContext3D(
    scoped_ptr<gpu_blink::WebGraphicsContext3DImpl> context3d)
    : context3d_(std::move(context3d)) {}

void GrGLInterfaceForWebGraphicsContext3D::BindToCurrentThread() {
  context_thread_checker_.DetachFromThread();
}

GrGLInterfaceForWebGraphicsContext3D::~GrGLInterfaceForWebGraphicsContext3D() {
  DCHECK(context_thread_checker_.CalledOnValidThread());
#if !defined(NDEBUG)
  // Set all the function pointers to zero, in order to crash if function
  // pointers are used after free.
  memset(&fFunctions, 0, sizeof(GrGLInterface::Functions));
#endif
}

}  // namespace content
