// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_PLUGIN_PEPPER_VIDEO_RENDERER_2D_H_
#define REMOTING_CLIENT_PLUGIN_PEPPER_VIDEO_RENDERER_2D_H_

#include <list>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "ppapi/cpp/graphics_2d.h"
#include "ppapi/cpp/image_data.h"
#include "ppapi/cpp/point.h"
#include "ppapi/cpp/view.h"
#include "ppapi/utility/completion_callback_factory.h"
#include "remoting/client/plugin/pepper_video_renderer.h"
#include "remoting/protocol/frame_consumer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_geometry.h"

namespace base {
class ScopedClosureRunner;
}  // namespace base

namespace webrtc {
class DesktopFrame;
class SharedDesktopFrame;
}  // namespace webrtc

namespace remoting {

class SoftwareVideoRenderer;

// Video renderer that wraps SoftwareVideoRenderer and displays it using Pepper
// 2D graphics API.
class PepperVideoRenderer2D : public PepperVideoRenderer,
                              public protocol::FrameConsumer {
 public:
  PepperVideoRenderer2D();
  ~PepperVideoRenderer2D() override;

  // PepperVideoRenderer interface.
  bool Initialize(pp::Instance* instance,
                  const ClientContext& context,
                  EventHandler* event_handler,
                  protocol::PerformanceTracker* perf_tracker) override;
  void OnViewChanged(const pp::View& view) override;
  void EnableDebugDirtyRegion(bool enable) override;

  // VideoRenderer interface.
  void OnSessionConfig(const protocol::SessionConfig& config) override;
  protocol::VideoStub* GetVideoStub() override;
  protocol::FrameConsumer* GetFrameConsumer() override;

 private:
  // protocol::FrameConsumer implementation.
  scoped_ptr<webrtc::DesktopFrame> AllocateFrame(
      const webrtc::DesktopSize& size) override;
  void DrawFrame(scoped_ptr<webrtc::DesktopFrame> frame,
                 const base::Closure& done) override;
  PixelFormat GetPixelFormat() override;


  void Flush();
  void OnFlushDone(int result);

  // Parameters passed to Initialize().
  pp::Instance* instance_ = nullptr;
  EventHandler* event_handler_ = nullptr;

  pp::Graphics2D graphics2d_;

  scoped_ptr<SoftwareVideoRenderer> software_video_renderer_;

  // View size in output pixels.
  webrtc::DesktopSize view_size_;

  // Size of the most recent source frame in pixels.
  webrtc::DesktopSize source_size_;

  // Done callbacks for the frames that have been painted but not flushed.
  ScopedVector<base::ScopedClosureRunner> pending_frames_done_callbacks_;

  // Done callbacks for the frames that are currently being flushed.
  ScopedVector<base::ScopedClosureRunner> flushing_frames_done_callbacks_;

  // True if there paint operations that need to be flushed.
  bool need_flush_ = false;

  // True if there is already a Flush() pending on the Graphics2D context.
  bool flush_pending_ = false;

  // True after the first call to DrawFrame().
  bool frame_received_ = false;

  // True if dirty regions are to be sent to |event_handler_| for debugging.
  bool debug_dirty_region_ = false;

  base::ThreadChecker thread_checker_;

  pp::CompletionCallbackFactory<PepperVideoRenderer2D> callback_factory_;
  base::WeakPtrFactory<PepperVideoRenderer2D> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PepperVideoRenderer2D);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_PLUGIN_PEPPER_VIDEO_RENDERER_2D_H_
