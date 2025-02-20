// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/thread_task_runner_handle.h"
#include "base/timer/mock_timer.h"
#include "base/values.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/unix_domain_client_socket_posix.h"
#include "remoting/host/host_mock_objects.h"
#include "remoting/host/security_key/gnubby_auth_handler.h"
#include "remoting/host/security_key/gnubby_extension_session.h"
#include "remoting/proto/internal.pb.h"
#include "remoting/protocol/client_stub.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace remoting {

namespace {

// Test gnubby request data.
const unsigned char kRequestData[] = {
    0x00, 0x00, 0x00, 0x9a, 0x65, 0x1e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x60, 0x90,
    0x24, 0x71, 0xf8, 0xf2, 0xe5, 0xdf, 0x7f, 0x81, 0xc7, 0x49, 0xc4, 0xa3,
    0x58, 0x5c, 0xf6, 0xcc, 0x40, 0x14, 0x28, 0x0c, 0xa0, 0xfa, 0x03, 0x18,
    0x38, 0xd8, 0x7d, 0x77, 0x2b, 0x3a, 0x00, 0x00, 0x00, 0x20, 0x64, 0x46,
    0x47, 0x2f, 0xdf, 0x6e, 0xed, 0x7b, 0xf3, 0xc3, 0x37, 0x20, 0xf2, 0x36,
    0x67, 0x6c, 0x36, 0xe1, 0xb4, 0x5e, 0xbe, 0x04, 0x85, 0xdb, 0x89, 0xa3,
    0xcd, 0xfd, 0xd2, 0x4b, 0xd6, 0x9f, 0x00, 0x00, 0x00, 0x40, 0x38, 0x35,
    0x05, 0x75, 0x1d, 0x13, 0x6e, 0xb3, 0x6b, 0x1d, 0x29, 0xae, 0xd3, 0x43,
    0xe6, 0x84, 0x8f, 0xa3, 0x9d, 0x65, 0x4e, 0x2f, 0x57, 0xe3, 0xf6, 0xe6,
    0x20, 0x3c, 0x00, 0xc6, 0xe1, 0x73, 0x34, 0xe2, 0x23, 0x99, 0xc4, 0xfa,
    0x91, 0xc2, 0xd5, 0x97, 0xc1, 0x8b, 0xd0, 0x3c, 0x13, 0xba, 0xf0, 0xd7,
    0x5e, 0xa3, 0xbc, 0x02, 0x5b, 0xec, 0xe4, 0x4b, 0xae, 0x0e, 0xf2, 0xbd,
    0xc8, 0xaa};

}  // namespace

class TestClientStub : public protocol::ClientStub {
 public:
  TestClientStub() : run_loop_(new base::RunLoop) {}
  ~TestClientStub() override {}

  // protocol::ClientStub implementation.
  void SetCapabilities(const protocol::Capabilities& capabilities) override {}

  void SetPairingResponse(
      const protocol::PairingResponse& pairing_response) override {}

  void DeliverHostMessage(const protocol::ExtensionMessage& message) override {
    message_ = message;
    run_loop_->Quit();
  }

  void SetVideoLayout(const protocol::VideoLayout& layout) override {}

  // protocol::ClipboardStub implementation.
  void InjectClipboardEvent(const protocol::ClipboardEvent& event) override {}

  // protocol::CursorShapeStub implementation.
  void SetCursorShape(const protocol::CursorShapeInfo& cursor_shape) override {}

  void WaitForDeliverHostMessage(base::TimeDelta max_timeout) {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, run_loop_->QuitClosure(), max_timeout);
    run_loop_->Run();
    run_loop_.reset(new base::RunLoop);
  }

  void CheckHostDataMessage(int id, const std::string& data) {
    std::string connection_id = base::StringPrintf("\"connectionId\":%d", id);
    std::string data_message = base::StringPrintf("\"data\":%s", data.c_str());

    ASSERT_TRUE(message_.type() == "gnubby-auth" ||
                message_.type() == "auth-v1");
    ASSERT_NE(message_.data().find("\"type\":\"data\""), std::string::npos);
    ASSERT_NE(message_.data().find(connection_id), std::string::npos);
    ASSERT_NE(message_.data().find(data_message), std::string::npos);
  }

 private:
  protocol::ExtensionMessage message_;
  scoped_ptr<base::RunLoop> run_loop_;

  DISALLOW_COPY_AND_ASSIGN(TestClientStub);
};

class GnubbyExtensionSessionTest : public testing::Test {
 public:
  GnubbyExtensionSessionTest()
      : gnubby_extension_session_(new GnubbyExtensionSession(&client_stub_)) {
    // We want to retain ownership of mock object so we can use it to inject
    // events into the extension session.  The mock object should not be used
    // once |gnubby_extension_session_| is destroyed.
    mock_gnubby_auth_handler_ = new MockGnubbyAuthHandler();
    gnubby_extension_session_->SetGnubbyAuthHandlerForTesting(
        make_scoped_ptr(mock_gnubby_auth_handler_));
  }

  void WaitForAndVerifyHostMessage() {
    client_stub_.WaitForDeliverHostMessage(
        base::TimeDelta::FromMilliseconds(500));
    base::ListValue expected_data;

    // Skip first four bytes.
    for (size_t i = 4; i < sizeof(kRequestData); ++i) {
      expected_data.AppendInteger(kRequestData[i]);
    }

    std::string expected_data_json;
    base::JSONWriter::Write(expected_data, &expected_data_json);
    client_stub_.CheckHostDataMessage(1, expected_data_json);
  }

  void CreateGnubbyConnection() {
    EXPECT_CALL(*mock_gnubby_auth_handler_, CreateGnubbyConnection()).Times(1);

    protocol::ExtensionMessage message;
    message.set_type("gnubby-auth");
    message.set_data("{\"type\":\"control\",\"option\":\"auth-v1\"}");

    ASSERT_TRUE(gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr,
                                                              message));
  }

 protected:
  base::MessageLoopForIO message_loop_;

  // Object under test.
  scoped_ptr<GnubbyExtensionSession> gnubby_extension_session_;

  MockGnubbyAuthHandler* mock_gnubby_auth_handler_ = nullptr;

  TestClientStub client_stub_;

 private:
  DISALLOW_COPY_AND_ASSIGN(GnubbyExtensionSessionTest);
};

TEST_F(GnubbyExtensionSessionTest, GnubbyConnectionCreated_ValidMessage) {
  CreateGnubbyConnection();
}

TEST_F(GnubbyExtensionSessionTest, NoGnubbyConnectionCreated_WrongMessageType) {
  EXPECT_CALL(*mock_gnubby_auth_handler_, CreateGnubbyConnection()).Times(0);

  protocol::ExtensionMessage message;
  message.set_type("unsupported-gnubby-auth");
  message.set_data("{\"type\":\"control\",\"option\":\"auth-v1\"}");

  ASSERT_FALSE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest,
       NoGnubbyConnectionCreated_InvalidMessageData) {
  EXPECT_CALL(*mock_gnubby_auth_handler_, CreateGnubbyConnection()).Times(0);

  // First try invalid JSON.
  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"control\",\"option\":}");
  // handled should still be true, even if the message payload is invalid.
  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));

  // Now try an invalid message type.
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"control\",\"option\":\"auth-v0\"}");
  // handled should still be true, even if the message payload is invalid.
  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));

  // Now try a message that is missing the option and auth type.
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"control\"}");
  // handled should still be true, even if the message payload is invalid.
  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, DataMessageProcessing_MissingConnectionId) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(testing::_, testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendErrorAndCloseConnection(testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .Times(0);

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"data\"}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, DataMessageProcessing_InvalidConnectionId) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(testing::_, testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendErrorAndCloseConnection(testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(1)).Times(1);

  ON_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .WillByDefault(testing::Return(false));

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"data\",\"connectionId\":1}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, DataMessageProcessing_MissingPayload) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_, SendErrorAndCloseConnection(1))
      .Times(1);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(testing::_, testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(1)).Times(1);

  ON_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .WillByDefault(testing::Return(true));

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"data\",\"connectionId\":1}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, DataMessageProcessing_InvalidPayload) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_, SendErrorAndCloseConnection(1))
      .Times(1);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(testing::_, testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(1)).Times(1);

  ON_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .WillByDefault(testing::Return(true));

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data(
      "{\"type\":\"data\",\"connectionId\":1,\"data\":[\"a\",\"-\",\"z\"]}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, DataMessageProcessing_ValidData) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(1, "\x1\x2\x3\x4\x5"))
      .Times(1);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendErrorAndCloseConnection(testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(1)).Times(1);

  ON_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .WillByDefault(testing::Return(true));

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data(
      "{\"type\":\"data\",\"connectionId\":1,\"data\":[1,2,3,4,5]}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, ErrorMessageProcessing_MissingConnectionId) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendErrorAndCloseConnection(testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(testing::_, testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .Times(0);

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"error\"}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, ErrorMessageProcessing_InvalidConnectionId) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendErrorAndCloseConnection(testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(testing::_, testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(1)).Times(1);

  ON_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .WillByDefault(testing::Return(false));

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"error\",\"connectionId\":1}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, ErrorMessageProcessing_ValidData) {
  CreateGnubbyConnection();

  EXPECT_CALL(*mock_gnubby_auth_handler_, SendErrorAndCloseConnection(1))
      .Times(1);
  EXPECT_CALL(*mock_gnubby_auth_handler_,
              SendClientResponse(testing::_, testing::_))
      .Times(0);
  EXPECT_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(1)).Times(1);

  ON_CALL(*mock_gnubby_auth_handler_, IsValidConnectionId(testing::_))
      .WillByDefault(testing::Return(true));

  protocol::ExtensionMessage message;
  message.set_type("gnubby-auth");
  message.set_data("{\"type\":\"error\",\"connectionId\":1}");

  ASSERT_TRUE(
      gnubby_extension_session_->OnExtensionMessage(nullptr, nullptr, message));
}

TEST_F(GnubbyExtensionSessionTest, SendMessageToClient_ValidData) {
  CreateGnubbyConnection();

  // Inject data into the SendMessageCallback to simulate a gnubby request.
  mock_gnubby_auth_handler_->GetSendMessageCallback().Run(42, "test_msg");

  client_stub_.WaitForDeliverHostMessage(
      base::TimeDelta::FromMilliseconds(500));

  // Expects a JSON array of the ASCII character codes for "test_msg".
  client_stub_.CheckHostDataMessage(42, "[116,101,115,116,95,109,115,103]");
}

}  // namespace remoting
