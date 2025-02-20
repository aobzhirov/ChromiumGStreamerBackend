// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_player_channel_filter.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "content/child/child_thread_impl.h"
#include "content/common/media/media_messages.h"
#include "content/common/media/media_player_channel.h"
#include "ipc/message_filter.h"
#include "ipc/message_router.h"

namespace content {

MediaPlayerChannelFilter::MediaPlayerChannelFilter(
    IPC::MessageRouter* router,
    base::SingleThreadTaskRunner* io_task_runner,
    base::WaitableEvent* shutdown_event,
    IPC::SyncChannel* channel,
    MediaPlayerGStreamerFactory* media_player_factory)
    : io_task_runner_(io_task_runner),
      shutdown_event_(shutdown_event),
      router_(router),
      channel_(channel),
      media_player_factory_(media_player_factory),
      weak_factory_(this) {
  DCHECK(router_);
  DCHECK(io_task_runner);
  DCHECK(shutdown_event);
}

MediaPlayerChannelFilter::~MediaPlayerChannelFilter() {
  media_channels_.clear();
}

void MediaPlayerChannelFilter::RemoveChannel(int client_id) {
  media_channels_.erase(client_id);
}

MediaPlayerChannel* MediaPlayerChannelFilter::LookupChannel(int32_t client_id) {
  MediaChannelMap::const_iterator iter = media_channels_.find(client_id);
  if (iter == media_channels_.end())
    return NULL;
  else
    return iter->second;
}

bool MediaPlayerChannelFilter::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MediaPlayerChannelFilter, msg)
    IPC_MESSAGE_HANDLER(MediaMsg_EstablishChannel, OnEstablishChannel)
    IPC_MESSAGE_HANDLER(MediaMsg_CloseChannel, OnCloseChannel)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

bool MediaPlayerChannelFilter::Send(IPC::Message* msg) {
  return router_->Send(msg);
}

void MediaPlayerChannelFilter::OnEstablishChannel(int client_id) {
  DCHECK_NE(io_task_runner_.get(), base::ThreadTaskRunnerHandle::Get().get());

  IPC::ChannelHandle channel_handle;

  scoped_ptr<MediaPlayerChannel> channel(new MediaPlayerChannel(client_id, this));
  channel->Init(io_task_runner_.get(), shutdown_event_);
  channel_handle.name = channel->GetChannelName();

#if defined(OS_POSIX)
  // On POSIX, pass the renderer-side FD. Also mark it as auto-close so
  // that it gets closed after it has been sent.
  base::ScopedFD renderer_fd = channel->TakeRendererFileDescriptor();
  DCHECK(renderer_fd.is_valid());
  channel_handle.socket = base::FileDescriptor(std::move(renderer_fd));
#endif

  media_channels_.set(client_id, std::move(channel));

  Send(new MediaHostMsg_ChannelEstablished(channel_handle));
}

void MediaPlayerChannelFilter::OnCloseChannel(
    const IPC::ChannelHandle& channel_handle) {
  for (MediaChannelMap::iterator iter = media_channels_.begin();
       iter != media_channels_.end(); ++iter) {
    if (iter->second->GetChannelName() == channel_handle.name) {
      media_channels_.erase(iter);
      return;
    }
  }
}

}  // namespace content
