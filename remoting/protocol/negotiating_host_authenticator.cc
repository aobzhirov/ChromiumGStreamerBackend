// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/negotiating_host_authenticator.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "remoting/base/rsa_key_pair.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/pairing_host_authenticator.h"
#include "remoting/protocol/pairing_registry.h"
#include "remoting/protocol/spake2_authenticator.h"
#include "remoting/protocol/token_validator.h"
#include "remoting/protocol/v2_authenticator.h"
#include "third_party/webrtc/libjingle/xmllite/xmlelement.h"

namespace remoting {
namespace protocol {

NegotiatingHostAuthenticator::NegotiatingHostAuthenticator(
    const std::string& local_id,
    const std::string& remote_id,
    const std::string& local_cert,
    scoped_refptr<RsaKeyPair> key_pair)
    : NegotiatingAuthenticatorBase(WAITING_MESSAGE),
      local_id_(local_id),
      remote_id_(remote_id),
      local_cert_(local_cert),
      local_key_pair_(key_pair) {}

// static
scoped_ptr<NegotiatingHostAuthenticator>
NegotiatingHostAuthenticator::CreateWithSharedSecret(
    const std::string& local_id,
    const std::string& remote_id,
    const std::string& local_cert,
    scoped_refptr<RsaKeyPair> key_pair,
    const std::string& shared_secret_hash,
    scoped_refptr<PairingRegistry> pairing_registry) {
  scoped_ptr<NegotiatingHostAuthenticator> result(
      new NegotiatingHostAuthenticator(local_id, remote_id, local_cert,
                                       key_pair));
  result->shared_secret_hash_ = shared_secret_hash;
  result->pairing_registry_ = pairing_registry;
  result->AddMethod(Method::SHARED_SECRET_SPAKE2_CURVE25519);
  result->AddMethod(Method::SHARED_SECRET_SPAKE2_P224);
  if (pairing_registry.get()) {
    result->AddMethod(Method::PAIRED_SPAKE2_CURVE25519);
    result->AddMethod(Method::PAIRED_SPAKE2_P224);
  }
  return result;
}

// static
scoped_ptr<NegotiatingHostAuthenticator>
NegotiatingHostAuthenticator::CreateWithThirdPartyAuth(
    const std::string& local_id,
    const std::string& remote_id,
    const std::string& local_cert,
    scoped_refptr<RsaKeyPair> key_pair,
    scoped_refptr<TokenValidatorFactory> token_validator_factory) {
  scoped_ptr<NegotiatingHostAuthenticator> result(
      new NegotiatingHostAuthenticator(local_id, remote_id, local_cert,
                                       key_pair));
  result->token_validator_factory_ = token_validator_factory;
  result->AddMethod(Method::THIRD_PARTY_SPAKE2_CURVE25519);
  result->AddMethod(Method::THIRD_PARTY_SPAKE2_P224);
  return result;
}

NegotiatingHostAuthenticator::~NegotiatingHostAuthenticator() {}

void NegotiatingHostAuthenticator::ProcessMessage(
    const buzz::XmlElement* message,
    const base::Closure& resume_callback) {
  DCHECK_EQ(state(), WAITING_MESSAGE);
  state_ = PROCESSING_MESSAGE;

  const buzz::XmlElement* pairing_tag = message->FirstNamed(kPairingInfoTag);
  if (pairing_tag) {
    client_id_ = pairing_tag->Attr(kClientIdAttribute);
  }

  std::string method_attr = message->Attr(kMethodAttributeQName);
  Method method = ParseMethodString(method_attr);

  // If the host has already chosen a method, it can't be changed by the client.
  if (current_method_ != Method::INVALID && method != current_method_) {
    state_ = REJECTED;
    rejection_reason_ = PROTOCOL_ERROR;
    resume_callback.Run();
    return;
  }

  // If the client did not specify a preferred auth method, or specified an
  // unknown or unsupported method, then select the first known method from
  // the supported-methods attribute.
  if (method == Method::INVALID ||
      std::find(methods_.begin(), methods_.end(), method) == methods_.end()) {
    method = Method::INVALID;

    std::string supported_methods_attr =
        message->Attr(kSupportedMethodsAttributeQName);
    if (supported_methods_attr.empty()) {
      // Message contains neither method nor supported-methods attributes.
      state_ = REJECTED;
      rejection_reason_ = PROTOCOL_ERROR;
      resume_callback.Run();
      return;
    }

    // Find the first mutually-supported method in the client's list of
    // supported-methods.
    for (const std::string& method_str :
         base::SplitString(supported_methods_attr,
                           std::string(1, kSupportedMethodsSeparator),
                           base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
      Method list_value = ParseMethodString(method_str);
      if (list_value != Method::INVALID &&
          std::find(methods_.begin(), methods_.end(), list_value) !=
              methods_.end()) {
        // Found common method.
        method = list_value;
        break;
      }
    }

    if (method == Method::INVALID) {
      // Failed to find a common auth method.
      state_ = REJECTED;
      rejection_reason_ = PROTOCOL_ERROR;
      resume_callback.Run();
      return;
    }

    // Drop the current message because we've chosen a different method.
    current_method_ = method;
    CreateAuthenticator(MESSAGE_READY,
                        base::Bind(&NegotiatingHostAuthenticator::UpdateState,
                                   base::Unretained(this), resume_callback));
    return;
  }

  // If the client specified a supported method, and the host hasn't chosen a
  // method yet, use the client's preferred method and process the message.
  if (current_method_ == Method::INVALID) {
    current_method_ = method;
    // Copy the message since the authenticator may process it asynchronously.
    CreateAuthenticator(
        WAITING_MESSAGE,
        base::Bind(&NegotiatingAuthenticatorBase::ProcessMessageInternal,
                   base::Unretained(this),
                   base::Owned(new buzz::XmlElement(*message)),
                   resume_callback));
    return;
  }

  // If the client is using the host's current method, just process the message.
  ProcessMessageInternal(message, resume_callback);
}

scoped_ptr<buzz::XmlElement> NegotiatingHostAuthenticator::GetNextMessage() {
  return GetNextMessageInternal();
}

void NegotiatingHostAuthenticator::CreateAuthenticator(
    Authenticator::State preferred_initial_state,
    const base::Closure& resume_callback) {
  DCHECK(current_method_ != Method::INVALID);

  switch(current_method_) {
    case Method::INVALID:
      NOTREACHED();
      break;

    case Method::THIRD_PARTY_SPAKE2_P224:
      current_authenticator_.reset(new ThirdPartyHostAuthenticator(
          base::Bind(&V2Authenticator::CreateForHost, local_cert_,
                     local_key_pair_),
          token_validator_factory_->CreateTokenValidator(local_id_,
                                                         remote_id_)));
      resume_callback.Run();
      break;

    case Method::THIRD_PARTY_SPAKE2_CURVE25519:
      current_authenticator_.reset(new ThirdPartyHostAuthenticator(
          base::Bind(&Spake2Authenticator::CreateForHost, local_id_, remote_id_,
                     local_cert_, local_key_pair_),
          token_validator_factory_->CreateTokenValidator(local_id_,
                                                         remote_id_)));
      resume_callback.Run();
      break;

    case Method::PAIRED_SPAKE2_P224: {
      PairingHostAuthenticator* pairing_authenticator =
          new PairingHostAuthenticator(
              pairing_registry_, base::Bind(&V2Authenticator::CreateForHost,
                                            local_cert_, local_key_pair_),
              shared_secret_hash_);
      current_authenticator_.reset(pairing_authenticator);
      pairing_authenticator->Initialize(client_id_, preferred_initial_state,
                                        resume_callback);
      break;
    }

    case Method::PAIRED_SPAKE2_CURVE25519: {
      PairingHostAuthenticator* pairing_authenticator =
          new PairingHostAuthenticator(
              pairing_registry_,
              base::Bind(&Spake2Authenticator::CreateForHost, local_id_,
                         remote_id_, local_cert_, local_key_pair_),
              shared_secret_hash_);
      current_authenticator_.reset(pairing_authenticator);
      pairing_authenticator->Initialize(client_id_, preferred_initial_state,
                                        resume_callback);
      break;
    }

    case Method::SHARED_SECRET_SPAKE2_CURVE25519:
      current_authenticator_ = Spake2Authenticator::CreateForHost(
          local_id_, remote_id_, local_cert_, local_key_pair_,
          shared_secret_hash_, preferred_initial_state);
      resume_callback.Run();
      break;

    case Method::SHARED_SECRET_SPAKE2_P224:
      current_authenticator_ = V2Authenticator::CreateForHost(
          local_cert_, local_key_pair_, shared_secret_hash_,
          preferred_initial_state);
      resume_callback.Run();
      break;
  }
}

}  // namespace protocol
}  // namespace remoting
