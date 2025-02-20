// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>
#include <string>

#include "base/at_exit.h"
#include "base/strings/stringprintf.h"
#include "blimp/common/logging.h"
#include "blimp/common/proto/blimp_message.pb.h"
#include "blimp/net/test_common.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::Return;

namespace blimp {
namespace {

const int kTargetTab = 123;

class LoggingTest : public testing::Test {
 public:
  LoggingTest() {}
  ~LoggingTest() override {}

 protected:
  // Verifies that the logged form of |msg| matches |expected|, modulo prefix
  // and suffix.
  void VerifyLogOutput(const std::string& expected_fragment,
                       const BlimpMessage& msg) {
    std::string expected = "<BlimpMessage " + expected_fragment +
                           " byte_size=" + std::to_string(msg.ByteSize()) + ">";
    std::stringstream outstream;
    outstream << msg;
    EXPECT_EQ(expected, outstream.str());
  }

 private:
  // Deletes the singleton on test termination.
  base::ShadowingAtExitManager at_exit_;
};

TEST_F(LoggingTest, Compositor) {
  BlimpMessage base_msg;
  base_msg.set_type(BlimpMessage::COMPOSITOR);
  base_msg.set_target_tab_id(kTargetTab);
  VerifyLogOutput("type=COMPOSITOR render_widget_id=0 target_tab_id=123",
                  base_msg);
}

TEST_F(LoggingTest, Input) {
  BlimpMessage base_msg;
  base_msg.set_type(BlimpMessage::INPUT);
  base_msg.set_target_tab_id(kTargetTab);
  VerifyLogOutput("type=INPUT render_widget_id=0 target_tab_id=123", base_msg);
}

TEST_F(LoggingTest, Navigation) {
  BlimpMessage base_msg;
  base_msg.set_type(BlimpMessage::NAVIGATION);
  base_msg.set_target_tab_id(kTargetTab);

  BlimpMessage navigation_state_msg = base_msg;
  navigation_state_msg.mutable_navigation()->set_type(
      NavigationMessage::NAVIGATION_STATE_CHANGED);
  navigation_state_msg.mutable_navigation()
      ->mutable_navigation_state_changed()
      ->set_url("http://foo.com");
  navigation_state_msg.mutable_navigation()
      ->mutable_navigation_state_changed()
      ->set_favicon("bytes!");
  navigation_state_msg.mutable_navigation()
      ->mutable_navigation_state_changed()
      ->set_title("FooCo");
  navigation_state_msg.mutable_navigation()
      ->mutable_navigation_state_changed()
      ->set_loading(true);
  VerifyLogOutput(
      "type=NAVIGATION subtype=NAVIGATION_STATE_CHANGED url=\"http://foo.com\" "
      "favicon_size=6 title=\"FooCo\" loading=true target_tab_id=123",
      navigation_state_msg);

  BlimpMessage load_url_msg = base_msg;
  load_url_msg.mutable_navigation()->set_type(NavigationMessage::LOAD_URL);
  load_url_msg.mutable_navigation()->mutable_load_url()->set_url(
      "http://foo.com");
  VerifyLogOutput(
      "type=NAVIGATION subtype=LOAD_URL url=\"http://foo.com\" "
      "target_tab_id=123",
      load_url_msg);

  BlimpMessage go_back_msg = base_msg;
  go_back_msg.mutable_navigation()->set_type(NavigationMessage::GO_BACK);
  VerifyLogOutput("type=NAVIGATION subtype=GO_BACK target_tab_id=123",
                  go_back_msg);

  BlimpMessage go_forward_msg = base_msg;
  go_forward_msg.mutable_navigation()->set_type(NavigationMessage::GO_FORWARD);
  VerifyLogOutput("type=NAVIGATION subtype=GO_FORWARD target_tab_id=123",
                  go_forward_msg);

  BlimpMessage reload_msg = base_msg;
  reload_msg.mutable_navigation()->set_type(NavigationMessage::RELOAD);
  VerifyLogOutput("type=NAVIGATION subtype=RELOAD target_tab_id=123",
                  reload_msg);
}

TEST_F(LoggingTest, TabControl) {
  BlimpMessage base_msg;
  base_msg.set_type(BlimpMessage::TAB_CONTROL);
  base_msg.set_target_tab_id(kTargetTab);

  BlimpMessage create_tab_msg = base_msg;
  create_tab_msg.mutable_tab_control()->set_type(TabControlMessage::CREATE_TAB);
  VerifyLogOutput("type=TAB_CONTROL subtype=CREATE_TAB target_tab_id=123",
                  create_tab_msg);

  BlimpMessage close_tab_msg = base_msg;
  close_tab_msg.mutable_tab_control()->set_type(TabControlMessage::CLOSE_TAB);
  VerifyLogOutput("type=TAB_CONTROL subtype=CLOSE_TAB target_tab_id=123",
                  close_tab_msg);

  BlimpMessage size_msg = base_msg;
  size_msg.mutable_tab_control()->set_type(TabControlMessage::SIZE);
  size_msg.mutable_tab_control()->mutable_size()->set_width(640);
  size_msg.mutable_tab_control()->mutable_size()->set_height(480);
  size_msg.mutable_tab_control()->mutable_size()->set_device_pixel_ratio(2);
  VerifyLogOutput(
      "type=TAB_CONTROL subtype=SIZE size=640x480:2.00 target_tab_id=123",
      size_msg);
}

TEST_F(LoggingTest, ProtocolControl) {
  BlimpMessage base_msg;
  base_msg.set_type(BlimpMessage::PROTOCOL_CONTROL);

  BlimpMessage start_connection_msg = base_msg;
  start_connection_msg.mutable_protocol_control()->set_type(
      ProtocolControlMessage::START_CONNECTION);
  start_connection_msg.mutable_protocol_control()
      ->mutable_start_connection()
      ->set_client_token("token");
  start_connection_msg.mutable_protocol_control()
      ->mutable_start_connection()
      ->set_protocol_version(2);
  VerifyLogOutput(
      "type=PROTOCOL_CONTROL subtype=START_CONNECTION "
      "client_token=\"token\" protocol_version=2",
      start_connection_msg);

  BlimpMessage checkpoint_msg = base_msg;
  start_connection_msg.mutable_protocol_control()->set_type(
      ProtocolControlMessage::CHECKPOINT_ACK);
  start_connection_msg.mutable_protocol_control()
      ->mutable_checkpoint_ack()
      ->set_checkpoint_id(123);
  VerifyLogOutput(
      "type=PROTOCOL_CONTROL subtype=CHECKPOINT_ACK "
      "checkpoint_id=123",
      start_connection_msg);
}

TEST_F(LoggingTest, RenderWidget) {
  BlimpMessage base_msg;
  base_msg.set_type(BlimpMessage::RENDER_WIDGET);
  base_msg.mutable_render_widget()->set_render_widget_id(123);

  BlimpMessage initialize_msg = base_msg;
  initialize_msg.mutable_render_widget()->set_type(
      RenderWidgetMessage::INITIALIZE);
  VerifyLogOutput("type=RENDER_WIDGET subtype=INITIALIZE render_widget_id=123",
                  initialize_msg);

  BlimpMessage created_msg = base_msg;
  created_msg.mutable_render_widget()->set_type(
      RenderWidgetMessage::CREATED);
  VerifyLogOutput("type=RENDER_WIDGET subtype=CREATED render_widget_id=123",
                  created_msg);

  BlimpMessage deleted_msg = base_msg;
  deleted_msg.mutable_render_widget()->set_type(RenderWidgetMessage::DELETED);
  VerifyLogOutput("type=RENDER_WIDGET subtype=DELETED render_widget_id=123",
                  deleted_msg);
}

}  // namespace
}  // namespace blimp
