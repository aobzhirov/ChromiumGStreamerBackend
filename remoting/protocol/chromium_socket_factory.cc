// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/chromium_socket_factory.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "jingle/glue/utils.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/udp/udp_server_socket.h"
#include "remoting/protocol/socket_util.h"
#include "third_party/webrtc/base/asyncpacketsocket.h"
#include "third_party/webrtc/base/nethelpers.h"
#include "third_party/webrtc/media/base/rtputils.h"

namespace remoting {
namespace protocol {

namespace {

// Size of the buffer to allocate for RecvFrom().
const int kReceiveBufferSize = 65536;

// Maximum amount of data in the send buffers. This is necessary to
// prevent out-of-memory crashes if the caller sends data faster than
// Pepper's UDP API can handle it. This maximum should never be
// reached under normal conditions.
const int kMaxSendBufferSize = 256 * 1024;

class UdpPacketSocket : public rtc::AsyncPacketSocket {
 public:
  UdpPacketSocket();
  ~UdpPacketSocket() override;

  bool Init(const rtc::SocketAddress& local_address,
            uint16_t min_port,
            uint16_t max_port);

  // rtc::AsyncPacketSocket interface.
  rtc::SocketAddress GetLocalAddress() const override;
  rtc::SocketAddress GetRemoteAddress() const override;
  int Send(const void* data,
           size_t data_size,
           const rtc::PacketOptions& options) override;
  int SendTo(const void* data,
             size_t data_size,
             const rtc::SocketAddress& address,
             const rtc::PacketOptions& options) override;
  int Close() override;
  State GetState() const override;
  int GetOption(rtc::Socket::Option option, int* value) override;
  int SetOption(rtc::Socket::Option option, int value) override;
  int GetError() const override;
  void SetError(int error) override;

 private:
  struct PendingPacket {
    PendingPacket(const void* buffer,
                  int buffer_size,
                  const net::IPEndPoint& address);

    scoped_refptr<net::IOBufferWithSize> data;
    net::IPEndPoint address;
    bool retried;
  };

  void OnBindCompleted(int error);

  void DoSend();
  void OnSendCompleted(int result);

  void DoRead();
  void OnReadCompleted(int result);
  void HandleReadResult(int result);

  scoped_ptr<net::UDPServerSocket> socket_;

  State state_;
  int error_;

  rtc::SocketAddress local_address_;

  // Receive buffer and address are populated by asynchronous reads.
  scoped_refptr<net::IOBuffer> receive_buffer_;
  net::IPEndPoint receive_address_;

  bool send_pending_;
  std::list<PendingPacket> send_queue_;
  int send_queue_size_;

  DISALLOW_COPY_AND_ASSIGN(UdpPacketSocket);
};

UdpPacketSocket::PendingPacket::PendingPacket(
    const void* buffer,
    int buffer_size,
    const net::IPEndPoint& address)
    : data(new net::IOBufferWithSize(buffer_size)),
      address(address),
      retried(false) {
  memcpy(data->data(), buffer, buffer_size);
}

UdpPacketSocket::UdpPacketSocket()
    : state_(STATE_CLOSED),
      error_(0),
      send_pending_(false),
      send_queue_size_(0) {
}

UdpPacketSocket::~UdpPacketSocket() {
  Close();
}

bool UdpPacketSocket::Init(const rtc::SocketAddress& local_address,
                           uint16_t min_port,
                           uint16_t max_port) {
  net::IPEndPoint local_endpoint;
  if (!jingle_glue::SocketAddressToIPEndPoint(
          local_address, &local_endpoint)) {
    return false;
  }

  for (uint32_t port = min_port; port <= max_port; ++port) {
    socket_.reset(new net::UDPServerSocket(nullptr, net::NetLog::Source()));
    int result = socket_->Listen(
        net::IPEndPoint(local_endpoint.address(), static_cast<uint16_t>(port)));
    if (result == net::OK) {
      break;
    } else {
      socket_.reset();
    }
  }

  if (!socket_.get()) {
    // Failed to bind the socket.
    return false;
  }

  if (socket_->GetLocalAddress(&local_endpoint) != net::OK ||
      !jingle_glue::IPEndPointToSocketAddress(local_endpoint,
                                              &local_address_)) {
    return false;
  }

  state_ = STATE_BOUND;
  DoRead();

  return true;
}

rtc::SocketAddress UdpPacketSocket::GetLocalAddress() const {
  DCHECK_EQ(state_, STATE_BOUND);
  return local_address_;
}

rtc::SocketAddress UdpPacketSocket::GetRemoteAddress() const {
  // UDP sockets are not connected - this method should never be called.
  NOTREACHED();
  return rtc::SocketAddress();
}

int UdpPacketSocket::Send(const void* data, size_t data_size,
                          const rtc::PacketOptions& options) {
  // UDP sockets are not connected - this method should never be called.
  NOTREACHED();
  return EWOULDBLOCK;
}

int UdpPacketSocket::SendTo(const void* data, size_t data_size,
                            const rtc::SocketAddress& address,
                            const rtc::PacketOptions& options) {
  if (state_ != STATE_BOUND) {
    NOTREACHED();
    return EINVAL;
  }

  if (error_ != 0) {
    return error_;
  }

  net::IPEndPoint endpoint;
  if (!jingle_glue::SocketAddressToIPEndPoint(address, &endpoint)) {
    return EINVAL;
  }

  if (send_queue_size_ >= kMaxSendBufferSize) {
    return EWOULDBLOCK;
  }

  PendingPacket packet(data, data_size, endpoint);
  cricket::ApplyPacketOptions(
      reinterpret_cast<uint8_t*>(packet.data->data()), data_size,
      options.packet_time_params,
      (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds());
  send_queue_.push_back(packet);
  send_queue_size_ += data_size;

  DoSend();
  return data_size;
}

int UdpPacketSocket::Close() {
  state_ = STATE_CLOSED;
  socket_.reset();
  return 0;
}

rtc::AsyncPacketSocket::State UdpPacketSocket::GetState() const {
  return state_;
}

int UdpPacketSocket::GetOption(rtc::Socket::Option option, int* value) {
  // This method is never called by libjingle.
  NOTIMPLEMENTED();
  return -1;
}

int UdpPacketSocket::SetOption(rtc::Socket::Option option, int value) {
  if (state_ != STATE_BOUND) {
    NOTREACHED();
    return EINVAL;
  }

  switch (option) {
    case rtc::Socket::OPT_DONTFRAGMENT:
      NOTIMPLEMENTED();
      return -1;

    case rtc::Socket::OPT_RCVBUF: {
      int net_error = socket_->SetReceiveBufferSize(value);
      return (net_error == net::OK) ? 0 : -1;
    }

    case rtc::Socket::OPT_SNDBUF: {
      int net_error = socket_->SetSendBufferSize(value);
      return (net_error == net::OK) ? 0 : -1;
    }

    case rtc::Socket::OPT_NODELAY:
      // OPT_NODELAY is only for TCP sockets.
      NOTREACHED();
      return -1;

    case rtc::Socket::OPT_IPV6_V6ONLY:
      NOTIMPLEMENTED();
      return -1;

    case rtc::Socket::OPT_DSCP:
      NOTIMPLEMENTED();
      return -1;

    case rtc::Socket::OPT_RTP_SENDTIME_EXTN_ID:
      NOTIMPLEMENTED();
      return -1;
  }

  NOTREACHED();
  return -1;
}

int UdpPacketSocket::GetError() const {
  return error_;
}

void UdpPacketSocket::SetError(int error) {
  error_ = error;
}

void UdpPacketSocket::DoSend() {
  if (send_pending_ || send_queue_.empty())
    return;

  PendingPacket& packet = send_queue_.front();
  int result = socket_->SendTo(
      packet.data.get(),
      packet.data->size(),
      packet.address,
      base::Bind(&UdpPacketSocket::OnSendCompleted, base::Unretained(this)));
  if (result == net::ERR_IO_PENDING) {
    send_pending_ = true;
  } else {
    OnSendCompleted(result);
  }
}

void UdpPacketSocket::OnSendCompleted(int result) {
  send_pending_ = false;

  if (result < 0) {
    SocketErrorAction action = GetSocketErrorAction(result);
    switch (action) {
      case SOCKET_ERROR_ACTION_FAIL:
        LOG(ERROR) << "Send failed on a UDP socket: " << result;
        error_ = EINVAL;
        return;

      case SOCKET_ERROR_ACTION_RETRY:
        // Retry resending only once.
        if (!send_queue_.front().retried) {
          send_queue_.front().retried = true;
          DoSend();
          return;
        }
        break;

      case SOCKET_ERROR_ACTION_IGNORE:
        break;
    }
  }

  // Don't need to worry about partial sends because this is a datagram
  // socket.
  send_queue_size_ -= send_queue_.front().data->size();
  send_queue_.pop_front();
  DoSend();
}

void UdpPacketSocket::DoRead() {
  int result = 0;
  while (result >= 0) {
    receive_buffer_ = new net::IOBuffer(kReceiveBufferSize);
    result = socket_->RecvFrom(
        receive_buffer_.get(),
        kReceiveBufferSize,
        &receive_address_,
        base::Bind(&UdpPacketSocket::OnReadCompleted, base::Unretained(this)));
    HandleReadResult(result);
  }
}

void UdpPacketSocket::OnReadCompleted(int result) {
  HandleReadResult(result);
  if (result >= 0) {
    DoRead();
  }
}

void UdpPacketSocket::HandleReadResult(int result) {
  if (result == net::ERR_IO_PENDING) {
    return;
  }

  if (result > 0) {
    rtc::SocketAddress address;
    if (!jingle_glue::IPEndPointToSocketAddress(receive_address_, &address)) {
      NOTREACHED();
      LOG(ERROR) << "Failed to convert address received from RecvFrom().";
      return;
    }
    SignalReadPacket(this, receive_buffer_->data(), result, address,
                     rtc::CreatePacketTime(0));
  } else {
    LOG(ERROR) << "Received error when reading from UDP socket: " << result;
  }
}

}  // namespace

ChromiumPacketSocketFactory::ChromiumPacketSocketFactory() {
}

ChromiumPacketSocketFactory::~ChromiumPacketSocketFactory() {
}

rtc::AsyncPacketSocket* ChromiumPacketSocketFactory::CreateUdpSocket(
    const rtc::SocketAddress& local_address,
    uint16_t min_port,
    uint16_t max_port) {
  scoped_ptr<UdpPacketSocket> result(new UdpPacketSocket());
  if (!result->Init(local_address, min_port, max_port))
    return nullptr;
  return result.release();
}

rtc::AsyncPacketSocket* ChromiumPacketSocketFactory::CreateServerTcpSocket(
    const rtc::SocketAddress& local_address,
    uint16_t min_port,
    uint16_t max_port,
    int opts) {
  // TCP sockets are not supported.
  // TODO(sergeyu): Implement TCP support crbug.com/600032 .
  NOTIMPLEMENTED();
  return nullptr;
}

rtc::AsyncPacketSocket*
ChromiumPacketSocketFactory::CreateClientTcpSocket(
      const rtc::SocketAddress& local_address,
      const rtc::SocketAddress& remote_address,
      const rtc::ProxyInfo& proxy_info,
      const std::string& user_agent,
      int opts) {
  // TCP sockets are not supported.
  // TODO(sergeyu): Implement TCP support crbug.com/600032 .
  NOTIMPLEMENTED();
  return nullptr;
}

rtc::AsyncResolverInterface*
ChromiumPacketSocketFactory::CreateAsyncResolver() {
  return new rtc::AsyncResolver();
}

}  // namespace protocol
}  // namespace remoting
