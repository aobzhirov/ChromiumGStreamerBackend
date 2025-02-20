// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_tokenizer.h"
#include "google_apis/gcm/engine/gcm_registration_request_handler.h"
#include "google_apis/gcm/engine/gcm_request_test_base.h"
#include "google_apis/gcm/engine/instance_id_get_token_request_handler.h"
#include "google_apis/gcm/monitoring/fake_gcm_stats_recorder.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_status.h"

namespace gcm {

namespace {
const uint64_t kAndroidId = 42UL;
const char kAppId[] = "TestAppId";
const char kDeveloperId[] = "Project1";
const char kLoginHeader[] = "AidLogin";
const char kRegistrationURL[] = "http://foo.bar/register";
const uint64_t kSecurityToken = 77UL;
const int kGCMVersion = 40;
const char kInstanceId[] = "IID1";
const char kScope[] = "GCM";

}  // namespace

class RegistrationRequestTest : public GCMRequestTestBase {
 public:
  RegistrationRequestTest();
  ~RegistrationRequestTest() override;

  void RegistrationCallback(RegistrationRequest::Status status,
                            const std::string& registration_id);

  void CompleteFetch() override;

  void set_max_retry_count(int max_retry_count) {
    max_retry_count_ = max_retry_count;
  }

 protected:
  int max_retry_count_;
  RegistrationRequest::Status status_;
  std::string registration_id_;
  bool callback_called_;
  std::map<std::string, std::string> extras_;
  scoped_ptr<RegistrationRequest> request_;
  FakeGCMStatsRecorder recorder_;
};

RegistrationRequestTest::RegistrationRequestTest()
    : max_retry_count_(2),
      status_(RegistrationRequest::SUCCESS),
      callback_called_(false) {}

RegistrationRequestTest::~RegistrationRequestTest() {}

void RegistrationRequestTest::RegistrationCallback(
    RegistrationRequest::Status status,
    const std::string& registration_id) {
  status_ = status;
  registration_id_ = registration_id;
  callback_called_ = true;
}

void RegistrationRequestTest::CompleteFetch() {
  registration_id_.clear();
  status_ = RegistrationRequest::SUCCESS;
  callback_called_ = false;

  GCMRequestTestBase::CompleteFetch();
}

class GCMRegistrationRequestTest : public RegistrationRequestTest {
 public:
  GCMRegistrationRequestTest();
  ~GCMRegistrationRequestTest() override;

  void CreateRequest(const std::string& sender_ids);
};

GCMRegistrationRequestTest::GCMRegistrationRequestTest() {
}

GCMRegistrationRequestTest::~GCMRegistrationRequestTest() {
}

void GCMRegistrationRequestTest::CreateRequest(const std::string& sender_ids) {
  RegistrationRequest::RequestInfo request_info(
      kAndroidId, kSecurityToken, kAppId);
  scoped_ptr<GCMRegistrationRequestHandler> request_handler(
      new GCMRegistrationRequestHandler(sender_ids));
  request_.reset(new RegistrationRequest(
      GURL(kRegistrationURL), request_info, std::move(request_handler),
      GetBackoffPolicy(),
      base::Bind(&RegistrationRequestTest::RegistrationCallback,
                 base::Unretained(this)),
      max_retry_count_, url_request_context_getter(), &recorder_, sender_ids));
}

TEST_F(GCMRegistrationRequestTest, RequestSuccessful) {
  set_max_retry_count(0);
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, RequestDataAndURL) {
  CreateRequest(kDeveloperId);
  request_->Start();

  // Get data sent by request.
  net::TestURLFetcher* fetcher = GetFetcher();
  ASSERT_TRUE(fetcher);

  EXPECT_EQ(GURL(kRegistrationURL), fetcher->GetOriginalURL());

  // Verify that authorization header was put together properly.
  net::HttpRequestHeaders headers;
  fetcher->GetExtraRequestHeaders(&headers);
  std::string auth_header;
  headers.GetHeader(net::HttpRequestHeaders::kAuthorization, &auth_header);
  base::StringTokenizer auth_tokenizer(auth_header, " :");
  ASSERT_TRUE(auth_tokenizer.GetNext());
  EXPECT_EQ(kLoginHeader, auth_tokenizer.token());
  ASSERT_TRUE(auth_tokenizer.GetNext());
  EXPECT_EQ(base::Uint64ToString(kAndroidId), auth_tokenizer.token());
  ASSERT_TRUE(auth_tokenizer.GetNext());
  EXPECT_EQ(base::Uint64ToString(kSecurityToken), auth_tokenizer.token());

  std::map<std::string, std::string> expected_pairs;
  expected_pairs["app"] = kAppId;
  expected_pairs["sender"] = kDeveloperId;
  expected_pairs["device"] = base::Uint64ToString(kAndroidId);

  // Verify data was formatted properly.
  std::string upload_data = fetcher->upload_data();
  base::StringTokenizer data_tokenizer(upload_data, "&=");
  while (data_tokenizer.GetNext()) {
    std::map<std::string, std::string>::iterator iter =
        expected_pairs.find(data_tokenizer.token());
    ASSERT_TRUE(iter != expected_pairs.end());
    ASSERT_TRUE(data_tokenizer.GetNext());
    EXPECT_EQ(iter->second, data_tokenizer.token());
    // Ensure that none of the keys appears twice.
    expected_pairs.erase(iter);
  }

  EXPECT_EQ(0UL, expected_pairs.size());
}

TEST_F(GCMRegistrationRequestTest, RequestRegistrationWithMultipleSenderIds) {
  CreateRequest("sender1,sender2@gmail.com");
  request_->Start();

  net::TestURLFetcher* fetcher = GetFetcher();
  ASSERT_TRUE(fetcher);

  // Verify data was formatted properly.
  std::string upload_data = fetcher->upload_data();
  base::StringTokenizer data_tokenizer(upload_data, "&=");

  // Skip all tokens until you hit entry for senders.
  while (data_tokenizer.GetNext() && data_tokenizer.token() != "sender")
    continue;

  ASSERT_TRUE(data_tokenizer.GetNext());
  std::string senders(net::UnescapeURLComponent(
      data_tokenizer.token(),
      net::UnescapeRule::PATH_SEPARATORS |
          net::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS));
  base::StringTokenizer sender_tokenizer(senders, ",");
  ASSERT_TRUE(sender_tokenizer.GetNext());
  EXPECT_EQ("sender1", sender_tokenizer.token());
  ASSERT_TRUE(sender_tokenizer.GetNext());
  EXPECT_EQ("sender2@gmail.com", sender_tokenizer.token());
}

TEST_F(GCMRegistrationRequestTest, ResponseParsing) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseHttpStatusNotOK) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_UNAUTHORIZED, "token=2501");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseMissingRegistrationId) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_OK, "");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  SetResponse(net::HTTP_OK, "some error in response");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  // Ensuring a retry happened and succeeds.
  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseDeviceRegistrationError) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_OK, "Error=PHONE_REGISTRATION_ERROR");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  // Ensuring a retry happened and succeeds.
  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseAuthenticationError) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_UNAUTHORIZED,
                             "Error=AUTHENTICATION_FAILED");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  // Ensuring a retry happened and succeeds.
  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseInvalidParameters) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_OK, "Error=INVALID_PARAMETERS");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::INVALID_PARAMETERS, status_);
  EXPECT_EQ(std::string(), registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseInvalidSender) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_OK, "Error=INVALID_SENDER");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::INVALID_SENDER, status_);
  EXPECT_EQ(std::string(), registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseInvalidSenderBadRequest) {
  CreateRequest("sender1");
  request_->Start();

  SetResponse(net::HTTP_BAD_REQUEST, "Error=INVALID_SENDER");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::INVALID_SENDER, status_);
  EXPECT_EQ(std::string(), registration_id_);
}

TEST_F(GCMRegistrationRequestTest, RequestNotSuccessful) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_OK, "token=2501");

  net::TestURLFetcher* fetcher = GetFetcher();
  ASSERT_TRUE(fetcher);
  GetFetcher()->set_status(net::URLRequestStatus::FromError(net::ERR_FAILED));

  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  // Ensuring a retry happened and succeeded.
  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, ResponseHttpNotOk) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_GATEWAY_TIMEOUT, "token=2501");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  // Ensuring a retry happened and succeeded.
  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(GCMRegistrationRequestTest, MaximumAttemptsReachedWithZeroRetries) {
  set_max_retry_count(0);
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_GATEWAY_TIMEOUT, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::REACHED_MAX_RETRIES, status_);
  EXPECT_EQ(std::string(), registration_id_);
}

TEST_F(GCMRegistrationRequestTest, MaximumAttemptsReached) {
  CreateRequest("sender1,sender2");
  request_->Start();

  SetResponse(net::HTTP_GATEWAY_TIMEOUT, "token=2501");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  SetResponse(net::HTTP_GATEWAY_TIMEOUT, "token=2501");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  SetResponse(net::HTTP_GATEWAY_TIMEOUT, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::REACHED_MAX_RETRIES, status_);
  EXPECT_EQ(std::string(), registration_id_);
}

class InstanceIDGetTokenRequestTest : public RegistrationRequestTest {
 public:
  InstanceIDGetTokenRequestTest();
  ~InstanceIDGetTokenRequestTest() override;

  void CreateRequest(const std::string& instance_id,
                     const std::string& authorized_entity,
                     const std::string& scope,
                     const std::map<std::string, std::string>& options);
};

InstanceIDGetTokenRequestTest::InstanceIDGetTokenRequestTest() {
}

InstanceIDGetTokenRequestTest::~InstanceIDGetTokenRequestTest() {
}

void InstanceIDGetTokenRequestTest::CreateRequest(
    const std::string& instance_id,
    const std::string& authorized_entity,
    const std::string& scope,
    const std::map<std::string, std::string>& options) {
  RegistrationRequest::RequestInfo request_info(
      kAndroidId, kSecurityToken, kAppId);
  scoped_ptr<InstanceIDGetTokenRequestHandler> request_handler(
      new InstanceIDGetTokenRequestHandler(
          instance_id, authorized_entity, scope, kGCMVersion, options));
  request_.reset(new RegistrationRequest(
      GURL(kRegistrationURL), request_info, std::move(request_handler),
      GetBackoffPolicy(),
      base::Bind(&RegistrationRequestTest::RegistrationCallback,
                 base::Unretained(this)),
      max_retry_count_, url_request_context_getter(), &recorder_,
      authorized_entity));
}

TEST_F(InstanceIDGetTokenRequestTest, RequestSuccessful) {
  std::map<std::string, std::string> options;
  options["Foo"] = "Bar";

  set_max_retry_count(0);
  CreateRequest(kInstanceId, kDeveloperId, kScope, options);
  request_->Start();

  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

TEST_F(InstanceIDGetTokenRequestTest, RequestDataAndURL) {
  std::map<std::string, std::string> options;
  options["Foo"] = "Bar";
  CreateRequest(kInstanceId, kDeveloperId, kScope, options);
  request_->Start();

  // Get data sent by request.
  net::TestURLFetcher* fetcher = GetFetcher();
  ASSERT_TRUE(fetcher);

  EXPECT_EQ(GURL(kRegistrationURL), fetcher->GetOriginalURL());

  // Verify that the no-cookie flag is set.
  int flags = fetcher->GetLoadFlags();
  EXPECT_TRUE(flags & net::LOAD_DO_NOT_SEND_COOKIES);
  EXPECT_TRUE(flags & net::LOAD_DO_NOT_SAVE_COOKIES);

  // Verify that authorization header was put together properly.
  net::HttpRequestHeaders headers;
  fetcher->GetExtraRequestHeaders(&headers);
  std::string auth_header;
  headers.GetHeader(net::HttpRequestHeaders::kAuthorization, &auth_header);
  base::StringTokenizer auth_tokenizer(auth_header, " :");
  ASSERT_TRUE(auth_tokenizer.GetNext());
  EXPECT_EQ(kLoginHeader, auth_tokenizer.token());
  ASSERT_TRUE(auth_tokenizer.GetNext());
  EXPECT_EQ(base::Uint64ToString(kAndroidId), auth_tokenizer.token());
  ASSERT_TRUE(auth_tokenizer.GetNext());
  EXPECT_EQ(base::Uint64ToString(kSecurityToken), auth_tokenizer.token());

  std::map<std::string, std::string> expected_pairs;
  expected_pairs["gmsv"] = base::IntToString(kGCMVersion);
  expected_pairs["app"] = kAppId;
  expected_pairs["sender"] = kDeveloperId;
  expected_pairs["X-subtype"] = kDeveloperId;
  expected_pairs["device"] = base::Uint64ToString(kAndroidId);
  expected_pairs["appid"] = kInstanceId;
  expected_pairs["scope"] = kScope;
  expected_pairs["X-scope"] = kScope;
  expected_pairs["X-Foo"] = "Bar";

  // Verify data was formatted properly.
  std::string upload_data = fetcher->upload_data();
  base::StringTokenizer data_tokenizer(upload_data, "&=");
  while (data_tokenizer.GetNext()) {
    std::map<std::string, std::string>::iterator iter =
        expected_pairs.find(data_tokenizer.token());
    ASSERT_TRUE(iter != expected_pairs.end());
    ASSERT_TRUE(data_tokenizer.GetNext());
    EXPECT_EQ(iter->second, data_tokenizer.token());
    // Ensure that none of the keys appears twice.
    expected_pairs.erase(iter);
  }

  EXPECT_EQ(0UL, expected_pairs.size());
}

TEST_F(InstanceIDGetTokenRequestTest, ResponseHttpStatusNotOK) {
  std::map<std::string, std::string> options;
  CreateRequest(kInstanceId, kDeveloperId, kScope, options);
  request_->Start();

  SetResponse(net::HTTP_UNAUTHORIZED, "token=2501");
  CompleteFetch();

  EXPECT_FALSE(callback_called_);

  SetResponse(net::HTTP_OK, "token=2501");
  CompleteFetch();

  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(RegistrationRequest::SUCCESS, status_);
  EXPECT_EQ("2501", registration_id_);
}

}  // namespace gcm
