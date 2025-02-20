// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/cast_transport_host_filter.h"

#include "base/thread_task_runner_handle.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/cast_messages.h"
#include "components/net_log/chrome_net_log.h"
#include "content/public/browser/power_save_blocker.h"
#include "media/cast/net/cast_transport_sender.h"

namespace {

// How often to send raw events.
const int kSendRawEventsIntervalSecs = 1;

class TransportClient : public media::cast::CastTransportSender::Client {
 public:
  TransportClient(int32_t channel_id,
                  cast::CastTransportHostFilter* cast_transport_host_filter)
      : channel_id_(channel_id),
        cast_transport_host_filter_(cast_transport_host_filter) {}

  void OnStatusChanged(media::cast::CastTransportStatus status) final;
  void OnLoggingEventsReceived(
      scoped_ptr<std::vector<media::cast::FrameEvent>> frame_events,
      scoped_ptr<std::vector<media::cast::PacketEvent>> packet_events) final;
  void ProcessRtpPacket(scoped_ptr<media::cast::Packet> packet) final;

 private:
  const int32_t channel_id_;
  cast::CastTransportHostFilter* const cast_transport_host_filter_;

  DISALLOW_COPY_AND_ASSIGN(TransportClient);
};

void TransportClient::OnStatusChanged(media::cast::CastTransportStatus status) {
  cast_transport_host_filter_->Send(
      new CastMsg_NotifyStatusChange(channel_id_, status));
}

void TransportClient::OnLoggingEventsReceived(
    scoped_ptr<std::vector<media::cast::FrameEvent>> frame_events,
    scoped_ptr<std::vector<media::cast::PacketEvent>> packet_events) {
  if (frame_events->empty() && packet_events->empty())
    return;
  cast_transport_host_filter_->Send(
      new CastMsg_RawEvents(channel_id_, *packet_events, *frame_events));
}

void TransportClient::ProcessRtpPacket(scoped_ptr<media::cast::Packet> packet) {
  cast_transport_host_filter_->Send(
      new CastMsg_ReceivedPacket(channel_id_, *packet));
}

}  // namespace

namespace cast {

CastTransportHostFilter::CastTransportHostFilter()
    : BrowserMessageFilter(CastMsgStart),
      weak_factory_(this) {}

CastTransportHostFilter::~CastTransportHostFilter() {}

void CastTransportHostFilter::OnStatusChanged(
    int32_t channel_id,
    media::cast::CastTransportStatus status) {
  Send(new CastMsg_NotifyStatusChange(channel_id, status));
}

bool CastTransportHostFilter::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(CastTransportHostFilter, message)
    IPC_MESSAGE_HANDLER(CastHostMsg_New, OnNew)
    IPC_MESSAGE_HANDLER(CastHostMsg_Delete, OnDelete)
    IPC_MESSAGE_HANDLER(CastHostMsg_InitializeAudio, OnInitializeAudio)
    IPC_MESSAGE_HANDLER(CastHostMsg_InitializeVideo, OnInitializeVideo)
    IPC_MESSAGE_HANDLER(CastHostMsg_InsertFrame, OnInsertFrame)
    IPC_MESSAGE_HANDLER(CastHostMsg_SendSenderReport,
                        OnSendSenderReport)
    IPC_MESSAGE_HANDLER(CastHostMsg_ResendFrameForKickstart,
                        OnResendFrameForKickstart)
    IPC_MESSAGE_HANDLER(CastHostMsg_CancelSendingFrames,
                        OnCancelSendingFrames)
    IPC_MESSAGE_HANDLER(CastHostMsg_AddValidRtpReceiver, OnAddValidRtpReceiver)
    IPC_MESSAGE_HANDLER(CastHostMsg_InitializeRtpReceiverRtcpBuilder,
                        OnInitializeRtpReceiverRtcpBuilder)
    IPC_MESSAGE_HANDLER(CastHostMsg_AddCastFeedback, OnAddCastFeedback)
    IPC_MESSAGE_HANDLER(CastHostMsg_AddPli, OnAddPli)
    IPC_MESSAGE_HANDLER(CastHostMsg_AddRtcpEvents, OnAddRtcpEvents)
    IPC_MESSAGE_HANDLER(CastHostMsg_AddRtpReceiverReport,
                        OnAddRtpReceiverReport)
    IPC_MESSAGE_HANDLER(CastHostMsg_SendRtcpFromRtpReceiver,
                        OnSendRtcpFromRtpReceiver)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void CastTransportHostFilter::SendRtt(int32_t channel_id,
                                      uint32_t rtp_sender_ssrc,
                                      base::TimeDelta rtt) {
  Send(new CastMsg_Rtt(channel_id, rtp_sender_ssrc, rtt));
}

void CastTransportHostFilter::SendCastMessage(
    int32_t channel_id,
    uint32_t rtp_sender_ssrc,
    const media::cast::RtcpCastMessage& cast_message) {
  Send(new CastMsg_RtcpCastMessage(channel_id, rtp_sender_ssrc, cast_message));
}

void CastTransportHostFilter::SendReceivedPli(int32_t channel_id,
                                              uint32_t rtp_sender_ssrc) {
  Send(new CastMsg_Pli(channel_id, rtp_sender_ssrc));
}

void CastTransportHostFilter::OnNew(int32_t channel_id,
                                    const net::IPEndPoint& local_end_point,
                                    const net::IPEndPoint& remote_end_point,
                                    const base::DictionaryValue& options) {
  if (!power_save_blocker_) {
    DVLOG(1) << ("Preventing the application from being suspended while one or "
                 "more transports are active for Cast Streaming.");
    power_save_blocker_ = content::PowerSaveBlocker::Create(
        content::PowerSaveBlocker::kPowerSaveBlockPreventAppSuspension,
        content::PowerSaveBlocker::kReasonOther,
        "Cast is streaming content to a remote receiver");
  }

  if (id_map_.Lookup(channel_id)) {
    id_map_.Remove(channel_id);
  }

  scoped_ptr<media::cast::UdpTransport> udp_transport(
      new media::cast::UdpTransport(
          g_browser_process->net_log(), base::ThreadTaskRunnerHandle::Get(),
          local_end_point, remote_end_point,
          base::Bind(&CastTransportHostFilter::OnStatusChanged,
                     weak_factory_.GetWeakPtr(), channel_id)));
  udp_transport->SetUdpOptions(options);
  scoped_ptr<media::cast::CastTransportSender> sender =
      media::cast::CastTransportSender::Create(
          &clock_, base::TimeDelta::FromSeconds(kSendRawEventsIntervalSecs),
          make_scoped_ptr(new TransportClient(channel_id, this)),
          std::move(udp_transport), base::ThreadTaskRunnerHandle::Get());
  sender->SetOptions(options);
  id_map_.AddWithID(sender.release(), channel_id);
}

void CastTransportHostFilter::OnDelete(int32_t channel_id) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    id_map_.Remove(channel_id);
  } else {
    DVLOG(1) << "CastTransportHostFilter::Delete called "
             << "on non-existing channel";
  }

  if (id_map_.IsEmpty()) {
    DVLOG_IF(1, power_save_blocker_) <<
        ("Releasing the block on application suspension since no transports "
         "are active anymore for Cast Streaming.");
    power_save_blocker_.reset();
  }
}

void CastTransportHostFilter::OnInitializeAudio(
    int32_t channel_id,
    const media::cast::CastTransportRtpConfig& config) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->InitializeAudio(
        config, base::Bind(&CastTransportHostFilter::SendCastMessage,
                           weak_factory_.GetWeakPtr(), channel_id, config.ssrc),
        base::Bind(&CastTransportHostFilter::SendRtt,
                   weak_factory_.GetWeakPtr(), channel_id, config.ssrc),
        base::Bind(&CastTransportHostFilter::SendReceivedPli,
                   weak_factory_.GetWeakPtr(), channel_id, config.ssrc));
  } else {
    DVLOG(1)
        << "CastTransportHostFilter::OnInitializeAudio on non-existing channel";
  }
}

void CastTransportHostFilter::OnInitializeVideo(
    int32_t channel_id,
    const media::cast::CastTransportRtpConfig& config) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->InitializeVideo(
        config, base::Bind(&CastTransportHostFilter::SendCastMessage,
                           weak_factory_.GetWeakPtr(), channel_id, config.ssrc),
        base::Bind(&CastTransportHostFilter::SendRtt,
                   weak_factory_.GetWeakPtr(), channel_id, config.ssrc),
        base::Bind(&CastTransportHostFilter::SendReceivedPli,
                   weak_factory_.GetWeakPtr(), channel_id, config.ssrc));
  } else {
    DVLOG(1)
        << "CastTransportHostFilter::OnInitializeVideo on non-existing channel";
  }
}

void CastTransportHostFilter::OnInsertFrame(
    int32_t channel_id,
    uint32_t ssrc,
    const media::cast::EncodedFrame& frame) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->InsertFrame(ssrc, frame);
  } else {
    DVLOG(1)
        << "CastTransportHostFilter::OnInsertFrame on non-existing channel";
  }
}

void CastTransportHostFilter::OnCancelSendingFrames(
    int32_t channel_id,
    uint32_t ssrc,
    const std::vector<uint32_t>& frame_ids) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->CancelSendingFrames(ssrc, frame_ids);
  } else {
    DVLOG(1)
        << "CastTransportHostFilter::OnCancelSendingFrames "
        << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnResendFrameForKickstart(int32_t channel_id,
                                                        uint32_t ssrc,
                                                        uint32_t frame_id) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->ResendFrameForKickstart(ssrc, frame_id);
  } else {
    DVLOG(1)
        << "CastTransportHostFilter::OnResendFrameForKickstart "
        << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnSendSenderReport(
    int32_t channel_id,
    uint32_t ssrc,
    base::TimeTicks current_time,
    media::cast::RtpTimeTicks current_time_as_rtp_timestamp) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->SendSenderReport(ssrc,
                             current_time,
                             current_time_as_rtp_timestamp);
  } else {
    DVLOG(1)
        << "CastTransportHostFilter::OnSendSenderReport "
        << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnAddValidRtpReceiver(
    int32_t channel_id,
    uint32_t rtp_sender_ssrc,
    uint32_t rtp_receiver_ssrc) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->AddValidRtpReceiver(rtp_sender_ssrc, rtp_receiver_ssrc);
  } else {
    DVLOG(1) << "CastTransportHostFilter::OnAddValidRtpReceiver "
             << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnInitializeRtpReceiverRtcpBuilder(
    int32_t channel_id,
    uint32_t rtp_receiver_ssrc,
    const media::cast::RtcpTimeData& time_data) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->InitializeRtpReceiverRtcpBuilder(rtp_receiver_ssrc, time_data);
  } else {
    DVLOG(1) << "CastTransportHostFilter::OnInitializeRtpReceiverRtcpBuilder "
             << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnAddCastFeedback(
    int32_t channel_id,
    const media::cast::RtcpCastMessage& cast_message,
    base::TimeDelta target_delay) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->AddCastFeedback(cast_message, target_delay);
  } else {
    DVLOG(1) << "CastTransportHostFilter::OnAddCastFeedback "
             << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnAddPli(
    int32_t channel_id,
    const media::cast::RtcpPliMessage& pli_message) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->AddPli(pli_message);
  } else {
    DVLOG(1) << "CastTransportHostFilter::OnAddPli on non-existing channel";
  }
}

void CastTransportHostFilter::OnAddRtcpEvents(
    int32_t channel_id,
    const media::cast::ReceiverRtcpEventSubscriber::RtcpEvents& rtcp_events) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->AddRtcpEvents(rtcp_events);
  } else {
    DVLOG(1) << "CastTransportHostFilter::OnAddRtcpEvents "
             << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnAddRtpReceiverReport(
    int32_t channel_id,
    const media::cast::RtcpReportBlock& rtp_receiver_report_block) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->AddRtpReceiverReport(rtp_receiver_report_block);
  } else {
    DVLOG(1) << "CastTransportHostFilter::OnAddRtpReceiverReport "
             << "on non-existing channel";
  }
}

void CastTransportHostFilter::OnSendRtcpFromRtpReceiver(int32_t channel_id) {
  media::cast::CastTransportSender* sender = id_map_.Lookup(channel_id);
  if (sender) {
    sender->SendRtcpFromRtpReceiver();
  } else {
    DVLOG(1)
        << "CastTransportHostFilter::OnSendRtcpFromRtpReceiver "
        << "on non-existing channel";
  }
}

}  // namespace cast
