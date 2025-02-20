// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_NATIVE_DESKTOP_MEDIA_LIST_H_
#define CHROME_BROWSER_MEDIA_NATIVE_DESKTOP_MEDIA_LIST_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner.h"
#include "chrome/browser/media/desktop_media_list_base.h"
#include "content/public/browser/desktop_media_id.h"
#include "ui/gfx/image/image.h"

namespace webrtc {
class ScreenCapturer;
class WindowCapturer;
}

// Implementation of DesktopMediaList that shows native screens and
// native windows.
class NativeDesktopMediaList : public DesktopMediaListBase {
 public:
  // Caller may pass NULL for either of the arguments in case when only some
  // types of sources the model should be populated with (e.g. it will only
  // contain windows, if |screen_capturer| is NULL).
  NativeDesktopMediaList(
      scoped_ptr<webrtc::ScreenCapturer> screen_capturer,
      scoped_ptr<webrtc::WindowCapturer> window_capturer);
  ~NativeDesktopMediaList() override;

 private:
  typedef std::map<content::DesktopMediaID, uint32_t> ImageHashesMap;

  class Worker;
  friend class Worker;

  // Refresh() posts a task for the |worker_| to update list of windows, get
  // thumbnails and schedule next refresh.
  void Refresh() override;

  void RefreshForAuraWindows(std::vector<SourceDescription> sources);
  void UpdateNativeThumbnailsFinished();

#if defined(USE_AURA)
  void CaptureAuraWindowThumbnail(const content::DesktopMediaID& id);
  void OnAuraThumbnailCaptured(const content::DesktopMediaID& id,
                               const gfx::Image& image);
#endif

  // Task runner used for the |worker_|.
  scoped_refptr<base::SequencedTaskRunner> capture_task_runner_;

  // An object that does all the work of getting list of sources on a background
  // thread (see |capture_task_runner_|). Destroyed on |capture_task_runner_|
  // after the model is destroyed.
  scoped_ptr<Worker> worker_;

#if defined(USE_AURA)
  // previous_aura_thumbnail_hashes_ holds thumbanil hash values of aura windows
  // in the previous refresh. While new_aura_thumbnail_hashes_ has hash values
  // of the ongoing refresh. Those two maps are used to detect new thumbnails
  // and changed thumbnails from the previous refresh.
  ImageHashesMap previous_aura_thumbnail_hashes_;
  ImageHashesMap new_aura_thumbnail_hashes_;

  int pending_aura_capture_requests_ = 0;
  bool pending_native_thumbnail_capture_ = true;
#endif

  base::WeakPtrFactory<NativeDesktopMediaList> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(NativeDesktopMediaList);
};

#endif  // CHROME_BROWSER_MEDIA_NATIVE_DESKTOP_MEDIA_LIST_H_
