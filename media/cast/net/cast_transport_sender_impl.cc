// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/net/cast_transport_sender_impl.h"

#include <stddef.h>
#include <algorithm>
#include <string>
#include <utility>

#include "base/single_thread_task_runner.h"
#include "build/build_config.h"
#include "media/cast/net/cast_transport_defines.h"
#include "media/cast/net/rtcp/sender_rtcp_session.h"
#include "net/base/net_errors.h"

namespace media {
namespace cast {

namespace {

// Options for PaceSender.
const char kOptionPacerMaxBurstSize[] = "pacer_max_burst_size";
const char kOptionPacerTargetBurstSize[] = "pacer_target_burst_size";

// Wifi options.
const char kOptionWifiDisableScan[] = "disable_wifi_scan";
const char kOptionWifiMediaStreamingMode[] = "media_streaming_mode";

int LookupOptionWithDefault(const base::DictionaryValue& options,
                            const std::string& path,
                            int default_value) {
  int ret;
  if (options.GetInteger(path, &ret)) {
    return ret;
  } else {
    return default_value;
  }
}

}  // namespace

scoped_ptr<CastTransportSender> CastTransportSender::Create(
    base::TickClock* clock,  // Owned by the caller.
    base::TimeDelta logging_flush_interval,
    scoped_ptr<Client> client,
    scoped_ptr<PacketSender> transport,
    const scoped_refptr<base::SingleThreadTaskRunner>& transport_task_runner) {
  return scoped_ptr<CastTransportSender>(new CastTransportSenderImpl(
      clock, logging_flush_interval, std::move(client), std::move(transport),
      transport_task_runner.get()));
}

PacketReceiverCallback CastTransportSender::PacketReceiverForTesting() {
  return PacketReceiverCallback();
}

CastTransportSenderImpl::CastTransportSenderImpl(
    base::TickClock* clock,
    base::TimeDelta logging_flush_interval,
    scoped_ptr<Client> client,
    scoped_ptr<PacketSender> transport,
    const scoped_refptr<base::SingleThreadTaskRunner>& transport_task_runner)
    : clock_(clock),
      logging_flush_interval_(logging_flush_interval),
      transport_client_(std::move(client)),
      transport_(std::move(transport)),
      transport_task_runner_(transport_task_runner),
      pacer_(kTargetBurstSize,
             kMaxBurstSize,
             clock,
             logging_flush_interval > base::TimeDelta() ? &recent_packet_events_
                                                        : nullptr,
             transport_.get(),
             transport_task_runner),
      last_byte_acked_for_audio_(0),
      weak_factory_(this) {
  DCHECK(clock);
  DCHECK(transport_client_);
  DCHECK(transport_);
  DCHECK(transport_task_runner_);
  if (logging_flush_interval_ > base::TimeDelta()) {
    transport_task_runner_->PostDelayedTask(
        FROM_HERE, base::Bind(&CastTransportSenderImpl::SendRawEvents,
                              weak_factory_.GetWeakPtr()),
        logging_flush_interval_);
  }
  transport_->StartReceiving(base::Bind(
      &CastTransportSenderImpl::OnReceivedPacket, base::Unretained(this)));
}

CastTransportSenderImpl::~CastTransportSenderImpl() {
  transport_->StopReceiving();
}

void CastTransportSenderImpl::InitializeAudio(
    const CastTransportRtpConfig& config,
    const RtcpCastMessageCallback& cast_message_cb,
    const RtcpRttCallback& rtt_cb,
    const RtcpPliCallback& pli_cb) {
  LOG_IF(WARNING, config.aes_key.empty() || config.aes_iv_mask.empty())
      << "Unsafe to send audio with encryption DISABLED.";
  if (!audio_encryptor_.Initialize(config.aes_key, config.aes_iv_mask)) {
    transport_client_->OnStatusChanged(TRANSPORT_AUDIO_UNINITIALIZED);
    return;
  }

  audio_sender_.reset(new RtpSender(transport_task_runner_, &pacer_));
  if (audio_sender_->Initialize(config)) {
    // Audio packets have a higher priority.
    pacer_.RegisterAudioSsrc(config.ssrc);
    pacer_.RegisterPrioritySsrc(config.ssrc);
    transport_client_->OnStatusChanged(TRANSPORT_AUDIO_INITIALIZED);
  } else {
    audio_sender_.reset();
    transport_client_->OnStatusChanged(TRANSPORT_AUDIO_UNINITIALIZED);
    return;
  }

  audio_rtcp_session_.reset(new SenderRtcpSession(
      base::Bind(&CastTransportSenderImpl::OnReceivedCastMessage,
                 weak_factory_.GetWeakPtr(), config.ssrc, cast_message_cb),
      rtt_cb, base::Bind(&CastTransportSenderImpl::OnReceivedLogMessage,
                         weak_factory_.GetWeakPtr(), AUDIO_EVENT),
      pli_cb, clock_, &pacer_, config.ssrc, config.feedback_ssrc));
  pacer_.RegisterAudioSsrc(config.ssrc);
  valid_sender_ssrcs_.insert(config.feedback_ssrc);
  transport_client_->OnStatusChanged(TRANSPORT_AUDIO_INITIALIZED);
}

void CastTransportSenderImpl::InitializeVideo(
    const CastTransportRtpConfig& config,
    const RtcpCastMessageCallback& cast_message_cb,
    const RtcpRttCallback& rtt_cb,
    const RtcpPliCallback& pli_cb) {
  LOG_IF(WARNING, config.aes_key.empty() || config.aes_iv_mask.empty())
      << "Unsafe to send video with encryption DISABLED.";
  if (!video_encryptor_.Initialize(config.aes_key, config.aes_iv_mask)) {
    transport_client_->OnStatusChanged(TRANSPORT_VIDEO_UNINITIALIZED);
    return;
  }

  video_sender_.reset(new RtpSender(transport_task_runner_, &pacer_));
  if (!video_sender_->Initialize(config)) {
    video_sender_.reset();
    transport_client_->OnStatusChanged(TRANSPORT_VIDEO_UNINITIALIZED);
    return;
  }

  video_rtcp_session_.reset(new SenderRtcpSession(
      base::Bind(&CastTransportSenderImpl::OnReceivedCastMessage,
                 weak_factory_.GetWeakPtr(), config.ssrc, cast_message_cb),
      rtt_cb, base::Bind(&CastTransportSenderImpl::OnReceivedLogMessage,
                         weak_factory_.GetWeakPtr(), VIDEO_EVENT),
      pli_cb, clock_, &pacer_, config.ssrc, config.feedback_ssrc));
  pacer_.RegisterVideoSsrc(config.ssrc);
  valid_sender_ssrcs_.insert(config.feedback_ssrc);
  transport_client_->OnStatusChanged(TRANSPORT_VIDEO_INITIALIZED);
}

namespace {
void EncryptAndSendFrame(const EncodedFrame& frame,
                         TransportEncryptionHandler* encryptor,
                         RtpSender* sender) {
  // TODO(miu): We probably shouldn't attempt to send an empty frame, but this
  // issue is still under investigation.  http://crbug.com/519022
  if (encryptor->is_activated() && !frame.data.empty()) {
    EncodedFrame encrypted_frame;
    frame.CopyMetadataTo(&encrypted_frame);
    if (encryptor->Encrypt(frame.frame_id, frame.data, &encrypted_frame.data)) {
      sender->SendFrame(encrypted_frame);
    } else {
      LOG(ERROR) << "Encryption failed.  Not sending frame with ID "
                 << frame.frame_id;
    }
  } else {
    sender->SendFrame(frame);
  }
}
}  // namespace

void CastTransportSenderImpl::InsertFrame(uint32_t ssrc,
                                          const EncodedFrame& frame) {
  if (audio_sender_ && ssrc == audio_sender_->ssrc()) {
    EncryptAndSendFrame(frame, &audio_encryptor_, audio_sender_.get());
  } else if (video_sender_ && ssrc == video_sender_->ssrc()) {
    EncryptAndSendFrame(frame, &video_encryptor_, video_sender_.get());
  } else {
    NOTREACHED() << "Invalid InsertFrame call.";
  }
}

void CastTransportSenderImpl::SendSenderReport(
    uint32_t ssrc,
    base::TimeTicks current_time,
    RtpTimeTicks current_time_as_rtp_timestamp) {
  if (audio_sender_ && ssrc == audio_sender_->ssrc()) {
    audio_rtcp_session_->SendRtcpReport(
        current_time, current_time_as_rtp_timestamp,
        audio_sender_->send_packet_count(), audio_sender_->send_octet_count());
  } else if (video_sender_ && ssrc == video_sender_->ssrc()) {
    video_rtcp_session_->SendRtcpReport(
        current_time, current_time_as_rtp_timestamp,
        video_sender_->send_packet_count(), video_sender_->send_octet_count());
  } else {
    NOTREACHED() << "Invalid request for sending RTCP packet.";
  }
}

void CastTransportSenderImpl::CancelSendingFrames(
    uint32_t ssrc,
    const std::vector<uint32_t>& frame_ids) {
  if (audio_sender_ && ssrc == audio_sender_->ssrc()) {
    audio_sender_->CancelSendingFrames(frame_ids);
  } else if (video_sender_ && ssrc == video_sender_->ssrc()) {
    video_sender_->CancelSendingFrames(frame_ids);
  } else {
    NOTREACHED() << "Invalid request for cancel sending.";
  }
}

void CastTransportSenderImpl::ResendFrameForKickstart(uint32_t ssrc,
                                                      uint32_t frame_id) {
  if (audio_sender_ && ssrc == audio_sender_->ssrc()) {
    DCHECK(audio_rtcp_session_);
    audio_sender_->ResendFrameForKickstart(
        frame_id,
        audio_rtcp_session_->current_round_trip_time());
  } else if (video_sender_ && ssrc == video_sender_->ssrc()) {
    DCHECK(video_rtcp_session_);
    video_sender_->ResendFrameForKickstart(
        frame_id,
        video_rtcp_session_->current_round_trip_time());
  } else {
    NOTREACHED() << "Invalid request for kickstart.";
  }
}

void CastTransportSenderImpl::ResendPackets(
    uint32_t ssrc,
    const MissingFramesAndPacketsMap& missing_packets,
    bool cancel_rtx_if_not_in_list,
    const DedupInfo& dedup_info) {
  if (audio_sender_ && ssrc == audio_sender_->ssrc()) {
    audio_sender_->ResendPackets(missing_packets,
                                 cancel_rtx_if_not_in_list,
                                 dedup_info);
  } else if (video_sender_ && ssrc == video_sender_->ssrc()) {
    video_sender_->ResendPackets(missing_packets,
                                 cancel_rtx_if_not_in_list,
                                 dedup_info);
  } else {
    NOTREACHED() << "Invalid request for retransmission.";
  }
}

PacketReceiverCallback CastTransportSenderImpl::PacketReceiverForTesting() {
  return base::Bind(
      base::IgnoreResult(&CastTransportSenderImpl::OnReceivedPacket),
      weak_factory_.GetWeakPtr());
}

void CastTransportSenderImpl::SendRawEvents() {
  DCHECK(logging_flush_interval_ > base::TimeDelta());

  if (!recent_frame_events_.empty() || !recent_packet_events_.empty()) {
    scoped_ptr<std::vector<FrameEvent>> frame_events(
        new std::vector<FrameEvent>());
    frame_events->swap(recent_frame_events_);
    scoped_ptr<std::vector<PacketEvent>> packet_events(
        new std::vector<PacketEvent>());
    packet_events->swap(recent_packet_events_);
    transport_client_->OnLoggingEventsReceived(std::move(frame_events),
                                               std::move(packet_events));
  }

  transport_task_runner_->PostDelayedTask(
      FROM_HERE, base::Bind(&CastTransportSenderImpl::SendRawEvents,
                            weak_factory_.GetWeakPtr()),
      logging_flush_interval_);
}

bool CastTransportSenderImpl::OnReceivedPacket(scoped_ptr<Packet> packet) {
  const uint8_t* const data = &packet->front();
  const size_t length = packet->size();
  uint32_t ssrc;
  if (IsRtcpPacket(data, length)) {
    ssrc = GetSsrcOfSender(data, length);
  } else if (!RtpParser::ParseSsrc(data, length, &ssrc)) {
    VLOG(1) << "Invalid RTP packet.";
    return false;
  }
  if (valid_sender_ssrcs_.find(ssrc) == valid_sender_ssrcs_.end()) {
    VLOG(1) << "Stale packet received.";
    return false;
  }

  if (audio_rtcp_session_ &&
      audio_rtcp_session_->IncomingRtcpPacket(data, length)) {
    return true;
  }
  if (video_rtcp_session_ &&
      video_rtcp_session_->IncomingRtcpPacket(data, length)) {
    return true;
  }
  transport_client_->ProcessRtpPacket(std::move(packet));
  return true;
}

void CastTransportSenderImpl::OnReceivedLogMessage(
    EventMediaType media_type,
    const RtcpReceiverLogMessage& log) {
  if (logging_flush_interval_ <= base::TimeDelta())
    return;

  // Add received log messages into our log system.
  for (const RtcpReceiverFrameLogMessage& frame_log_message : log) {
    for (const RtcpReceiverEventLogMessage& event_log_message :
         frame_log_message.event_log_messages_) {
      switch (event_log_message.type) {
        case PACKET_RECEIVED: {
          recent_packet_events_.push_back(PacketEvent());
          PacketEvent& receive_event = recent_packet_events_.back();
          receive_event.timestamp = event_log_message.event_timestamp;
          receive_event.type = event_log_message.type;
          receive_event.media_type = media_type;
          receive_event.rtp_timestamp = frame_log_message.rtp_timestamp_;
          receive_event.packet_id = event_log_message.packet_id;
          break;
        }
        case FRAME_ACK_SENT:
        case FRAME_DECODED:
        case FRAME_PLAYOUT: {
          recent_frame_events_.push_back(FrameEvent());
          FrameEvent& frame_event = recent_frame_events_.back();
          frame_event.timestamp = event_log_message.event_timestamp;
          frame_event.type = event_log_message.type;
          frame_event.media_type = media_type;
          frame_event.rtp_timestamp = frame_log_message.rtp_timestamp_;
          if (event_log_message.type == FRAME_PLAYOUT)
            frame_event.delay_delta = event_log_message.delay_delta;
          break;
        }
        default:
          VLOG(2) << "Received log message via RTCP that we did not expect: "
                  << event_log_message.type;
          break;
      }
    }
  }
}

void CastTransportSenderImpl::OnReceivedCastMessage(
    uint32_t ssrc,
    const RtcpCastMessageCallback& cast_message_cb,
    const RtcpCastMessage& cast_message) {
  if (!cast_message_cb.is_null())
    cast_message_cb.Run(cast_message);

  DedupInfo dedup_info;
  if (audio_sender_ && audio_sender_->ssrc() == ssrc) {
    const int64_t acked_bytes =
        audio_sender_->GetLastByteSentForFrame(cast_message.ack_frame_id);
    last_byte_acked_for_audio_ =
        std::max(acked_bytes, last_byte_acked_for_audio_);
  } else if (video_sender_ && video_sender_->ssrc() == ssrc) {
    dedup_info.resend_interval = video_rtcp_session_->current_round_trip_time();

    // Only use audio stream to dedup if there is one.
    if (audio_sender_) {
      dedup_info.last_byte_acked_for_audio = last_byte_acked_for_audio_;
    }
  }

  if (!cast_message.missing_frames_and_packets.empty()) {
    VLOG(2) << "feedback_count: "
            << static_cast<uint32_t>(cast_message.feedback_count);
    // This call does two things.
    // 1. Specifies that retransmissions for packets not listed in the set are
    //    cancelled.
    // 2. Specifies a deduplication window. For video this would be the most
    //    recent RTT. For audio there is no deduplication.
    ResendPackets(ssrc, cast_message.missing_frames_and_packets, true,
                  dedup_info);
  }

  if (!cast_message.received_later_frames.empty()) {
    // Cancel resending frames that were received by the RTP receiver.
    CancelSendingFrames(ssrc, cast_message.received_later_frames);
  }
}

void CastTransportSenderImpl::AddValidRtpReceiver(uint32_t rtp_sender_ssrc,
                                                  uint32_t rtp_receiver_ssrc) {
  valid_sender_ssrcs_.insert(rtp_sender_ssrc);
  valid_rtp_receiver_ssrcs_.insert(rtp_receiver_ssrc);
}

void CastTransportSenderImpl::SetOptions(const base::DictionaryValue& options) {
  // Set PacedSender options.
  int burst_size = LookupOptionWithDefault(options, kOptionPacerTargetBurstSize,
                                           media::cast::kTargetBurstSize);
  if (burst_size != media::cast::kTargetBurstSize)
    pacer_.SetTargetBurstSize(burst_size);
  burst_size = LookupOptionWithDefault(options, kOptionPacerMaxBurstSize,
                                       media::cast::kMaxBurstSize);
  if (burst_size != media::cast::kMaxBurstSize)
    pacer_.SetMaxBurstSize(burst_size);

  // Set Wifi options.
  int wifi_options = 0;
  if (options.HasKey(kOptionWifiDisableScan)) {
    wifi_options |= net::WIFI_OPTIONS_DISABLE_SCAN;
  }
  if (options.HasKey(kOptionWifiMediaStreamingMode)) {
    wifi_options |= net::WIFI_OPTIONS_MEDIA_STREAMING_MODE;
  }
  if (wifi_options)
    wifi_options_autoreset_ = net::SetWifiOptions(wifi_options);
}

void CastTransportSenderImpl::InitializeRtpReceiverRtcpBuilder(
    uint32_t rtp_receiver_ssrc,
    const RtcpTimeData& time_data) {
  if (valid_rtp_receiver_ssrcs_.find(rtp_receiver_ssrc) ==
      valid_rtp_receiver_ssrcs_.end()) {
    VLOG(1) << "Invalid RTP receiver ssrc in "
            << "CastTransportSenderImpl::InitializeRtpReceiverRtcpBuilder.";
    return;
  }
  if (rtcp_builder_at_rtp_receiver_) {
    VLOG(1) << "Re-initialize rtcp_builder_at_rtp_receiver_ in "
               "CastTransportSenderImpl.";
    return;
  }
  rtcp_builder_at_rtp_receiver_.reset(new RtcpBuilder(rtp_receiver_ssrc));
  rtcp_builder_at_rtp_receiver_->Start();
  RtcpReceiverReferenceTimeReport rrtr;
  rrtr.ntp_seconds = time_data.ntp_seconds;
  rrtr.ntp_fraction = time_data.ntp_fraction;
  rtcp_builder_at_rtp_receiver_->AddRrtr(rrtr);
}

void CastTransportSenderImpl::AddCastFeedback(
    const RtcpCastMessage& cast_message,
    base::TimeDelta target_delay) {
  if (!rtcp_builder_at_rtp_receiver_) {
    VLOG(1) << "rtcp_builder_at_rtp_receiver_ is not initialized before "
               "calling CastTransportSenderImpl::AddCastFeedback.";
    return;
  }
  rtcp_builder_at_rtp_receiver_->AddCast(cast_message, target_delay);
}

void CastTransportSenderImpl::AddPli(const RtcpPliMessage& pli_message) {
  if (!rtcp_builder_at_rtp_receiver_) {
    VLOG(1) << "rtcp_builder_at_rtp_receiver_ is not initialized before "
               "calling CastTransportSenderImpl::AddPli.";
    return;
  }
  rtcp_builder_at_rtp_receiver_->AddPli(pli_message);
}

void CastTransportSenderImpl::AddRtcpEvents(
    const ReceiverRtcpEventSubscriber::RtcpEvents& rtcp_events) {
  if (!rtcp_builder_at_rtp_receiver_) {
    VLOG(1) << "rtcp_builder_at_rtp_receiver_ is not initialized before "
               "calling CastTransportSenderImpl::AddRtcpEvents.";
    return;
  }
  rtcp_builder_at_rtp_receiver_->AddReceiverLog(rtcp_events);
}

void CastTransportSenderImpl::AddRtpReceiverReport(
    const RtcpReportBlock& rtp_receiver_report_block) {
  if (!rtcp_builder_at_rtp_receiver_) {
    VLOG(1) << "rtcp_builder_at_rtp_receiver_ is not initialized before "
               "calling CastTransportSenderImpl::AddRtpReceiverReport.";
    return;
  }
  rtcp_builder_at_rtp_receiver_->AddRR(&rtp_receiver_report_block);
}

void CastTransportSenderImpl::SendRtcpFromRtpReceiver() {
  if (!rtcp_builder_at_rtp_receiver_) {
    VLOG(1) << "rtcp_builder_at_rtp_receiver_ is not initialized before "
               "calling CastTransportSenderImpl::SendRtcpFromRtpReceiver.";
    return;
  }
  pacer_.SendRtcpPacket(rtcp_builder_at_rtp_receiver_->local_ssrc(),
                        rtcp_builder_at_rtp_receiver_->Finish());
  rtcp_builder_at_rtp_receiver_.reset();
}

}  // namespace cast
}  // namespace media
