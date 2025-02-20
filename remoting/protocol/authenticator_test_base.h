// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_AUTHENTICATOR_TEST_BASE_H_
#define REMOTING_PROTOCOL_AUTHENTICATOR_TEST_BASE_H_

#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace remoting {

class RsaKeyPair;

namespace protocol {

class Authenticator;
class ChannelAuthenticator;
class FakeStreamSocket;
class P2PStreamSocket;

class AuthenticatorTestBase : public testing::Test {
 public:
  AuthenticatorTestBase();
  ~AuthenticatorTestBase() override;

 protected:
  class MockChannelDoneCallback {
   public:
    MockChannelDoneCallback();
    ~MockChannelDoneCallback();
    MOCK_METHOD1(OnDone, void(int error));
  };

  static void ContinueAuthExchangeWith(Authenticator* sender,
                                       Authenticator* receiver,
                                       bool sender_started,
                                       bool receiver_srated);
  void SetUp() override;
  void RunAuthExchange();
  void RunHostInitiatedAuthExchange();
  void RunChannelAuth(bool expected_fail);

  void OnHostConnected(int error,
                       scoped_ptr<P2PStreamSocket> socket);
  void OnClientConnected(int error,
                         scoped_ptr<P2PStreamSocket> socket);

  base::MessageLoop message_loop_;

  scoped_refptr<RsaKeyPair> key_pair_;
  std::string host_public_key_;
  std::string host_cert_;
  scoped_ptr<Authenticator> host_;
  scoped_ptr<Authenticator> client_;
  scoped_ptr<FakeStreamSocket> client_fake_socket_;
  scoped_ptr<FakeStreamSocket> host_fake_socket_;
  scoped_ptr<ChannelAuthenticator> client_auth_;
  scoped_ptr<ChannelAuthenticator> host_auth_;
  MockChannelDoneCallback client_callback_;
  MockChannelDoneCallback host_callback_;
  scoped_ptr<P2PStreamSocket> client_socket_;
  scoped_ptr<P2PStreamSocket> host_socket_;

  DISALLOW_COPY_AND_ASSIGN(AuthenticatorTestBase);
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_AUTHENTICATOR_TEST_BASE_H_
