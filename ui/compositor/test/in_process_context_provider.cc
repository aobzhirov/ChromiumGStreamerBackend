// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/compositor/test/in_process_context_provider.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "cc/output/managed_memory_policy.h"
#include "gpu/command_buffer/client/gl_in_process_context.h"
#include "gpu/command_buffer/client/gles2_implementation.h"
#include "gpu/command_buffer/client/gles2_lib.h"
#include "gpu/skia_bindings/gl_bindings_skia_cmd_buffer.h"
#include "third_party/skia/include/gpu/GrContext.h"
#include "third_party/skia/include/gpu/gl/GrGLInterface.h"

namespace ui {

// static
scoped_refptr<InProcessContextProvider> InProcessContextProvider::Create(
    const gpu::gles2::ContextCreationAttribHelper& attribs,
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    gpu::ImageFactory* image_factory,
    gfx::AcceleratedWidget window,
    const std::string& debug_name) {
  return new InProcessContextProvider(
      attribs, gpu_memory_buffer_manager, image_factory, window, debug_name);
}

// static
scoped_refptr<InProcessContextProvider>
InProcessContextProvider::CreateOffscreen(
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    gpu::ImageFactory* image_factory) {
  gpu::gles2::ContextCreationAttribHelper attribs;
  attribs.alpha_size = 8;
  attribs.blue_size = 8;
  attribs.green_size = 8;
  attribs.red_size = 8;
  attribs.depth_size = 0;
  attribs.stencil_size = 8;
  attribs.samples = 0;
  attribs.sample_buffers = 0;
  attribs.fail_if_major_perf_caveat = false;
  attribs.bind_generates_resource = false;
  return new InProcessContextProvider(attribs, gpu_memory_buffer_manager,
                                      image_factory,
                                      gfx::kNullAcceleratedWidget, "Offscreen");
}

InProcessContextProvider::InProcessContextProvider(
    const gpu::gles2::ContextCreationAttribHelper& attribs,
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    gpu::ImageFactory* image_factory,
    gfx::AcceleratedWidget window,
    const std::string& debug_name)
    : attribs_(attribs),
      gpu_memory_buffer_manager_(gpu_memory_buffer_manager),
      image_factory_(image_factory),
      window_(window),
      debug_name_(debug_name) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  context_thread_checker_.DetachFromThread();
}

InProcessContextProvider::~InProcessContextProvider() {
  DCHECK(main_thread_checker_.CalledOnValidThread() ||
         context_thread_checker_.CalledOnValidThread());
}

bool InProcessContextProvider::BindToCurrentThread() {
  // This is called on the thread the context will be used.
  DCHECK(context_thread_checker_.CalledOnValidThread());

  if (!context_) {
    gfx::GpuPreference gpu_preference = gfx::PreferDiscreteGpu;
    context_.reset(gpu::GLInProcessContext::Create(
        nullptr,                           /* service */
        nullptr,                           /* surface */
        !window_,                          /* is_offscreen */
        window_, gfx::Size(1, 1), nullptr, /* share_context */
        true,                              /* share_resources */
        attribs_, gpu_preference, gpu::GLInProcessContextSharedMemoryLimits(),
        gpu_memory_buffer_manager_, image_factory_));

    if (!context_)
      return false;

    context_->SetContextLostCallback(base::Bind(
        &InProcessContextProvider::OnLostContext, base::Unretained(this)));
  }

  capabilities_.gpu = context_->GetImplementation()->capabilities();

  std::string unique_context_name =
      base::StringPrintf("%s-%p", debug_name_.c_str(), context_.get());
  context_->GetImplementation()->TraceBeginCHROMIUM(
      "gpu_toplevel", unique_context_name.c_str());

  return true;
}

void InProcessContextProvider::DetachFromThread() {
  context_thread_checker_.DetachFromThread();
}

cc::ContextProvider::Capabilities
InProcessContextProvider::ContextCapabilities() {
  DCHECK(context_thread_checker_.CalledOnValidThread());
  return capabilities_;
}

gpu::gles2::GLES2Interface* InProcessContextProvider::ContextGL() {
  DCHECK(context_thread_checker_.CalledOnValidThread());

  return context_->GetImplementation();
}

gpu::ContextSupport* InProcessContextProvider::ContextSupport() {
  DCHECK(context_thread_checker_.CalledOnValidThread());

  return context_->GetImplementation();
}

class GrContext* InProcessContextProvider::GrContext() {
  DCHECK(context_thread_checker_.CalledOnValidThread());

  if (gr_context_)
    return gr_context_.get();

  skia::RefPtr<GrGLInterface> interface = skia::AdoptRef(new GrGLInterface);
  skia_bindings::InitGLES2InterfaceBindings(interface.get(), ContextGL());

  gr_context_ = skia::AdoptRef(GrContext::Create(
      kOpenGL_GrBackend, reinterpret_cast<GrBackendContext>(interface.get())));

  return gr_context_.get();
}

void InProcessContextProvider::InvalidateGrContext(uint32_t state) {
  DCHECK(context_thread_checker_.CalledOnValidThread());

  if (gr_context_)
    gr_context_.get()->resetContext(state);
}

void InProcessContextProvider::SetupLock() {
}

base::Lock* InProcessContextProvider::GetLock() {
  return &context_lock_;
}

void InProcessContextProvider::DeleteCachedResources() {
  DCHECK(context_thread_checker_.CalledOnValidThread());

  if (gr_context_) {
    TRACE_EVENT_INSTANT0("gpu", "GrContext::freeGpuResources",
                         TRACE_EVENT_SCOPE_THREAD);
    gr_context_->freeGpuResources();
  }
}

void InProcessContextProvider::SetLostContextCallback(
    const LostContextCallback& lost_context_callback) {
  lost_context_callback_ = lost_context_callback;
}

void InProcessContextProvider::OnLostContext() {
  DCHECK(context_thread_checker_.CalledOnValidThread());
  if (!lost_context_callback_.is_null())
    base::ResetAndReturn(&lost_context_callback_).Run();
  if (gr_context_)
    gr_context_->abandonContext();
}

}  // namespace ui
