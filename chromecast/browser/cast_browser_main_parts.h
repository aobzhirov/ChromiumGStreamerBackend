// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_BROWSER_CAST_BROWSER_MAIN_PARTS_H_
#define CHROMECAST_BROWSER_CAST_BROWSER_MAIN_PARTS_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/common/main_function_params.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace net {
class NetLog;
}

namespace chromecast {

namespace media {
class MediaResourceTracker;
class VideoPlaneController;
}  // namespace media

namespace shell {
class CastBrowserProcess;
class URLRequestContextFactory;

class CastBrowserMainParts : public content::BrowserMainParts {
 public:
  // This class does not take ownership of |url_request_content_factory|.
  CastBrowserMainParts(const content::MainFunctionParams& parameters,
                       URLRequestContextFactory* url_request_context_factory);
  ~CastBrowserMainParts() override;

  scoped_refptr<base::SingleThreadTaskRunner> GetMediaTaskRunner() const;
#if !defined(OS_ANDROID)
  media::MediaResourceTracker* media_resource_tracker();
#endif

  // content::BrowserMainParts implementation:
  void PreMainMessageLoopStart() override;
  void PostMainMessageLoopStart() override;
  void ToolkitInitialized() override;
  int PreCreateThreads() override;
  void PreMainMessageLoopRun() override;
  bool MainMessageLoopRun(int* result_code) override;
  void PostMainMessageLoopRun() override;
  void PostDestroyThreads() override;

 private:
  scoped_ptr<CastBrowserProcess> cast_browser_process_;
  const content::MainFunctionParams parameters_;  // For running browser tests.
  URLRequestContextFactory* const url_request_context_factory_;
  scoped_ptr<net::NetLog> net_log_;
  scoped_ptr<media::VideoPlaneController> video_plane_controller_;

#if !defined(OS_ANDROID)
  // Tracks usage of media resource by e.g. CMA pipeline, CDM.
  media::MediaResourceTracker* media_resource_tracker_;
#endif

  DISALLOW_COPY_AND_ASSIGN(CastBrowserMainParts);
};

}  // namespace shell
}  // namespace chromecast

#endif  // CHROMECAST_BROWSER_CAST_BROWSER_MAIN_PARTS_H_
