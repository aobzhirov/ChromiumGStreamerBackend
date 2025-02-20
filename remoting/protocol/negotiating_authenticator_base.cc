// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/negotiating_authenticator_base.h"

#include <algorithm>
#include <sstream>

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "remoting/base/constants.h"
#include "remoting/base/rsa_key_pair.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/name_value_map.h"
#include "third_party/webrtc/libjingle/xmllite/xmlelement.h"

namespace remoting {
namespace protocol {

namespace {

const NameMapElement<NegotiatingAuthenticatorBase::Method>
    kAuthenticationMethodStrings[] = {
        {NegotiatingAuthenticatorBase::Method::SHARED_SECRET_SPAKE2_P224,
         "spake2_hmac"},
        {NegotiatingAuthenticatorBase::Method::SHARED_SECRET_SPAKE2_CURVE25519,
         "spake2_curve25519"},

        {NegotiatingAuthenticatorBase::Method::PAIRED_SPAKE2_P224,
         "spake2_pair"},
        {NegotiatingAuthenticatorBase::Method::PAIRED_SPAKE2_CURVE25519,
         "pair_spake2_curve25519"},

        {NegotiatingAuthenticatorBase::Method::THIRD_PARTY_SPAKE2_P224,
         "third_party"},
        {NegotiatingAuthenticatorBase::Method::THIRD_PARTY_SPAKE2_CURVE25519,
         "third_party_spake2_curve25519"},
};

}  // namespace

const buzz::StaticQName NegotiatingAuthenticatorBase::kMethodAttributeQName = {
    "", "method"};
const buzz::StaticQName
    NegotiatingAuthenticatorBase::kSupportedMethodsAttributeQName = {
        "", "supported-methods"};
const char NegotiatingAuthenticatorBase::kSupportedMethodsSeparator = ',';

const buzz::StaticQName NegotiatingAuthenticatorBase::kPairingInfoTag = {
    kChromotingXmlNamespace, "pairing-info"};
const buzz::StaticQName NegotiatingAuthenticatorBase::kClientIdAttribute = {
    "", "client-id"};

NegotiatingAuthenticatorBase::NegotiatingAuthenticatorBase(
    Authenticator::State initial_state)
    : state_(initial_state) {}

NegotiatingAuthenticatorBase::~NegotiatingAuthenticatorBase() {}

Authenticator::State NegotiatingAuthenticatorBase::state() const {
  return state_;
}

bool NegotiatingAuthenticatorBase::started() const {
  if (!current_authenticator_) {
    return false;
  }
  return current_authenticator_->started();
}

Authenticator::RejectionReason
NegotiatingAuthenticatorBase::rejection_reason() const {
  return rejection_reason_;
}

// static
NegotiatingAuthenticatorBase::Method
NegotiatingAuthenticatorBase::ParseMethodString(const std::string& value) {
  Method result;
  if (!NameToValue(kAuthenticationMethodStrings, value, &result))
    return Method::INVALID;
  return result;
}

// static
std::string NegotiatingAuthenticatorBase::MethodToString(Method method) {
  return ValueToName(kAuthenticationMethodStrings, method);
}

void NegotiatingAuthenticatorBase::ProcessMessageInternal(
    const buzz::XmlElement* message,
    const base::Closure& resume_callback) {
  DCHECK_EQ(state_, PROCESSING_MESSAGE);

  if (current_authenticator_->state() == WAITING_MESSAGE) {
    // If the message was not discarded and the authenticator is waiting for it,
    // give it to the underlying authenticator to process.
    // |current_authenticator_| is owned, so Unretained() is safe here.
    current_authenticator_->ProcessMessage(
        message, base::Bind(&NegotiatingAuthenticatorBase::UpdateState,
                            base::Unretained(this), resume_callback));
  } else {
    // Otherwise, just discard the message.
    UpdateState(resume_callback);
  }
}

void NegotiatingAuthenticatorBase::UpdateState(
    const base::Closure& resume_callback) {
  DCHECK_EQ(state_, PROCESSING_MESSAGE);

  // After the underlying authenticator finishes processing the message, the
  // NegotiatingAuthenticatorBase must update its own state before running the
  // |resume_callback| to resume the session negotiation.
  state_ = current_authenticator_->state();

  // Verify that this is a valid state transition.
  DCHECK(state_ == MESSAGE_READY || state_ == ACCEPTED || state_ == REJECTED)
      << "State: " << state_;

  if (state_ == REJECTED)
    rejection_reason_ = current_authenticator_->rejection_reason();

  resume_callback.Run();
}

scoped_ptr<buzz::XmlElement>
NegotiatingAuthenticatorBase::GetNextMessageInternal() {
  DCHECK_EQ(state(), MESSAGE_READY);
  DCHECK(current_method_ != Method::INVALID);

  scoped_ptr<buzz::XmlElement> result;
  if (current_authenticator_->state() == MESSAGE_READY) {
    result = current_authenticator_->GetNextMessage();
  } else {
    result = CreateEmptyAuthenticatorMessage();
  }
  state_ = current_authenticator_->state();
  DCHECK(state_ == ACCEPTED || state_ == WAITING_MESSAGE);
  result->AddAttr(kMethodAttributeQName, MethodToString(current_method_));
  return result;
}

void NegotiatingAuthenticatorBase::AddMethod(Method method) {
  DCHECK(method != Method::INVALID);
  methods_.push_back(method);
}

const std::string& NegotiatingAuthenticatorBase::GetAuthKey() const {
  DCHECK_EQ(state(), ACCEPTED);
  return current_authenticator_->GetAuthKey();
}

scoped_ptr<ChannelAuthenticator>
NegotiatingAuthenticatorBase::CreateChannelAuthenticator() const {
  DCHECK_EQ(state(), ACCEPTED);
  return current_authenticator_->CreateChannelAuthenticator();
}

}  // namespace protocol
}  // namespace remoting
