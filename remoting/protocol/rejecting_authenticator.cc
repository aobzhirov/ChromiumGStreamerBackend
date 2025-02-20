// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/rejecting_authenticator.h"

#include "base/callback.h"
#include "base/logging.h"
#include "remoting/protocol/channel_authenticator.h"
#include "third_party/webrtc/libjingle/xmllite/xmlelement.h"

namespace remoting {
namespace protocol {

RejectingAuthenticator::RejectingAuthenticator(RejectionReason rejection_reason)
    : rejection_reason_(rejection_reason) {
}

RejectingAuthenticator::~RejectingAuthenticator() = default;

Authenticator::State RejectingAuthenticator::state() const {
  return state_;
}
bool RejectingAuthenticator::started() const {
  return true;
}

Authenticator::RejectionReason
RejectingAuthenticator::rejection_reason() const {
  DCHECK_EQ(state_, REJECTED);
  return rejection_reason_;
}

void RejectingAuthenticator::ProcessMessage(
    const buzz::XmlElement* message,
    const base::Closure& resume_callback) {
  DCHECK_EQ(state_, WAITING_MESSAGE);
  state_ = REJECTED;
  resume_callback.Run();
}

scoped_ptr<buzz::XmlElement> RejectingAuthenticator::GetNextMessage() {
  NOTREACHED();
  return nullptr;
}

const std::string& RejectingAuthenticator::GetAuthKey() const {
  NOTREACHED();
  return auth_key_;
};

scoped_ptr<ChannelAuthenticator>
RejectingAuthenticator::CreateChannelAuthenticator() const {
  NOTREACHED();
  return nullptr;
}

}  // namespace protocol
}  // namespace remoting
