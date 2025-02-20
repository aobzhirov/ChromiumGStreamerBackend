// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CODEC_VIDEO_DECODER_VPX_H_
#define REMOTING_CODEC_VIDEO_DECODER_VPX_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "remoting/codec/scoped_vpx_codec.h"
#include "remoting/codec/video_decoder.h"

typedef const struct vpx_codec_iface vpx_codec_iface_t;
typedef struct vpx_image vpx_image_t;

namespace remoting {

class VideoDecoderVpx : public VideoDecoder {
 public:
  // Create decoders for the specified protocol.
  static scoped_ptr<VideoDecoderVpx> CreateForVP8();
  static scoped_ptr<VideoDecoderVpx> CreateForVP9();

  ~VideoDecoderVpx() override;

  // VideoDecoder interface.
  bool DecodePacket(const VideoPacket& packet,
                    webrtc::DesktopFrame* frame) override;

 private:
  explicit VideoDecoderVpx(vpx_codec_iface_t* codec);

  ScopedVpxCodec codec_;

  DISALLOW_COPY_AND_ASSIGN(VideoDecoderVpx);
};

}  // namespace remoting

#endif  // REMOTING_CODEC_VIDEO_DECODER_VP8_H_
