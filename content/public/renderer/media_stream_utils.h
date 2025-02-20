// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_RENDERER_MEDIA_STREAM_UTILS_H_
#define CONTENT_PUBLIC_RENDERER_MEDIA_STREAM_UTILS_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/common/content_export.h"
#include "media/base/channel_layout.h"
#include "media/base/video_capture_types.h"

namespace blink {
class WebMediaStream;
class WebMediaStreamTrack;
}

namespace media {
class AudioCapturerSource;
class VideoCapturerSource;
}

namespace content {
// These methods create a WebMediaStreamSource + MediaStreamSource pair with the
// provided audio or video capturer source. A new WebMediaStreamTrack +
// MediaStreamTrack pair is created, connected to the source and is plugged into
// the WebMediaStream (|web_media_stream|).
// |is_remote| should be true if the source of the data is not a local device.
// |is_readonly| should be true if the format of the data cannot be changed by
//     MediaTrackConstraints.
CONTENT_EXPORT bool AddVideoTrackToMediaStream(
    scoped_ptr<media::VideoCapturerSource> video_source,
    bool is_remote,
    bool is_readonly,
    blink::WebMediaStream* web_media_stream);

// |sample_rate|, |channel_layout|, and |frames_per_buffer| specify the audio
// parameters of the track. Generally, these should match the |audio_source| so
// that it does not have to perform unnecessary sample rate conversion or
// channel mixing.
CONTENT_EXPORT bool AddAudioTrackToMediaStream(
    scoped_refptr<media::AudioCapturerSource> audio_source,
    int sample_rate,
    media::ChannelLayout channel_layout,
    int frames_per_buffer,
    bool is_remote,
    bool is_readonly,
    blink::WebMediaStream* web_media_stream);

// On success returns pointer to the current format of the given video track;
// returns nullptr on failure (if the argument is invalid or if the format
// cannot be retrieved at the moment).
CONTENT_EXPORT const media::VideoCaptureFormat* GetCurrentVideoTrackFormat(
    const blink::WebMediaStreamTrack& video_track);

}  // namespace content

#endif  // CONTENT_PUBLIC_RENDERER_MEDIA_STREAM_UTILS_H_
