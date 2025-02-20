// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media_message_filter.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "content/browser/media/media_process_host.h"
#include "content/common/media/media_messages.h"
#include "content/public/common/content_switches.h"

namespace content {

MediaMessageFilter::MediaMessageFilter(int render_process_id)
    : BrowserMessageFilter(MediaPlayerChannelMsgStart),
      media_process_id_(0),  // There is only one media process.
      render_process_id_(render_process_id),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DVLOG(1) << __FUNCTION__ << "(Create MediaMessageFilter)";
}

MediaMessageFilter::~MediaMessageFilter() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
}

bool MediaMessageFilter::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MediaMessageFilter, message)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(MediaHostMsg_EstablishMediaChannel,
                                    OnEstablishMediaChannel)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void MediaMessageFilter::OnEstablishMediaChannel(
    CauseForMediaLaunch cause_for_media_launch,
    IPC::Message* reply_ptr) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  scoped_ptr<IPC::Message> reply(reply_ptr);

  MediaProcessHost* host = MediaProcessHost::FromID(media_process_id_);
  if (!host) {
    DVLOG(1) << __FUNCTION__ << "(Launching Media Process)";
    host = MediaProcessHost::Get(MediaProcessHost::MEDIA_PROCESS_KIND_SANDBOXED,
                                 cause_for_media_launch);
    if (!host) {
      reply->set_reply_error();
      Send(reply.release());
      return;
    }

    media_process_id_ = host->host_id();
  }

  DVLOG(1) << __FUNCTION__ << "(Establishing channel)";

  host->EstablishMediaChannel(
      render_process_id_,
      base::Bind(&MediaMessageFilter::EstablishChannelCallback,
                 weak_ptr_factory_.GetWeakPtr(), base::Passed(&reply)));
}

void MediaMessageFilter::EstablishChannelCallback(
    scoped_ptr<IPC::Message> reply,
    const IPC::ChannelHandle& channel) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DVLOG(1) << __FUNCTION__ << "(Media channel established)";
  MediaHostMsg_EstablishMediaChannel::WriteReplyParams(
      reply.get(), render_process_id_, channel);
  Send(reply.release());
}

}  // namespace content
