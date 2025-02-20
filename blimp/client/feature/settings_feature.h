// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLIMP_CLIENT_FEATURE_SETTINGS_FEATURE_H_
#define BLIMP_CLIENT_FEATURE_SETTINGS_FEATURE_H_

#include "base/macros.h"
#include "blimp/client/blimp_client_export.h"
#include "blimp/net/blimp_message_processor.h"

namespace blimp {
namespace client {

// The feature is used to send global settings to the engine.
class BLIMP_CLIENT_EXPORT SettingsFeature : public BlimpMessageProcessor {
 public:
  SettingsFeature();
  ~SettingsFeature() override;

  // Set the BlimpMessageProcessor that will be used to send
  // BlimpMessage::SETTINGS messages to the engine.
  void set_outgoing_message_processor(
      scoped_ptr<BlimpMessageProcessor> processor);

  void SetRecordWholeDocument(bool record_whole_document);

 private:
  // BlimpMessageProcessor implementation.
  void ProcessMessage(scoped_ptr<BlimpMessage> message,
                      const net::CompletionCallback& callback) override;

  // Used to send BlimpMessage::TAB_CONTROL messages to the engine.
  scoped_ptr<BlimpMessageProcessor> outgoing_message_processor_;

  // Used to avoid sending unnecessary messages to engine.
  bool record_whole_document_;

  DISALLOW_COPY_AND_ASSIGN(SettingsFeature);
};

}  // namespace client
}  // namespace blimp

#endif  // BLIMP_CLIENT_FEATURE_SETTINGS_FEATURE_H_
