// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/security_key/remote_security_key_message_writer.h"

#include <cstdint>
#include <utility>

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/task_runner_util.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "remoting/host/security_key/security_key_message.h"
#include "remoting/host/setup/test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
const remoting::RemoteSecurityKeyMessageType kTestMessageType =
    remoting::RemoteSecurityKeyMessageType::CONNECT;
const unsigned int kLargeMessageSizeBytes = 200000;
}  // namespace

namespace remoting {

class RemoteSecurityKeyMessageWriterTest : public testing::Test {
 public:
  RemoteSecurityKeyMessageWriterTest();
  ~RemoteSecurityKeyMessageWriterTest() override;

  // Run on a separate thread, this method reads the message written to the
  // output stream and returns the result.
  std::string ReadMessage(int payload_length_bytes);

  // Called back once the read operation has completed.
  void OnReadComplete(const base::Closure& done_callback,
                      const std::string& result);

 protected:
  // testing::Test interface.
  void SetUp() override;

  // Writes |kTestMessageType| and |payload| to the output stream and verifies
  // they were written correctly.
  void WriteMessageToOutput(const std::string& payload);

  scoped_ptr<RemoteSecurityKeyMessageWriter> writer_;
  base::File read_file_;
  base::File write_file_;

  // Stores the result of the last read operation.
  std::string message_result_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RemoteSecurityKeyMessageWriterTest);
};

RemoteSecurityKeyMessageWriterTest::RemoteSecurityKeyMessageWriterTest() {}

RemoteSecurityKeyMessageWriterTest::~RemoteSecurityKeyMessageWriterTest() {}

std::string RemoteSecurityKeyMessageWriterTest::ReadMessage(
    int payload_length_bytes) {
  std::string message_header(SecurityKeyMessage::kHeaderSizeBytes, '\0');
  read_file_.ReadAtCurrentPos(string_as_array(&message_header),
                              SecurityKeyMessage::kHeaderSizeBytes);

  std::string message_type(SecurityKeyMessage::kMessageTypeSizeBytes, '\0');
  read_file_.ReadAtCurrentPos(string_as_array(&message_type),
                              SecurityKeyMessage::kMessageTypeSizeBytes);

  std::string message_data(payload_length_bytes, '\0');
  if (payload_length_bytes) {
    read_file_.ReadAtCurrentPos(string_as_array(&message_data),
                                payload_length_bytes);
  }

  return message_header + message_type + message_data;
}

void RemoteSecurityKeyMessageWriterTest::OnReadComplete(
    const base::Closure& done_callback,
    const std::string& result) {
  message_result_ = result;
  done_callback.Run();
}

void RemoteSecurityKeyMessageWriterTest::SetUp() {
  ASSERT_TRUE(MakePipe(&read_file_, &write_file_));
  writer_.reset(new RemoteSecurityKeyMessageWriter(std::move(write_file_)));
}

void RemoteSecurityKeyMessageWriterTest::WriteMessageToOutput(
    const std::string& payload) {
  // Thread used for blocking IO operations.
  base::Thread reader_thread("ReaderThread");

  base::Thread::Options options;
  options.message_loop_type = base::MessageLoop::TYPE_IO;
  reader_thread.StartWithOptions(options);

  // Used to block until the read complete callback is triggered.
  base::MessageLoopForIO message_loop;
  base::RunLoop run_loop;

  ASSERT_TRUE(base::PostTaskAndReplyWithResult(
      reader_thread.task_runner().get(), FROM_HERE,
      base::Bind(&RemoteSecurityKeyMessageWriterTest::ReadMessage,
                 base::Unretained(this), payload.size()),
      base::Bind(&RemoteSecurityKeyMessageWriterTest::OnReadComplete,
                 base::Unretained(this), run_loop.QuitClosure())));

  if (payload.size()) {
    ASSERT_TRUE(writer_->WriteMessageWithPayload(kTestMessageType, payload));
  } else {
    ASSERT_TRUE(writer_->WriteMessage(kTestMessageType));
  }

  run_loop.Run();

  size_t total_size = SecurityKeyMessage::kHeaderSizeBytes +
                      SecurityKeyMessage::kMessageTypeSizeBytes +
                      payload.size();
  ASSERT_EQ(message_result_.size(), total_size);

  RemoteSecurityKeyMessageType type =
      SecurityKeyMessage::MessageTypeFromValue(message_result_[4]);
  ASSERT_EQ(kTestMessageType, type);

  if (payload.size()) {
    ASSERT_EQ(message_result_.substr(5), payload);
  }

  // Destroy the writer and verify the other end of the pipe is clean.
  writer_.reset();
  char unused;
  ASSERT_LE(read_file_.ReadAtCurrentPos(&unused, 1), 0);
}

TEST_F(RemoteSecurityKeyMessageWriterTest, WriteMessageWithoutPayload) {
  std::string empty_payload;
  WriteMessageToOutput(empty_payload);
}

TEST_F(RemoteSecurityKeyMessageWriterTest, WriteMessageWithPayload) {
  WriteMessageToOutput("Super-test-payload!");
}

TEST_F(RemoteSecurityKeyMessageWriterTest, WriteMessageWithLargePayload) {
  WriteMessageToOutput(std::string(kLargeMessageSizeBytes, 'Y'));
}

TEST_F(RemoteSecurityKeyMessageWriterTest, WriteMultipleMessages) {
  int total_messages_to_write = 10;
  for (int i = 0; i < total_messages_to_write; i++) {
    if (i % 2 == 0) {
      ASSERT_TRUE(writer_->WriteMessage(RemoteSecurityKeyMessageType::CONNECT));
    } else {
      ASSERT_TRUE(writer_->WriteMessage(RemoteSecurityKeyMessageType::REQUEST));
    }
  }

  for (int i = 0; i < total_messages_to_write; i++) {
    // Retrieve and verify the message header.
    int length;
    ASSERT_EQ(SecurityKeyMessage::kHeaderSizeBytes,
              read_file_.ReadAtCurrentPos(reinterpret_cast<char*>(&length), 4));
    ASSERT_EQ(SecurityKeyMessage::kMessageTypeSizeBytes, length);

    // Retrieve and verify the message type.
    std::string message_type(length, '\0');
    int bytes_read =
        read_file_.ReadAtCurrentPos(string_as_array(&message_type), length);
    ASSERT_EQ(length, bytes_read);

    RemoteSecurityKeyMessageType type =
        SecurityKeyMessage::MessageTypeFromValue(message_type[0]);
    if (i % 2 == 0) {
      ASSERT_EQ(RemoteSecurityKeyMessageType::CONNECT, type);
    } else {
      ASSERT_EQ(RemoteSecurityKeyMessageType::REQUEST, type);
    }
  }

  // Destroy the writer and verify the other end of the pipe is clean.
  writer_.reset();
  char unused;
  ASSERT_LE(read_file_.ReadAtCurrentPos(&unused, 1), 0);
}

TEST_F(RemoteSecurityKeyMessageWriterTest, EnsureWriteFailsWhenPipeClosed) {
  // Close the read end so that writing fails immediately.
  read_file_.Close();

  EXPECT_FALSE(writer_->WriteMessage(kTestMessageType));
}

}  // namespace remoting
