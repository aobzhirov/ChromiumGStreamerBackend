// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "chromecast/media/cma/backend/audio_decoder_default.h"
#include "chromecast/media/cma/backend/media_pipeline_backend_default.h"
#include "chromecast/media/cma/backend/video_decoder_default.h"
#include "chromecast/media/cma/pipeline/av_pipeline_client.h"
#include "chromecast/media/cma/pipeline/media_pipeline_impl.h"
#include "chromecast/media/cma/pipeline/video_pipeline_client.h"
#include "chromecast/media/cma/test/frame_generator_for_test.h"
#include "chromecast/media/cma/test/mock_frame_provider.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/media_util.h"
#include "media/base/video_decoder_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
// Total number of frames generated by CodedFrameProvider.
// The first frame has config, while the last one is EOS.
const int kNumFrames = 100;
const int kFrameSize = 512;
const int kFrameDurationUs = 40 * 1000;
const int kLastFrameTimestamp = (kNumFrames - 2) * kFrameDurationUs;
}  // namespace

namespace chromecast {
namespace media {

using AudioVideoTuple = ::testing::tuple<bool, bool>;

class AudioVideoPipelineImplTest
    : public ::testing::TestWithParam<AudioVideoTuple> {
 public:
  AudioVideoPipelineImplTest() : pipeline_backend_(nullptr) {
    eos_[STREAM_AUDIO] = eos_[STREAM_VIDEO] = false;
  }
  void Start(const base::Closure& eos_cb) {
    eos_cb_ = eos_cb;
    eos_[STREAM_AUDIO] = !media_pipeline_.HasAudio();
    eos_[STREAM_VIDEO] = !media_pipeline_.HasVideo();
    base::TimeDelta start_time = base::TimeDelta::FromMilliseconds(0);
    media_pipeline_.StartPlayingFrom(start_time);
  }
  void Flush(const base::Closure& flush_cb) { media_pipeline_.Flush(flush_cb); }
  void Stop() {
    media_pipeline_.Stop();
    base::MessageLoop::current()->QuitWhenIdle();
  }

  MediaPipelineBackendDefault* pipeline_backend() { return pipeline_backend_; }

 protected:
  void SetUp() override {
    scoped_ptr<MediaPipelineBackendDefault> backend =
        make_scoped_ptr(new MediaPipelineBackendDefault());
    pipeline_backend_ = backend.get();
    media_pipeline_.Initialize(kLoadTypeURL, std::move(backend));

    if (::testing::get<0>(GetParam())) {
      ::media::AudioDecoderConfig audio_config(
          ::media::kCodecMP3, ::media::kSampleFormatS16,
          ::media::CHANNEL_LAYOUT_STEREO, 44100, ::media::EmptyExtraData(),
          ::media::Unencrypted());
      AvPipelineClient client;
      client.eos_cb = base::Bind(&AudioVideoPipelineImplTest::OnEos,
                                 base::Unretained(this), STREAM_AUDIO);
      ::media::PipelineStatus status = media_pipeline_.InitializeAudio(
          audio_config, client, CreateFrameProvider());
      ASSERT_EQ(::media::PIPELINE_OK, status);
    }
    if (::testing::get<1>(GetParam())) {
      std::vector<::media::VideoDecoderConfig> video_configs;
      video_configs.push_back(::media::VideoDecoderConfig(
          ::media::kCodecH264, ::media::H264PROFILE_MAIN,
          ::media::PIXEL_FORMAT_I420, ::media::COLOR_SPACE_UNSPECIFIED,
          gfx::Size(640, 480), gfx::Rect(0, 0, 640, 480), gfx::Size(640, 480),
          ::media::EmptyExtraData(), ::media::Unencrypted()));
      VideoPipelineClient client;
      client.av_pipeline_client.eos_cb =
          base::Bind(&AudioVideoPipelineImplTest::OnEos, base::Unretained(this),
                     STREAM_VIDEO);
      ::media::PipelineStatus status = media_pipeline_.InitializeVideo(
          video_configs, client, CreateFrameProvider());
      ASSERT_EQ(::media::PIPELINE_OK, status);
    }
  }

  base::MessageLoop message_loop_;

 private:
  enum Stream { STREAM_AUDIO, STREAM_VIDEO };
  DISALLOW_COPY_AND_ASSIGN(AudioVideoPipelineImplTest);

  scoped_ptr<CodedFrameProvider> CreateFrameProvider() {
    std::vector<FrameGeneratorForTest::FrameSpec> frame_specs;
    frame_specs.resize(kNumFrames);
    for (size_t k = 0; k < frame_specs.size() - 1; k++) {
      frame_specs[k].has_config = (k == 0);
      frame_specs[k].timestamp =
          base::TimeDelta::FromMicroseconds(kFrameDurationUs) * k;
      frame_specs[k].size = kFrameSize;
      frame_specs[k].has_decrypt_config = false;
    }
    frame_specs[frame_specs.size() - 1].is_eos = true;

    scoped_ptr<FrameGeneratorForTest> frame_generator(
        new FrameGeneratorForTest(frame_specs));
    bool provider_delayed_pattern[] = {false, true};
    scoped_ptr<MockFrameProvider> frame_provider(new MockFrameProvider());
    frame_provider->Configure(
        std::vector<bool>(
            provider_delayed_pattern,
            provider_delayed_pattern + arraysize(provider_delayed_pattern)),
        std::move(frame_generator));
    frame_provider->SetDelayFlush(true);
    return std::move(frame_provider);
  }

  void OnEos(Stream stream) {
    eos_[stream] = true;
    if (eos_[STREAM_AUDIO] && eos_[STREAM_VIDEO] && !eos_cb_.is_null())
      eos_cb_.Run();
  }

  bool eos_[2];
  base::Closure eos_cb_;
  MediaPipelineImpl media_pipeline_;
  MediaPipelineBackendDefault* pipeline_backend_;
};

static void VerifyPlay(AudioVideoPipelineImplTest* pipeline_test) {
  // The backend must still be running.
  MediaPipelineBackendDefault* backend = pipeline_test->pipeline_backend();
  EXPECT_TRUE(backend->running());

  // The decoders must have received a few frames.
  const AudioDecoderDefault* audio_decoder = backend->audio_decoder();
  const VideoDecoderDefault* video_decoder = backend->video_decoder();
  ASSERT_TRUE(audio_decoder || video_decoder);
  if (audio_decoder)
    EXPECT_EQ(kLastFrameTimestamp, audio_decoder->last_push_pts());
  if (video_decoder)
    EXPECT_EQ(kLastFrameTimestamp, video_decoder->last_push_pts());

  pipeline_test->Stop();
}

TEST_P(AudioVideoPipelineImplTest, Play) {
  base::Closure verify_task = base::Bind(&VerifyPlay, base::Unretained(this));
  message_loop_.PostTask(FROM_HERE,
                         base::Bind(&AudioVideoPipelineImplTest::Start,
                                    base::Unretained(this), verify_task));
  message_loop_.Run();
}

static void VerifyFlush(AudioVideoPipelineImplTest* pipeline_test) {
  // The backend must have been stopped.
  MediaPipelineBackendDefault* backend = pipeline_test->pipeline_backend();
  EXPECT_FALSE(backend->running());

  // The decoders must not have received any frame.
  const AudioDecoderDefault* audio_decoder = backend->audio_decoder();
  const VideoDecoderDefault* video_decoder = backend->video_decoder();
  ASSERT_TRUE(audio_decoder || video_decoder);
  if (audio_decoder)
    EXPECT_LT(audio_decoder->last_push_pts(), 0);
  if (video_decoder)
    EXPECT_LT(video_decoder->last_push_pts(), 0);

  pipeline_test->Stop();
}

static void VerifyNotReached() {
  EXPECT_TRUE(false);
}

TEST_P(AudioVideoPipelineImplTest, Flush) {
  base::Closure verify_task = base::Bind(&VerifyFlush, base::Unretained(this));
  message_loop_.PostTask(
      FROM_HERE,
      base::Bind(&AudioVideoPipelineImplTest::Start, base::Unretained(this),
                 base::Bind(&VerifyNotReached)));
  message_loop_.PostTask(FROM_HERE,
                         base::Bind(&AudioVideoPipelineImplTest::Flush,
                                    base::Unretained(this), verify_task));

  message_loop_.Run();
}

TEST_P(AudioVideoPipelineImplTest, FullCycle) {
  base::Closure stop_task =
      base::Bind(&AudioVideoPipelineImplTest::Stop, base::Unretained(this));
  base::Closure eos_cb = base::Bind(&AudioVideoPipelineImplTest::Flush,
                                    base::Unretained(this), stop_task);

  message_loop_.PostTask(FROM_HERE,
                         base::Bind(&AudioVideoPipelineImplTest::Start,
                                    base::Unretained(this), eos_cb));
  message_loop_.Run();
};

// Test all three types of pipeline: audio-only, video-only, audio-video.
INSTANTIATE_TEST_CASE_P(
    MediaPipelineImplTests,
    AudioVideoPipelineImplTest,
    ::testing::Values(AudioVideoTuple(true, false),   // Audio only.
                      AudioVideoTuple(false, true),   // Video only.
                      AudioVideoTuple(true, true)));  // Audio and Video.

}  // namespace media
}  // namespace chromecast
