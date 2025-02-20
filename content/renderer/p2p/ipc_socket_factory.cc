// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/p2p/ipc_socket_factory.h"

#include <stddef.h>

#include <algorithm>
#include <list>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/threading/non_thread_safe.h"
#include "base/trace_event/trace_event.h"
#include "content/renderer/media/webrtc_logging.h"
#include "content/renderer/p2p/host_address_request.h"
#include "content/renderer/p2p/socket_client_delegate.h"
#include "content/renderer/p2p/socket_client_impl.h"
#include "content/renderer/p2p/socket_dispatcher.h"
#include "jingle/glue/utils.h"
#include "net/base/ip_address.h"
#include "third_party/webrtc/base/asyncpacketsocket.h"

namespace content {

namespace {

const int kDefaultNonSetOptionValue = -1;

bool IsTcpClientSocket(P2PSocketType type) {
  return (type == P2P_SOCKET_STUN_TCP_CLIENT) ||
         (type == P2P_SOCKET_TCP_CLIENT) ||
         (type == P2P_SOCKET_STUN_SSLTCP_CLIENT) ||
         (type == P2P_SOCKET_SSLTCP_CLIENT) ||
         (type == P2P_SOCKET_TLS_CLIENT) ||
         (type == P2P_SOCKET_STUN_TLS_CLIENT);
}

bool JingleSocketOptionToP2PSocketOption(rtc::Socket::Option option,
                                         P2PSocketOption* ipc_option) {
  switch (option) {
    case rtc::Socket::OPT_RCVBUF:
      *ipc_option = P2P_SOCKET_OPT_RCVBUF;
      break;
    case rtc::Socket::OPT_SNDBUF:
      *ipc_option = P2P_SOCKET_OPT_SNDBUF;
      break;
    case rtc::Socket::OPT_DSCP:
      *ipc_option = P2P_SOCKET_OPT_DSCP;
      break;
    case rtc::Socket::OPT_DONTFRAGMENT:
    case rtc::Socket::OPT_NODELAY:
    case rtc::Socket::OPT_IPV6_V6ONLY:
    case rtc::Socket::OPT_RTP_SENDTIME_EXTN_ID:
      return false;  // Not supported by the chrome sockets.
    default:
      NOTREACHED();
      return false;
  }
  return true;
}

// TODO(miu): This needs tuning.  http://crbug.com/237960
// http://crbug.com/427555
const size_t kMaximumInFlightBytes = 64 * 1024;  // 64 KB

// IpcPacketSocket implements rtc::AsyncPacketSocket interface
// using P2PSocketClient that works over IPC-channel. It must be used
// on the thread it was created.
class IpcPacketSocket : public rtc::AsyncPacketSocket,
                        public P2PSocketClientDelegate {
 public:
  IpcPacketSocket();
  ~IpcPacketSocket() override;

  // Struct to track information when a packet is received by this socket for
  // send. The information tracked here will be used to match with the
  // P2PSendPacketMetrics from the underneath system socket.
  struct InFlightPacketRecord {
    InFlightPacketRecord(uint64_t packet_id, size_t packet_size)
        : packet_id(packet_id), packet_size(packet_size) {}

    uint64_t packet_id;
    size_t packet_size;
  };

  typedef std::list<InFlightPacketRecord> InFlightPacketList;

  // Always takes ownership of client even if initialization fails.
  bool Init(P2PSocketType type, P2PSocketClientImpl* client,
            const rtc::SocketAddress& local_address,
            const rtc::SocketAddress& remote_address);

  // rtc::AsyncPacketSocket interface.
  rtc::SocketAddress GetLocalAddress() const override;
  rtc::SocketAddress GetRemoteAddress() const override;
  int Send(const void* pv,
           size_t cb,
           const rtc::PacketOptions& options) override;
  int SendTo(const void* pv,
             size_t cb,
             const rtc::SocketAddress& addr,
             const rtc::PacketOptions& options) override;
  int Close() override;
  State GetState() const override;
  int GetOption(rtc::Socket::Option option, int* value) override;
  int SetOption(rtc::Socket::Option option, int value) override;
  int GetError() const override;
  void SetError(int error) override;

  // P2PSocketClientDelegate implementation.
  void OnOpen(const net::IPEndPoint& local_address,
              const net::IPEndPoint& remote_address) override;
  void OnIncomingTcpConnection(const net::IPEndPoint& address,
                               P2PSocketClient* client) override;
  void OnSendComplete(const P2PSendPacketMetrics& send_metrics) override;
  void OnError() override;
  void OnDataReceived(const net::IPEndPoint& address,
                      const std::vector<char>& data,
                      const base::TimeTicks& timestamp) override;

 private:
  enum InternalState {
    IS_UNINITIALIZED,
    IS_OPENING,
    IS_OPEN,
    IS_CLOSED,
    IS_ERROR,
  };

  // Increment the counter for consecutive bytes discarded as socket is running
  // out of buffer.
  void IncrementDiscardCounters(size_t bytes_discarded);

  // Update trace of send throttling internal state. This should be called
  // immediately after any changes to |send_bytes_available_| and/or
  // |in_flight_packet_records_|.
  void TraceSendThrottlingState() const;

  void InitAcceptedTcp(P2PSocketClient* client,
                       const rtc::SocketAddress& local_address,
                       const rtc::SocketAddress& remote_address);

  int DoSetOption(P2PSocketOption option, int value);

  // Allow a finch experiment to control the initial value of
  // send_bytes_available_;
  void AdjustUdpSendBufferSize();

  P2PSocketType type_;

  // Message loop on which this socket was created and being used.
  base::MessageLoop* message_loop_;

  // Corresponding P2P socket client.
  scoped_refptr<P2PSocketClient> client_;

  // Local address is allocated by the browser process, and the
  // renderer side doesn't know the address until it receives OnOpen()
  // event from the browser.
  rtc::SocketAddress local_address_;

  // Remote address for client TCP connections.
  rtc::SocketAddress remote_address_;

  // Current state of the object.
  InternalState state_;

  // Track the number of bytes allowed to be sent non-blocking. This is used to
  // throttle the sending of packets to the browser process. For each packet
  // sent, the value is decreased. As callbacks to OnSendComplete() (as IPCs
  // from the browser process) are made, the value is increased back. This
  // allows short bursts of high-rate sending without dropping packets, but
  // quickly restricts the client to a sustainable steady-state rate.
  size_t send_bytes_available_;

  // Used to detect when browser doesn't send SendComplete message for some
  // packets. In normal case, the first packet should be the one that we're
  // going to receive the next completion signal.
  InFlightPacketList in_flight_packet_records_;

  // Set to true once EWOULDBLOCK was returned from Send(). Indicates that the
  // caller expects SignalWritable notification.
  bool writable_signal_expected_;

  // Current error code. Valid when state_ == IS_ERROR.
  int error_;
  int options_[P2P_SOCKET_OPT_MAX];

  // Track the maximum and current consecutive bytes discarded due to not enough
  // send_bytes_available_.
  size_t max_discard_bytes_sequence_;
  size_t current_discard_bytes_sequence_;

  // Track the total number of packets and the number of packets discarded.
  size_t packets_discarded_;
  size_t total_packets_;

  DISALLOW_COPY_AND_ASSIGN(IpcPacketSocket);
};

// Simple wrapper around P2PAsyncAddressResolver. The main purpose of this
// class is to send SignalDone, after OnDone callback from
// P2PAsyncAddressResolver. Libjingle sig slots are not thread safe. In case
// of MT sig slots clients must call disconnect. This class is to make sure
// we destruct from the same thread on which is created.
class AsyncAddressResolverImpl :  public base::NonThreadSafe,
                                  public rtc::AsyncResolverInterface {
 public:
  AsyncAddressResolverImpl(P2PSocketDispatcher* dispatcher);
  ~AsyncAddressResolverImpl() override;

  // rtc::AsyncResolverInterface interface.
  void Start(const rtc::SocketAddress& addr) override;
  bool GetResolvedAddress(int family, rtc::SocketAddress* addr) const override;
  int GetError() const override;
  void Destroy(bool wait) override;

 private:
  virtual void OnAddressResolved(const net::IPAddressList& addresses);

  scoped_refptr<P2PAsyncAddressResolver> resolver_;
  int port_;   // Port number in |addr| from Start() method.
  std::vector<rtc::IPAddress> addresses_;  // Resolved addresses.
};

IpcPacketSocket::IpcPacketSocket()
    : type_(P2P_SOCKET_UDP),
      message_loop_(base::MessageLoop::current()),
      state_(IS_UNINITIALIZED),
      send_bytes_available_(kMaximumInFlightBytes),
      writable_signal_expected_(false),
      error_(0),
      max_discard_bytes_sequence_(0),
      current_discard_bytes_sequence_(0),
      packets_discarded_(0),
      total_packets_(0) {
  static_assert(kMaximumInFlightBytes > 0, "would send at zero rate");
  std::fill_n(options_, static_cast<int> (P2P_SOCKET_OPT_MAX),
              kDefaultNonSetOptionValue);
}

IpcPacketSocket::~IpcPacketSocket() {
  if (state_ == IS_OPENING || state_ == IS_OPEN ||
      state_ == IS_ERROR) {
    Close();
  }

  UMA_HISTOGRAM_CUSTOM_COUNTS("WebRTC.ApplicationMaxConsecutiveBytesDiscard.v2",
                              max_discard_bytes_sequence_, 1, 1000000, 200);
  if (total_packets_ > 0) {
    UMA_HISTOGRAM_PERCENTAGE("WebRTC.ApplicationPercentPacketsDiscarded",
                             (packets_discarded_ * 100) / total_packets_);
  }
}

void IpcPacketSocket::TraceSendThrottlingState() const {
  TRACE_COUNTER_ID1("p2p", "P2PSendBytesAvailable", local_address_.port(),
                    send_bytes_available_);
  TRACE_COUNTER_ID1("p2p", "P2PSendPacketsInFlight", local_address_.port(),
                    in_flight_packet_records_.size());
}

void IpcPacketSocket::IncrementDiscardCounters(size_t bytes_discarded) {
  current_discard_bytes_sequence_ += bytes_discarded;
  packets_discarded_++;

  if (current_discard_bytes_sequence_ > max_discard_bytes_sequence_) {
    max_discard_bytes_sequence_ = current_discard_bytes_sequence_;
  }
}

void IpcPacketSocket::AdjustUdpSendBufferSize() {
  DCHECK_EQ(type_, P2P_SOCKET_UDP);
  unsigned int send_buffer_size = 0;

  base::StringToUint(
      base::FieldTrialList::FindFullName("WebRTC-ApplicationUDPSendSocketSize"),
      &send_buffer_size);

  if (send_buffer_size > 0) {
    send_bytes_available_ = send_buffer_size;
  }
}

bool IpcPacketSocket::Init(P2PSocketType type,
                           P2PSocketClientImpl* client,
                           const rtc::SocketAddress& local_address,
                           const rtc::SocketAddress& remote_address) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  DCHECK_EQ(state_, IS_UNINITIALIZED);

  type_ = type;
  client_ = client;
  local_address_ = local_address;
  remote_address_ = remote_address;
  state_ = IS_OPENING;

  net::IPEndPoint local_endpoint;
  if (!jingle_glue::SocketAddressToIPEndPoint(
          local_address, &local_endpoint)) {
    return false;
  }

  if (type_ == P2P_SOCKET_UDP) {
    AdjustUdpSendBufferSize();
  }

  net::IPEndPoint remote_endpoint;
  if (!remote_address.IsNil()) {
    DCHECK(IsTcpClientSocket(type_));

    if (remote_address.IsUnresolvedIP()) {
      remote_endpoint =
          net::IPEndPoint(net::IPAddress(), remote_address.port());
    } else {
      if (!jingle_glue::SocketAddressToIPEndPoint(remote_address,
                                                  &remote_endpoint)) {
        return false;
      }
    }
  }

  // We need to send both resolved and unresolved address in Init. Unresolved
  // address will be used in case of TLS for certificate hostname matching.
  // Certificate will be tied to domain name not to IP address.
  P2PHostAndIPEndPoint remote_info(remote_address.hostname(), remote_endpoint);

  client->Init(type, local_endpoint, remote_info, this);

  return true;
}

void IpcPacketSocket::InitAcceptedTcp(
    P2PSocketClient* client,
    const rtc::SocketAddress& local_address,
    const rtc::SocketAddress& remote_address) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  DCHECK_EQ(state_, IS_UNINITIALIZED);

  client_ = client;
  local_address_ = local_address;
  remote_address_ = remote_address;
  state_ = IS_OPEN;
  TraceSendThrottlingState();
  client_->SetDelegate(this);
}

// rtc::AsyncPacketSocket interface.
rtc::SocketAddress IpcPacketSocket::GetLocalAddress() const {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  return local_address_;
}

rtc::SocketAddress IpcPacketSocket::GetRemoteAddress() const {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  return remote_address_;
}

int IpcPacketSocket::Send(const void *data, size_t data_size,
                          const rtc::PacketOptions& options) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  return SendTo(data, data_size, remote_address_, options);
}

int IpcPacketSocket::SendTo(const void *data, size_t data_size,
                            const rtc::SocketAddress& address,
                            const rtc::PacketOptions& options) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  switch (state_) {
    case IS_UNINITIALIZED:
      NOTREACHED();
      error_ = EWOULDBLOCK;
      return -1;
    case IS_OPENING:
      error_ = EWOULDBLOCK;
      return -1;
    case IS_CLOSED:
      error_ = ENOTCONN;
      return -1;
    case IS_ERROR:
      return -1;
    case IS_OPEN:
      // Continue sending the packet.
      break;
  }

  if (data_size == 0) {
    NOTREACHED();
    return 0;
  }

  total_packets_++;

  if (data_size > send_bytes_available_) {
    TRACE_EVENT_INSTANT1("p2p", "MaxPendingBytesWouldBlock",
                         TRACE_EVENT_SCOPE_THREAD,
                         "id",
                         client_->GetSocketID());
    if (!writable_signal_expected_) {
      WebRtcLogMessage(base::StringPrintf(
          "IpcPacketSocket: sending is blocked. %d packets_in_flight.",
          static_cast<int>(in_flight_packet_records_.size())));

      writable_signal_expected_ = true;
    }

    error_ = EWOULDBLOCK;
    IncrementDiscardCounters(data_size);
    return -1;
  } else {
    current_discard_bytes_sequence_ = 0;
  }

  net::IPEndPoint address_chrome;
  if (address.IsUnresolvedIP()) {
    address_chrome = net::IPEndPoint(net::IPAddress(), address.port());
  } else {
    if (!jingle_glue::SocketAddressToIPEndPoint(address, &address_chrome)) {
      LOG(WARNING) << "Failed to convert remote address to IPEndPoint: address="
                   << address.ipaddr().ToSensitiveString()
                   << ", remote_address_="
                   << remote_address_.ipaddr().ToSensitiveString();
      NOTREACHED();
      error_ = EINVAL;
      return -1;
    }
  }

  send_bytes_available_ -= data_size;

  const char* data_char = reinterpret_cast<const char*>(data);
  std::vector<char> data_vector(data_char, data_char + data_size);
  uint64_t packet_id = client_->Send(address_chrome, data_vector, options);

  // Ensure packet_id is not 0. It can't be the case according to
  // P2PSocketClientImpl::Send().
  DCHECK_NE(packet_id, 0uL);

  in_flight_packet_records_.push_back(
      InFlightPacketRecord(packet_id, data_size));
  TraceSendThrottlingState();

  // Fake successful send. The caller ignores result anyway.
  return data_size;
}

int IpcPacketSocket::Close() {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  client_->Close();
  state_ = IS_CLOSED;

  return 0;
}

rtc::AsyncPacketSocket::State IpcPacketSocket::GetState() const {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  switch (state_) {
    case IS_UNINITIALIZED:
      NOTREACHED();
      return STATE_CLOSED;

    case IS_OPENING:
      return STATE_BINDING;

    case IS_OPEN:
      if (IsTcpClientSocket(type_)) {
        return STATE_CONNECTED;
      } else {
        return STATE_BOUND;
      }

    case IS_CLOSED:
    case IS_ERROR:
      return STATE_CLOSED;
  }

  NOTREACHED();
  return STATE_CLOSED;
}

int IpcPacketSocket::GetOption(rtc::Socket::Option option, int* value) {
  P2PSocketOption p2p_socket_option = P2P_SOCKET_OPT_MAX;
  if (!JingleSocketOptionToP2PSocketOption(option, &p2p_socket_option)) {
    // unsupported option.
    return -1;
  }

  *value = options_[p2p_socket_option];
  return 0;
}

int IpcPacketSocket::SetOption(rtc::Socket::Option option, int value) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  P2PSocketOption p2p_socket_option = P2P_SOCKET_OPT_MAX;
  if (!JingleSocketOptionToP2PSocketOption(option, &p2p_socket_option)) {
    // Option is not supported.
    return -1;
  }

  options_[p2p_socket_option] = value;

  if (state_ == IS_OPEN) {
    // Options will be applied when state becomes IS_OPEN in OnOpen.
    return DoSetOption(p2p_socket_option, value);
  }
  return 0;
}

int IpcPacketSocket::DoSetOption(P2PSocketOption option, int value) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  DCHECK_EQ(state_, IS_OPEN);

  client_->SetOption(option, value);
  return 0;
}

int IpcPacketSocket::GetError() const {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  return error_;
}

void IpcPacketSocket::SetError(int error) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  error_ = error;
}

void IpcPacketSocket::OnOpen(const net::IPEndPoint& local_address,
                             const net::IPEndPoint& remote_address) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  if (!jingle_glue::IPEndPointToSocketAddress(local_address, &local_address_)) {
    // Always expect correct IPv4 address to be allocated.
    NOTREACHED();
    OnError();
    return;
  }

  state_ = IS_OPEN;
  TraceSendThrottlingState();

  // Set all pending options if any.
  for (int i = 0; i < P2P_SOCKET_OPT_MAX; ++i) {
    if (options_[i] != kDefaultNonSetOptionValue)
      DoSetOption(static_cast<P2PSocketOption> (i), options_[i]);
  }

  SignalAddressReady(this, local_address_);
  if (IsTcpClientSocket(type_)) {
    // If remote address is unresolved, set resolved remote IP address received
    // in the callback. This address will be used while sending the packets
    // over the network.
    if (remote_address_.IsUnresolvedIP()) {
      rtc::SocketAddress jingle_socket_address;
      // |remote_address| could be unresolved if the connection is behind a
      // proxy.
      if (!remote_address.address().empty() &&
          jingle_glue::IPEndPointToSocketAddress(
              remote_address, &jingle_socket_address)) {
        // Set only the IP address.
        remote_address_.SetResolvedIP(jingle_socket_address.ipaddr());
      }
    }

    // SignalConnect after updating the |remote_address_| so that the listener
    // can get the resolved remote address.
    SignalConnect(this);
  }
}

void IpcPacketSocket::OnIncomingTcpConnection(
    const net::IPEndPoint& address,
    P2PSocketClient* client) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  scoped_ptr<IpcPacketSocket> socket(new IpcPacketSocket());

  rtc::SocketAddress remote_address;
  if (!jingle_glue::IPEndPointToSocketAddress(address, &remote_address)) {
    // Always expect correct IPv4 address to be allocated.
    NOTREACHED();
  }
  socket->InitAcceptedTcp(client, local_address_, remote_address);
  SignalNewConnection(this, socket.release());
}

void IpcPacketSocket::OnSendComplete(const P2PSendPacketMetrics& send_metrics) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  CHECK(!in_flight_packet_records_.empty());

  const InFlightPacketRecord& record = in_flight_packet_records_.front();

  // Tracking is not turned on for TCP so it's always 0. For UDP, this will
  // cause a crash when the packet ids don't match.
  CHECK(send_metrics.packet_id == 0 ||
        record.packet_id == send_metrics.packet_id);

  send_bytes_available_ += record.packet_size;

  DCHECK_LE(send_bytes_available_, kMaximumInFlightBytes);

  in_flight_packet_records_.pop_front();
  TraceSendThrottlingState();

  int64_t send_time_ms = -1;
  if (send_metrics.rtc_packet_id >= 0) {
    send_time_ms = (send_metrics.send_time - base::TimeTicks::UnixEpoch())
                       .InMilliseconds();
  }
  SignalSentPacket(this, rtc::SentPacket(send_metrics.rtc_packet_id,
                                         send_time_ms));

  if (writable_signal_expected_ && send_bytes_available_ > 0) {
    WebRtcLogMessage(base::StringPrintf(
        "IpcPacketSocket: sending is unblocked. %d packets in flight.",
        static_cast<int>(in_flight_packet_records_.size())));

    SignalReadyToSend(this);
    writable_signal_expected_ = false;
  }
}

void IpcPacketSocket::OnError() {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);
  bool was_closed = (state_ == IS_ERROR || state_ == IS_CLOSED);
  state_ = IS_ERROR;
  error_ = ECONNABORTED;
  if (!was_closed) {
    SignalClose(this, 0);
  }
}

void IpcPacketSocket::OnDataReceived(const net::IPEndPoint& address,
                                     const std::vector<char>& data,
                                     const base::TimeTicks& timestamp) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  rtc::SocketAddress address_lj;

  if (address.address().empty()) {
    DCHECK(IsTcpClientSocket(type_));
    // |address| could be empty for TCP connections behind a proxy.
    address_lj = remote_address_;
  } else {
    if (!jingle_glue::IPEndPointToSocketAddress(address, &address_lj)) {
      // We should always be able to convert address here because we
      // don't expect IPv6 address on IPv4 connections.
      NOTREACHED();
      return;
    }
  }

  rtc::PacketTime packet_time(timestamp.ToInternalValue(), 0);
  SignalReadPacket(this, &data[0], data.size(), address_lj,
                   packet_time);
}

AsyncAddressResolverImpl::AsyncAddressResolverImpl(
    P2PSocketDispatcher* dispatcher)
    : resolver_(new P2PAsyncAddressResolver(dispatcher)) {
}

AsyncAddressResolverImpl::~AsyncAddressResolverImpl() {
}

void AsyncAddressResolverImpl::Start(const rtc::SocketAddress& addr) {
  DCHECK(CalledOnValidThread());
  // Copy port number from |addr|. |port_| must be copied
  // when resolved address is returned in GetResolvedAddress.
  port_ = addr.port();

  resolver_->Start(addr, base::Bind(
      &AsyncAddressResolverImpl::OnAddressResolved,
      base::Unretained(this)));
}

bool AsyncAddressResolverImpl::GetResolvedAddress(
    int family, rtc::SocketAddress* addr) const {
  DCHECK(CalledOnValidThread());

  if (addresses_.empty())
   return false;

  for (size_t i = 0; i < addresses_.size(); ++i) {
    if (family == addresses_[i].family()) {
      addr->SetResolvedIP(addresses_[i]);
      addr->SetPort(port_);
      return true;
    }
  }
  return false;
}

int AsyncAddressResolverImpl::GetError() const {
  DCHECK(CalledOnValidThread());
  return addresses_.empty() ? -1 : 0;
}

void AsyncAddressResolverImpl::Destroy(bool wait) {
  DCHECK(CalledOnValidThread());
  resolver_->Cancel();
  // Libjingle doesn't need this object any more and it's not going to delete
  // it explicitly.
  delete this;
}

void AsyncAddressResolverImpl::OnAddressResolved(
    const net::IPAddressList& addresses) {
  DCHECK(CalledOnValidThread());
  for (size_t i = 0; i < addresses.size(); ++i) {
    rtc::SocketAddress socket_address;
    if (!jingle_glue::IPEndPointToSocketAddress(
            net::IPEndPoint(addresses[i], 0), &socket_address)) {
      NOTREACHED();
    }
    addresses_.push_back(socket_address.ipaddr());
  }
  SignalDone(this);
}

}  // namespace

IpcPacketSocketFactory::IpcPacketSocketFactory(
    P2PSocketDispatcher* socket_dispatcher)
    : socket_dispatcher_(socket_dispatcher) {
}

IpcPacketSocketFactory::~IpcPacketSocketFactory() {
}

rtc::AsyncPacketSocket* IpcPacketSocketFactory::CreateUdpSocket(
    const rtc::SocketAddress& local_address,
    uint16_t min_port,
    uint16_t max_port) {
  rtc::SocketAddress crome_address;
  P2PSocketClientImpl* socket_client =
      new P2PSocketClientImpl(socket_dispatcher_);
  scoped_ptr<IpcPacketSocket> socket(new IpcPacketSocket());
  // TODO(sergeyu): Respect local_address and port limits here (need
  // to pass them over IPC channel to the browser).
  if (!socket->Init(P2P_SOCKET_UDP, socket_client,
                    local_address, rtc::SocketAddress())) {
    return NULL;
  }
  return socket.release();
}

rtc::AsyncPacketSocket* IpcPacketSocketFactory::CreateServerTcpSocket(
    const rtc::SocketAddress& local_address,
    uint16_t min_port,
    uint16_t max_port,
    int opts) {
  // TODO(sergeyu): Implement SSL support.
  if (opts & rtc::PacketSocketFactory::OPT_SSLTCP)
    return NULL;

  P2PSocketType type = (opts & rtc::PacketSocketFactory::OPT_STUN) ?
      P2P_SOCKET_STUN_TCP_SERVER : P2P_SOCKET_TCP_SERVER;
  P2PSocketClientImpl* socket_client =
      new P2PSocketClientImpl(socket_dispatcher_);
  scoped_ptr<IpcPacketSocket> socket(new IpcPacketSocket());
  if (!socket->Init(type, socket_client, local_address,
                    rtc::SocketAddress())) {
    return NULL;
  }
  return socket.release();
}

rtc::AsyncPacketSocket* IpcPacketSocketFactory::CreateClientTcpSocket(
    const rtc::SocketAddress& local_address,
    const rtc::SocketAddress& remote_address,
    const rtc::ProxyInfo& proxy_info,
    const std::string& user_agent, int opts) {
  P2PSocketType type;
  if (opts & rtc::PacketSocketFactory::OPT_SSLTCP) {
    type = (opts & rtc::PacketSocketFactory::OPT_STUN) ?
        P2P_SOCKET_STUN_SSLTCP_CLIENT : P2P_SOCKET_SSLTCP_CLIENT;
  } else if (opts & rtc::PacketSocketFactory::OPT_TLS) {
    type = (opts & rtc::PacketSocketFactory::OPT_STUN) ?
        P2P_SOCKET_STUN_TLS_CLIENT : P2P_SOCKET_TLS_CLIENT;
  } else {
    type = (opts & rtc::PacketSocketFactory::OPT_STUN) ?
        P2P_SOCKET_STUN_TCP_CLIENT : P2P_SOCKET_TCP_CLIENT;
  }
  P2PSocketClientImpl* socket_client =
      new P2PSocketClientImpl(socket_dispatcher_);
  scoped_ptr<IpcPacketSocket> socket(new IpcPacketSocket());
  if (!socket->Init(type, socket_client, local_address, remote_address))
    return NULL;
  return socket.release();
}

rtc::AsyncResolverInterface*
IpcPacketSocketFactory::CreateAsyncResolver() {
  scoped_ptr<AsyncAddressResolverImpl> resolver(
    new AsyncAddressResolverImpl(socket_dispatcher_));
  return resolver.release();
}

}  // namespace content
