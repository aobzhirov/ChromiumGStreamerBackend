// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/media/media_channel.h"

#include <queue>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "content/public/common/content_switches.h"
#include "content/child/child_process.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/message_filter.h"
#include "content/common/media/media_channel_filter.h"
#include "content/common/media/media_player_messages_gstreamer.h"
#include "content/media/gstreamer/media_player_gstreamer.h"

#if defined(OS_POSIX)
#include "ipc/ipc_channel_posix.h"
#endif

namespace content {

MediaChannel::MediaChannel(int client_id, MediaChannelFilter* channel_filter)
    : client_id_(client_id), channel_filter_(channel_filter) {
  DCHECK(client_id);

  channel_id_ = IPC::Channel::GenerateVerifiedChannelID("media");
}

MediaChannel::~MediaChannel() {}

void MediaChannel::Init(base::SingleThreadTaskRunner* io_task_runner,
                        base::WaitableEvent* shutdown_event) {
  DCHECK(!channel_.get());

  // Map renderer ID to a (single) channel to that process.
  channel_ =
      IPC::SyncChannel::Create(channel_id_, IPC::Channel::MODE_SERVER, this,
                               io_task_runner, false, shutdown_event);
}

std::string MediaChannel::GetChannelName() {
  return channel_id_;
}

#if defined(OS_POSIX)
base::ScopedFD MediaChannel::TakeRendererFileDescriptor() {
  if (!channel_) {
    NOTREACHED();
    return base::ScopedFD();
  }
  return channel_->TakeClientFileDescriptor();
}
#endif  // defined(OS_POSIX)

bool MediaChannel::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;

  IPC_BEGIN_MESSAGE_MAP(MediaChannel, message)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_Create, OnMediaPlayerCreate)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_Load, OnMediaPlayerLoad)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_Start, OnMediaPlayerStart)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_Pause, OnMediaPlayerPause)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_Seek, OnMediaPlayerSeek)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_Release, OnMediaPlayerRelease)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_ReleaseTexture,
                        OnMediaPlayerReleaseTexture)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_AddSourceId, OnMediaPlayerAddSourceId)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_RemoveSourceId,
                        OnMediaPlayerRemoveSourceId)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_SetDuration, OnMediaPlayerSetDuration)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_MarkEndOfStream,
                        OnMediaPlayerMarkEndOfStream)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_UnmarkEndOfStream,
                        OnMediaPlayerUnmarkEndOfStream)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_SetSequenceMode,
                        OnMediaPlayerSetSequenceMode)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_AppendData, OnMediaPlayerAppendData)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_Abort, OnMediaPlayerAbort)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_SetGroupStartTimestampIfInSequenceMode,
                        OnMediaPlayerSetGroupStartTimestampIfInSequenceMode)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_RemoveSegment,
                        OnMediaPlayerRemoveSegment)
    IPC_MESSAGE_HANDLER(MediaPlayerMsg_AddKey, OnMediaPlayerAddKey)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void MediaChannel::OnChannelError() {
  channel_filter_->RemoveChannel(client_id_);
}

MediaPlayerGStreamer* MediaChannel::GetMediaPlayer(int player_id) {
  MediaPlayerMap::const_iterator iter = media_players_.find(player_id);
  if (iter != media_players_.end())
    return iter->second;

  scoped_ptr<MediaPlayerGStreamer> player(
      channel_filter_->GetMediaPlayerFactory()->create(player_id, this));
  media_players_.set(player_id, player.Pass());
  return media_players_.find(player_id)->second;
}

void MediaChannel::OnMediaPlayerCreate(int player_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (!player) {
    SendMediaError(player_id, 0);
  }
}

void MediaChannel::OnMediaPlayerLoad(int player_id, GURL url, unsigned position_update_interval_ms) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Load(url, position_update_interval_ms);
  }
}

void MediaChannel::OnMediaPlayerStart(int player_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Play();
  }
}

void MediaChannel::OnMediaPlayerPause(int player_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Pause();
  }
}

void MediaChannel::OnMediaPlayerSeek(int player_id, base::TimeDelta delta) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Seek(delta);
  }
}

void MediaChannel::OnMediaPlayerRelease(int player_id) {
  media_players_.erase(player_id);
}

void MediaChannel::OnMediaPlayerReleaseTexture(int player_id,
                                               unsigned texture_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->ReleaseTexture(texture_id);
  }
}

void MediaChannel::OnMediaPlayerAddSourceId(
    int player_id,
    const std::string& source_id,
    const std::string& type,
    const std::vector<std::string>& codecs) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->AddSourceId(source_id, type, codecs);
  }
}

void MediaChannel::OnMediaPlayerRemoveSourceId(int player_id,
                                               const std::string& source_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->RemoveSourceId(source_id);
  }
}

void MediaChannel::OnMediaPlayerSetDuration(int player_id,
                                            const base::TimeDelta& duration) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->SetDuration(duration);
  }
}

void MediaChannel::OnMediaPlayerMarkEndOfStream(int player_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->MarkEndOfStream();
  }
}

void MediaChannel::OnMediaPlayerUnmarkEndOfStream(int player_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->UnmarkEndOfStream();
  }
}

void MediaChannel::OnMediaPlayerSetSequenceMode(int player_id,
                                                const std::string& source_id,
                                                bool sequence_mode) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->SetSequenceMode(source_id, sequence_mode);
  }
}

void MediaChannel::OnMediaPlayerAppendData(
    int player_id,
    const std::string& source_id,
    const std::vector<unsigned char>& data,
    const std::vector<base::TimeDelta>& times) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->AppendData(source_id, data, times);
  }
}

void MediaChannel::OnMediaPlayerAbort(int player_id,
                                      const std::string& source_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Abort(source_id);
  }
}

void MediaChannel::OnMediaPlayerSetGroupStartTimestampIfInSequenceMode(
    int player_id,
    const std::string& source_id,
    const base::TimeDelta& timestamp_offset) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->SetGroupStartTimestampIfInSequenceMode(source_id, timestamp_offset);
  }
}

void MediaChannel::OnMediaPlayerRemoveSegment(int player_id,
                                              const std::string& source_id,
                                              const base::TimeDelta& start,
                                              const base::TimeDelta& end) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->RemoveSegment(source_id, start, end);
  }
}

void MediaChannel::OnMediaPlayerAddKey(int player_id,
                                       const std::string& session_id,
                                       const std::string& key_id,
                                       const std::string& key) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->AddKey(session_id, key_id, key);
  }
}

void MediaChannel::SendMediaError(int player_id, int error) {
  Send(new MediaPlayerMsg_MediaError(player_id, error));
}

void MediaChannel::SendMediaPlaybackCompleted(int player_id) {
  Send(new MediaPlayerMsg_MediaPlaybackCompleted(player_id));
}

void MediaChannel::SendMediaDurationChanged(int player_id,
                                            const base::TimeDelta& duration) {
  Send(new MediaPlayerMsg_MediaDurationChanged(player_id, duration));
}

void MediaChannel::SendSeekCompleted(int player_id,
                                     const base::TimeDelta& current_time) {
  Send(new MediaPlayerMsg_SeekCompleted(player_id, current_time));
}

void MediaChannel::SendMediaVideoSizeChanged(int player_id,
                                             int width,
                                             int height) {
  Send(new MediaPlayerMsg_MediaVideoSizeChanged(player_id, width, height));
}

void MediaChannel::SendMediaTimeUpdate(int player_id,
                                       base::TimeDelta current_timestamp,
                                       base::TimeTicks current_time_ticks) {
  Send(new MediaPlayerMsg_MediaTimeUpdate(player_id, current_timestamp,
                                          current_time_ticks));
}

void MediaChannel::SendBufferingUpdate(int player_id, int percent) {
  Send(new MediaPlayerMsg_MediaBufferingUpdate(player_id, percent));
}

void MediaChannel::SendMediaPlayerReleased(int player_id) {
  Send(new MediaPlayerMsg_MediaPlayerReleased(player_id));
}

void MediaChannel::SendDidMediaPlayerPlay(int player_id) {
  Send(new MediaPlayerMsg_DidMediaPlayerPlay(player_id));
}

void MediaChannel::SendDidMediaPlayerPause(int player_id) {
  Send(new MediaPlayerMsg_DidMediaPlayerPause(player_id));
}

void MediaChannel::SendSetCurrentFrame(int player_id,
                                       int width,
                                       int height,
                                       unsigned texture_id,
                                       const std::vector<int32_t>& name) {
  Send(new MediaPlayerMsg_SetCurrentFrame(player_id, width, height, texture_id,
                                          name));
}

void MediaChannel::SendSourceSelected(int player_id) {
  Send(new MediaPlayerMsg_SourceSelected(player_id));
}

void MediaChannel::SendDidAddSourceId(int player_id,
                                      const std::string& source_id) {
  Send(new MediaPlayerMsg_DidAddSourceId(player_id, source_id));
}

void MediaChannel::SendDidRemoveSourceId(int player_id,
                                         const std::string& source_id) {
  Send(new MediaPlayerMsg_DidRemoveSourceId(player_id, source_id));
}

void MediaChannel::SendInitSegmentReceived(int player_id,
                                           const std::string& source_id) {
  Send(new MediaPlayerMsg_InitSegmentReceived(player_id, source_id));
}

void MediaChannel::SendBufferedRangeUpdate(
    int player_id,
    const std::string& source_id,
    const std::vector<base::TimeDelta>& ranges) {
  Send(new MediaPlayerMsg_BufferedRangeUpdate(player_id, source_id, ranges));
}

void MediaChannel::SendTimestampOffsetUpdate(
    int player_id,
    const std::string& source_id,
    const base::TimeDelta& timestamp_offset) {
  Send(new MediaPlayerMsg_TimestampOffsetUpdate(player_id, source_id,
                                                timestamp_offset));
}

void MediaChannel::SendNeedKey(int player_id,
                               const std::string& system_id,
                               const std::vector<unsigned char>& init_data) {
  Send(new MediaPlayerMsg_NeedKey(player_id, system_id, init_data));
}

bool MediaChannel::Send(IPC::Message* message) {
  // The media process must never send a synchronous IPC message to the renderer
  // process. This could result in deadlock.
  DCHECK(!message->is_sync());

  if (!channel_) {
    delete message;
    return false;
  }

  return channel_->Send(message);
}

}  // namespace content
