// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/spake2_authenticator.h"

#include "base/bind.h"
#include "base/macros.h"
#include "remoting/base/rsa_key_pair.h"
#include "remoting/protocol/authenticator_test_base.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/connection_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/webrtc/libjingle/xmllite/xmlelement.h"

using testing::_;
using testing::DeleteArg;
using testing::SaveArg;

namespace remoting {
namespace protocol {

namespace {

const int kMessageSize = 100;
const int kMessages = 1;

const char kClientId[] = "alice@gmail.com/abc";
const char kHostId[] = "alice@gmail.com/123";

const char kTestSharedSecret[] = "1234-1234-5678";
const char kTestSharedSecretBad[] = "0000-0000-0001";

}  // namespace

class Spake2AuthenticatorTest : public AuthenticatorTestBase {
 public:
  Spake2AuthenticatorTest() {}
  ~Spake2AuthenticatorTest() override {}

 protected:
  void InitAuthenticators(const std::string& client_secret,
                          const std::string& host_secret) {
    host_ = Spake2Authenticator::CreateForHost(kHostId, kClientId, host_cert_,
                                               key_pair_, host_secret,
                                               Authenticator::WAITING_MESSAGE);
    client_ = Spake2Authenticator::CreateForClient(
        kClientId, kHostId, client_secret, Authenticator::MESSAGE_READY);
  }

  DISALLOW_COPY_AND_ASSIGN(Spake2AuthenticatorTest);
};

TEST_F(Spake2AuthenticatorTest, SuccessfulAuth) {
  ASSERT_NO_FATAL_FAILURE(
      InitAuthenticators(kTestSharedSecret, kTestSharedSecret));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());

  ASSERT_EQ(Authenticator::ACCEPTED, host_->state());
  ASSERT_EQ(Authenticator::ACCEPTED, client_->state());

  client_auth_ = client_->CreateChannelAuthenticator();
  host_auth_ = host_->CreateChannelAuthenticator();
  RunChannelAuth(false);

  StreamConnectionTester tester(host_socket_.get(), client_socket_.get(),
                                kMessageSize, kMessages);

  tester.Start();
  message_loop_.Run();
  tester.CheckResults();
}

// Verify that connection is rejected when secrets don't match.
TEST_F(Spake2AuthenticatorTest, InvalidSecret) {
  ASSERT_NO_FATAL_FAILURE(
      InitAuthenticators(kTestSharedSecretBad, kTestSharedSecret));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());

  ASSERT_EQ(Authenticator::REJECTED, client_->state());
  ASSERT_EQ(Authenticator::INVALID_CREDENTIALS, client_->rejection_reason());

  // Change |client_| so that we can get the last message.
  reinterpret_cast<Spake2Authenticator*>(client_.get())->state_ =
      Authenticator::MESSAGE_READY;

  scoped_ptr<buzz::XmlElement> message(client_->GetNextMessage());
  ASSERT_TRUE(message.get());

  ASSERT_EQ(Authenticator::WAITING_MESSAGE, client_->state());
  host_->ProcessMessage(message.get(), base::Bind(&base::DoNothing));
  // This assumes that Spake2Authenticator::ProcessMessage runs synchronously.
  ASSERT_EQ(Authenticator::REJECTED, host_->state());
}

}  // namespace protocol
}  // namespace remoting
