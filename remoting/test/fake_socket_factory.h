// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_TEST_FAKE_SOCKET_FACTORY_H_
#define REMOTING_TEST_FAKE_SOCKET_FACTORY_H_

#include <stdint.h>

#include <list>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "remoting/test/fake_network_dispatcher.h"
#include "third_party/webrtc/p2p/base/packetsocketfactory.h"

namespace remoting {

class FakeNetworkDispatcher;
class LeakyBucket;

class FakePacketSocketFactory : public rtc::PacketSocketFactory,
                                public FakeNetworkDispatcher::Node {
 public:
  // |dispatcher| must outlive the factory.
  explicit FakePacketSocketFactory(FakeNetworkDispatcher* dispatcher);
  ~FakePacketSocketFactory() override;

  void OnSocketDestroyed(int port);

  // |bandwidth| - simulated link bandwidth in bytes/second. 0 indicates that
  // bandwidth is unlimited.
  // |max_buffer| - size of buffers in bytes. Ignored when |bandwidth| is 0.
  void SetBandwidth(int bandwidth, int max_buffer);

  // Specifies parameters for simulated network latency. Latency is generated
  // with normal distribution around |average| with the given |stddev|. Random
  // latency calculated based on these parameters is added to the buffering
  // delay (which is calculated based on the parameters passed to
  // SetBandwidth()). I.e. total latency for each packet is calculated using the
  // following formula
  //
  //    l = NormalRand(average, stddev) + bytes_buffered / bandwidth .
  //
  // Where bytes_buffered is the current level in the leaky bucket used to
  // control bandwidth.
  void SetLatency(base::TimeDelta average, base::TimeDelta stddev);

  void set_out_of_order_rate(double out_of_order_rate) {
    out_of_order_rate_ = out_of_order_rate;
  }

  // rtc::PacketSocketFactory interface.
  rtc::AsyncPacketSocket* CreateUdpSocket(
      const rtc::SocketAddress& local_address,
      uint16_t min_port,
      uint16_t max_port) override;
  rtc::AsyncPacketSocket* CreateServerTcpSocket(
      const rtc::SocketAddress& local_address,
      uint16_t min_port,
      uint16_t max_port,
      int opts) override;
  rtc::AsyncPacketSocket* CreateClientTcpSocket(
      const rtc::SocketAddress& local_address,
      const rtc::SocketAddress& remote_address,
      const rtc::ProxyInfo& proxy_info,
      const std::string& user_agent,
      int opts) override;
  rtc::AsyncResolverInterface* CreateAsyncResolver() override;

  // FakeNetworkDispatcher::Node interface.
  const scoped_refptr<base::SingleThreadTaskRunner>& GetThread() const override;
  const rtc::IPAddress& GetAddress() const override;
  void ReceivePacket(const rtc::SocketAddress& from,
                     const rtc::SocketAddress& to,
                     const scoped_refptr<net::IOBuffer>& data,
                     int data_size) override;

 private:
  struct PendingPacket {
    PendingPacket();
    PendingPacket(
        const rtc::SocketAddress& from,
        const rtc::SocketAddress& to,
        const scoped_refptr<net::IOBuffer>& data,
        int data_size);
    PendingPacket(const PendingPacket& other);
    ~PendingPacket();

    rtc::SocketAddress from;
    rtc::SocketAddress to;
    scoped_refptr<net::IOBuffer> data;
    int data_size;
  };

  typedef base::Callback<void(const rtc::SocketAddress& from,
                              const rtc::SocketAddress& to,
                              const scoped_refptr<net::IOBuffer>& data,
                              int data_size)> ReceiveCallback;
  typedef std::map<uint16_t, ReceiveCallback> UdpSocketsMap;

  void DoReceivePacket();

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  scoped_refptr<FakeNetworkDispatcher> dispatcher_;

  rtc::IPAddress address_;

  scoped_ptr<LeakyBucket> leaky_bucket_;
  base::TimeDelta latency_average_;
  base::TimeDelta latency_stddev_;
  double out_of_order_rate_;

  UdpSocketsMap udp_sockets_;
  uint16_t next_port_;

  std::list<PendingPacket> pending_packets_;

  base::WeakPtrFactory<FakePacketSocketFactory> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(FakePacketSocketFactory);
};

}  // namespace remoting

#endif  // REMOTING_TEST_FAKE_SOCKET_FACTORY_H_
