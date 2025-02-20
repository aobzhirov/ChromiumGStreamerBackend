// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ChromotingClient is the controller for the Client implementation.

#ifndef REMOTING_CLIENT_CHROMOTING_CLIENT_H_
#define REMOTING_CLIENT_CHROMOTING_CLIENT_H_

#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "remoting/protocol/client_authentication_config.h"
#include "remoting/protocol/client_stub.h"
#include "remoting/protocol/clipboard_stub.h"
#include "remoting/protocol/connection_to_host.h"
#include "remoting/protocol/input_stub.h"
#include "remoting/protocol/performance_tracker.h"
#include "remoting/protocol/session_config.h"
#include "remoting/protocol/video_stub.h"
#include "remoting/signaling/signal_strategy.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace remoting {

namespace protocol {
class CandidateSessionConfig;
class SessionManager;
class TransportContext;
class VideoRenderer;
}  // namespace protocol

class AudioDecodeScheduler;
class AudioPlayer;
class ClientContext;
class ClientUserInterface;
class FrameConsumerProxy;

class ChromotingClient : public SignalStrategy::Listener,
                         public protocol::ConnectionToHost::HostEventCallback,
                         public protocol::ClientStub {
 public:
  // |client_context|, |user_interface| and |video_renderer| must outlive the
  // client. |audio_player| may be null, in which case audio will not be
  // requested.
  ChromotingClient(ClientContext* client_context,
                   ClientUserInterface* user_interface,
                   protocol::VideoRenderer* video_renderer,
                   scoped_ptr<AudioPlayer> audio_player);

  ~ChromotingClient() override;

  void set_protocol_config(scoped_ptr<protocol::CandidateSessionConfig> config);

  // Used to set fake/mock objects for tests which use the ChromotingClient.
  void SetConnectionToHostForTests(
      scoped_ptr<protocol::ConnectionToHost> connection_to_host);

  // Start the client. Must be called on the main thread. |signal_strategy|
  // must outlive the client.
  void Start(SignalStrategy* signal_strategy,
             const protocol::ClientAuthenticationConfig& client_auth_config,
             scoped_refptr<protocol::TransportContext> transport_context,
             const std::string& host_jid,
             const std::string& capabilities);

  protocol::ConnectionToHost::State connection_state() const {
    return connection_->state();
  }

  protocol::ClipboardStub* clipboard_forwarder() {
    return connection_->clipboard_forwarder();
  }
  protocol::HostStub* host_stub() { return connection_->host_stub(); }
  protocol::InputStub* input_stub() { return connection_->input_stub(); }

  // ClientStub implementation.
  void SetCapabilities(const protocol::Capabilities& capabilities) override;
  void SetPairingResponse(
      const protocol::PairingResponse& pairing_response) override;
  void DeliverHostMessage(const protocol::ExtensionMessage& message) override;
  void SetVideoLayout(const protocol::VideoLayout& layout) override;

  // ClipboardStub implementation for receiving clipboard data from host.
  void InjectClipboardEvent(const protocol::ClipboardEvent& event) override;

  // CursorShapeStub implementation for receiving cursor shape updates.
  void SetCursorShape(const protocol::CursorShapeInfo& cursor_shape) override;

  // ConnectionToHost::HostEventCallback implementation.
  void OnConnectionState(protocol::ConnectionToHost::State state,
                         protocol::ErrorCode error) override;
  void OnConnectionReady(bool ready) override;
  void OnRouteChanged(const std::string& channel_name,
                      const protocol::TransportRoute& route) override;

 private:
   // SignalStrategy::StatusObserver interface.
  void OnSignalStrategyStateChange(SignalStrategy::State state) override;
  bool OnSignalStrategyIncomingStanza(const buzz::XmlElement* stanza) override;

  // Starts connection once |signal_strategy_| is connected.
  void StartConnection();

  // Called when the connection is authenticated.
  void OnAuthenticated();

  // Called when all channels are connected.
  void OnChannelsConnected();

  base::ThreadChecker thread_checker_;

  scoped_ptr<protocol::CandidateSessionConfig> protocol_config_;

  // The following are not owned by this class.
  ClientUserInterface* user_interface_ = nullptr;
  protocol::VideoRenderer* video_renderer_ = nullptr;
  SignalStrategy* signal_strategy_ = nullptr;

  std::string host_jid_;
  protocol::ClientAuthenticationConfig client_auth_config_;
  scoped_refptr<protocol::TransportContext> transport_context_;

  scoped_ptr<protocol::SessionManager> session_manager_;
  scoped_ptr<protocol::ConnectionToHost> connection_;

  scoped_ptr<AudioDecodeScheduler> audio_decode_scheduler_;

  std::string local_capabilities_;

  // The set of all capabilities supported by the host.
  std::string host_capabilities_;

  // True if |protocol::Capabilities| message has been received.
  bool host_capabilities_received_ = false;

  // Record the statistics of the connection.
  protocol::PerformanceTracker perf_tracker_;

  DISALLOW_COPY_AND_ASSIGN(ChromotingClient);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_CHROMOTING_CLIENT_H_
