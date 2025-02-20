// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_IPC_CLIENT_H_
#define REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_IPC_CLIENT_H_

#include <string>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "ipc/ipc_listener.h"

namespace IPC {
class Channel;
class Message;
}  // IPC

namespace remoting {

// Responsible for handing the client end of the IPC channel between the
// the network process (server) and remote_security_key process (client).
// The public methods are virtual to allow for using fake objects for testing.
class RemoteSecurityKeyIpcClient : public IPC::Listener {
 public:
  RemoteSecurityKeyIpcClient();
  ~RemoteSecurityKeyIpcClient() override;

  // Used to send gnubby extension messages to the client.
  typedef base::Callback<void(const std::string& response_data)>
      ResponseCallback;

  // Returns true if there is an active remoting session which supports
  // security key request forwarding.
  virtual bool WaitForSecurityKeyIpcServerChannel();

  // Begins the process of connecting to the IPC channel which will be used for
  // exchanging security key messages.
  // |connection_ready_callback| is called when a channel has been established
  // and security key requests can be sent.
  // |connection_error_callback| is stored and will be called back for any
  // unexpected errors that occur while establishing, or during, the session.
  virtual void EstablishIpcConnection(
      const base::Closure& connection_ready_callback,
      const base::Closure& connection_error_callback);

  // Sends a security key request message to the network process to be forwarded
  // to the remote client.
  virtual bool SendSecurityKeyRequest(
      const std::string& request_payload,
      const ResponseCallback& response_callback);

  // Closes the IPC channel if connected.
  virtual void CloseIpcConnection();

  // Allows tests to override the initial IPC channel used to retrieve IPC
  // connection details.
  void SetInitialIpcChannelNameForTest(
      const std::string& initial_ipc_channel_name);

  // Allows tests to override the expected session ID.
  void SetExpectedIpcServerSessionIdForTest(uint32_t expected_session_id);

 private:
  // IPC::Listener implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void OnChannelConnected(int32_t peer_pid) override;
  void OnChannelError() override;

  // Handles the ConnectionDetails IPC message.
  void OnConnectionDetails(const std::string& request_data);

  // Handles security key response IPC messages.
  void OnSecurityKeyResponse(const std::string& request_data);

  // Establishes a connection to the specified IPC Server channel.
  void ConnectToIpcChannel(const std::string& channel_name);

  // Used to validate the IPC Server process is running in the correct session.
  // '0' (default) corresponds to the session the network process runs in.
  uint32_t expected_ipc_server_session_id_ = 0;

  // Name for the IPC channel used for exchanging security key messages.
  std::string ipc_channel_name_;

  // Name of the initial IPC channel used to retrieve connection info.
  std::string initial_ipc_channel_name_;

  // Signaled when the IPC connection is ready for security key requests.
  base::Closure connection_ready_callback_;

  // Signaled when an error occurs in either the IPC channel or communication.
  base::Closure connection_error_callback_;

  // Signaled when a security key response has been received.
  ResponseCallback response_callback_;

  // Used for sending/receiving security key messages between processes.
  scoped_ptr<IPC::Channel> ipc_channel_;

  base::ThreadChecker thread_checker_;

  base::WeakPtrFactory<RemoteSecurityKeyIpcClient> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(RemoteSecurityKeyIpcClient);
};

}  // namespace remoting

#endif  // REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_IPC_CLIENT_H_
