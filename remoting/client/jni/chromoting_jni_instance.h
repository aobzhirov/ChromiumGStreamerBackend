// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_CHROMOTING_JNI_INSTANCE_H_
#define REMOTING_CLIENT_CHROMOTING_JNI_INSTANCE_H_

#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "remoting/client/chromoting_client.h"
#include "remoting/client/client_context.h"
#include "remoting/client/client_user_interface.h"
#include "remoting/proto/control.pb.h"
#include "remoting/proto/event.pb.h"
#include "remoting/protocol/clipboard_stub.h"
#include "remoting/protocol/cursor_shape_stub.h"
#include "remoting/signaling/xmpp_signal_strategy.h"

namespace remoting {

namespace protocol {
class ClipboardEvent;
class CursorShapeInfo;
class PerformanceTracker;
class VideoRenderer;
}  // namespace protocol

class ChromotingJniRuntime;
class ClientStatusLogger;
class JniFrameConsumer;

// ClientUserInterface that indirectly makes and receives JNI calls.
class ChromotingJniInstance
  : public ClientUserInterface,
    public protocol::ClipboardStub,
    public protocol::CursorShapeStub,
    public base::RefCountedThreadSafe<ChromotingJniInstance> {
 public:
  // Initiates a connection with the specified host. Call from the UI thread.
  // The instance does not take ownership of |jni_runtime|. To connect with an
  // unpaired host, pass in |pairing_id| and |pairing_secret| as empty strings.
  ChromotingJniInstance(ChromotingJniRuntime* jni_runtime,
                        const std::string& username,
                        const std::string& auth_token,
                        const std::string& host_jid,
                        const std::string& host_id,
                        const std::string& host_pubkey,
                        const std::string& pairing_id,
                        const std::string& pairing_secret,
                        const std::string& capabilities,
                        const std::string& flags);

  // Terminates the current connection (if it hasn't already failed) and cleans
  // up. Must be called before destruction.
  void Disconnect();

  // Requests the android app to fetch a third-party token.
  void FetchThirdPartyToken(
      const std::string& host_public_key,
      const std::string& token_url,
      const std::string& scope,
      const protocol::ThirdPartyTokenFetchedCallback& token_fetched_callback);

  // Called by the android app when the token is fetched.
  void HandleOnThirdPartyTokenFetched(const std::string& token,
                                      const std::string& shared_secret);

  // Provides the user's PIN and resumes the host authentication attempt. Call
  // on the UI thread once the user has finished entering this PIN into the UI,
  // but only after the UI has been asked to provide a PIN (via FetchSecret()).
  void ProvideSecret(const std::string& pin, bool create_pair,
                     const std::string& device_name);

  // Schedules a redraw on the display thread. May be called from any thread.
  void RedrawDesktop();

  // Moves the host's cursor to the specified coordinates, optionally with some
  // mouse button depressed. If |button| is BUTTON_UNDEFINED, no click is made.
  void SendMouseEvent(int x, int y,
                      protocol::MouseEvent_MouseButton button,
                      bool button_down);
  void SendMouseWheelEvent(int delta_x, int delta_y);

  // Sends the provided keyboard scan code to the host.
  bool SendKeyEvent(int scan_code, int key_code, bool key_down);

  void SendTextEvent(const std::string& text);

  // Sends the provided touch event payload to the host.
  void SendTouchEvent(const protocol::TouchEvent& touch_event);

  // Enables or disables the video channel. May be called from any thread.
  void EnableVideoChannel(bool enable);

  void SendClientMessage(const std::string& type, const std::string& data);

  // ClientUserInterface implementation.
  void OnConnectionState(protocol::ConnectionToHost::State state,
                         protocol::ErrorCode error) override;
  void OnConnectionReady(bool ready) override;
  void OnRouteChanged(const std::string& channel_name,
                      const protocol::TransportRoute& route) override;
  void SetCapabilities(const std::string& capabilities) override;
  void SetPairingResponse(const protocol::PairingResponse& response) override;
  void DeliverHostMessage(const protocol::ExtensionMessage& message) override;
  void SetDesktopSize(const webrtc::DesktopSize& size,
                      const webrtc::DesktopVector& dpi) override;
  protocol::ClipboardStub* GetClipboardStub() override;
  protocol::CursorShapeStub* GetCursorShapeStub() override;

  // CursorShapeStub implementation.
  void InjectClipboardEvent(const protocol::ClipboardEvent& event) override;

  // ClipboardStub implementation.
  void SetCursorShape(const protocol::CursorShapeInfo& shape) override;

 private:
  // This object is ref-counted, so it cleans itself up.
  ~ChromotingJniInstance() override;

  void ConnectToHostOnNetworkThread();

  // Notifies the user interface that the user needs to enter a PIN. The
  // current authentication attempt is put on hold until |callback| is invoked.
  // May be called on any thread.
  void FetchSecret(bool pairable,
                   const protocol::SecretFetchedCallback& callback);

  // Sets the device name. Can be called on any thread.
  void SetDeviceName(const std::string& device_name);

  void SendKeyEventInternal(int usb_key_code, bool key_down);

  // Enables or disables periodic logging of performance statistics. Called on
  // the network thread.
  void EnableStatsLogging(bool enabled);

  // If logging is enabled, logs the current connection statistics, and
  // triggers another call to this function after the logging time interval.
  // Called on the network thread.
  void LogPerfStats();

  // Used to obtain task runner references and make calls to Java methods.
  ChromotingJniRuntime* jni_runtime_;

  // ID of the host we are connecting to.
  std::string host_jid_;

  protocol::ClientAuthenticationConfig client_auth_config_;

  std::string flags_;

  // This group of variables is to be used on the network thread.
  scoped_ptr<ClientContext> client_context_;
  scoped_ptr<protocol::PerformanceTracker> perf_tracker_;
  scoped_ptr<JniFrameConsumer> view_;
  scoped_ptr<protocol::VideoRenderer> video_renderer_;
  scoped_ptr<ChromotingClient> client_;
  XmppSignalStrategy::XmppServerConfig xmpp_config_;
  scoped_ptr<XmppSignalStrategy> signaling_;  // Must outlive client_
  scoped_ptr<ClientStatusLogger> client_status_logger_;
  protocol::ThirdPartyTokenFetchedCallback third_party_token_fetched_callback_;

  // Pass this the user's PIN once we have it. To be assigned and accessed on
  // the UI thread, but must be posted to the network thread to call it.
  protocol::SecretFetchedCallback pin_callback_;

  // Indicates whether to establish a new pairing with this host. This is
  // modified in ProvideSecret(), but thereafter to be used only from the
  // network thread. (This is safe because ProvideSecret() is invoked at most
  // once per run, and always before any reference to this flag.)
  bool create_pairing_ = false;

  // The device name to appear in the paired-clients list. Accessed on the
  // network thread.
  std::string device_name_;

  // If this is true, performance statistics will be periodically written to
  // the Android log. Used on the network thread.
  bool stats_logging_enabled_ = false;

  // The set of capabilities supported by the client. Accessed on the network
  // thread. Once SetCapabilities() is called, this will contain the negotiated
  // set of capabilities for this remoting session.
  std::string capabilities_;

  friend class base::RefCountedThreadSafe<ChromotingJniInstance>;

  base::WeakPtrFactory<ChromotingJniInstance> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ChromotingJniInstance);
};

}  // namespace remoting

#endif
