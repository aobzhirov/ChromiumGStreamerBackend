// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/geolocation/simple_geolocation_request.h"

#include <stddef.h>

#include <algorithm>
#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chromeos/geolocation/simple_geolocation_provider.h"
#include "chromeos/geolocation/simple_geolocation_request_test_monitor.h"
#include "google_apis/google_api_keys.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"

// Location resolve timeout is usually 1 minute, so 2 minutes with 50 buckets
// should be enough.
#define UMA_HISTOGRAM_LOCATION_RESPONSE_TIMES(name, sample)         \
  UMA_HISTOGRAM_CUSTOM_TIMES(name,                                  \
                             sample,                                \
                             base::TimeDelta::FromMilliseconds(10), \
                             base::TimeDelta::FromMinutes(2),       \
                             50)

namespace chromeos {

namespace {

// The full request text. (no parameters are supported by now)
const char kSimpleGeolocationRequestBody[] = "{\"considerIp\": \"true\"}";

// Request data
const char kConsiderIp[] = "considerIp";
const char kWifiAccessPoints[] = "wifiAccessPoints";

// WiFi access point objects.
const char kMacAddress[] = "macAddress";
const char kSignalStrength[] = "signalStrength";
const char kAge[] = "age";
const char kChannel[] = "channel";
const char kSignalToNoiseRatio[] = "signalToNoiseRatio";

// Response data.
const char kLocationString[] = "location";
const char kLatString[] = "lat";
const char kLngString[] = "lng";
const char kAccuracyString[] = "accuracy";
// Error object and its contents.
const char kErrorString[] = "error";
// "errors" array in "erorr" object is ignored.
const char kCodeString[] = "code";
const char kMessageString[] = "message";

// We are using "sparse" histograms for the number of retry attempts,
// so we need to explicitly limit maximum value (in case something goes wrong).
const size_t kMaxRetriesValueInHistograms = 20;

// Sleep between geolocation request retry on HTTP error.
const unsigned int kResolveGeolocationRetrySleepOnServerErrorSeconds = 5;

// Sleep between geolocation request retry on bad server response.
const unsigned int kResolveGeolocationRetrySleepBadResponseSeconds = 10;

enum SimpleGeolocationRequestEvent {
  // NOTE: Do not renumber these as that would confuse interpretation of
  // previously logged data. When making changes, also update the enum list
  // in tools/metrics/histograms/histograms.xml to keep it in sync.
  SIMPLE_GEOLOCATION_REQUEST_EVENT_REQUEST_START = 0,
  SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_SUCCESS = 1,
  SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_NOT_OK = 2,
  SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_EMPTY = 3,
  SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_MALFORMED = 4,

  // NOTE: Add entries only immediately above this line.
  SIMPLE_GEOLOCATION_REQUEST_EVENT_COUNT = 5
};

enum SimpleGeolocationRequestResult {
  // NOTE: Do not renumber these as that would confuse interpretation of
  // previously logged data. When making changes, also update the enum list
  // in tools/metrics/histograms/histograms.xml to keep it in sync.
  SIMPLE_GEOLOCATION_REQUEST_RESULT_SUCCESS = 0,
  SIMPLE_GEOLOCATION_REQUEST_RESULT_FAILURE = 1,
  SIMPLE_GEOLOCATION_REQUEST_RESULT_SERVER_ERROR = 2,
  SIMPLE_GEOLOCATION_REQUEST_RESULT_CANCELLED = 3,

  // NOTE: Add entries only immediately above this line.
  SIMPLE_GEOLOCATION_REQUEST_RESULT_COUNT = 4
};

SimpleGeolocationRequestTestMonitor* g_test_request_hook = nullptr;

// Too many requests (more than 1) mean there is a problem in implementation.
void RecordUmaEvent(SimpleGeolocationRequestEvent event) {
  UMA_HISTOGRAM_ENUMERATION("SimpleGeolocation.Request.Event",
                            event,
                            SIMPLE_GEOLOCATION_REQUEST_EVENT_COUNT);
}

void RecordUmaResponseCode(int code) {
  UMA_HISTOGRAM_SPARSE_SLOWLY("SimpleGeolocation.Request.ResponseCode", code);
}

// Slow geolocation resolve leads to bad user experience.
void RecordUmaResponseTime(base::TimeDelta elapsed, bool success) {
  if (success) {
    UMA_HISTOGRAM_LOCATION_RESPONSE_TIMES(
        "SimpleGeolocation.Request.ResponseSuccessTime", elapsed);
  } else {
    UMA_HISTOGRAM_LOCATION_RESPONSE_TIMES(
        "SimpleGeolocation.Request.ResponseFailureTime", elapsed);
  }
}

void RecordUmaResult(SimpleGeolocationRequestResult result, size_t retries) {
  UMA_HISTOGRAM_ENUMERATION("SimpleGeolocation.Request.Result",
                            result,
                            SIMPLE_GEOLOCATION_REQUEST_RESULT_COUNT);
  UMA_HISTOGRAM_SPARSE_SLOWLY("SimpleGeolocation.Request.Retries",
                              std::min(retries, kMaxRetriesValueInHistograms));
}

// Creates the request url to send to the server.
GURL GeolocationRequestURL(const GURL& url) {
  if (url != SimpleGeolocationProvider::DefaultGeolocationProviderURL())
    return url;

  std::string api_key = google_apis::GetAPIKey();
  if (api_key.empty())
    return url;

  std::string query(url.query());
  if (!query.empty())
    query += "&";
  query += "key=" + net::EscapeQueryParamValue(api_key, true);
  GURL::Replacements replacements;
  replacements.SetQueryStr(query);
  return url.ReplaceComponents(replacements);
}

void PrintGeolocationError(const GURL& server_url,
                           const std::string& message,
                           Geoposition* position) {
  position->status = Geoposition::STATUS_SERVER_ERROR;
  position->error_message =
      base::StringPrintf("SimpleGeolocation provider at '%s' : %s.",
                         server_url.GetOrigin().spec().c_str(),
                         message.c_str());
  VLOG(1) << "SimpleGeolocationRequest::GetGeolocationFromResponse() : "
          << position->error_message;
}

// Parses the server response body. Returns true if parsing was successful.
// Sets |*position| to the parsed Geolocation if a valid position was received,
// otherwise leaves it unchanged.
bool ParseServerResponse(const GURL& server_url,
                         const std::string& response_body,
                         Geoposition* position) {
  DCHECK(position);

  if (response_body.empty()) {
    PrintGeolocationError(
        server_url, "Server returned empty response", position);
    RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_EMPTY);
    return false;
  }
  VLOG(1) << "SimpleGeolocationRequest::ParseServerResponse() : "
             "Parsing response '" << response_body << "'";

  // Parse the response, ignoring comments.
  std::string error_msg;
  scoped_ptr<base::Value> response_value = base::JSONReader::ReadAndReturnError(
      response_body, base::JSON_PARSE_RFC, NULL, &error_msg);
  if (response_value == NULL) {
    PrintGeolocationError(
        server_url, "JSONReader failed: " + error_msg, position);
    RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_MALFORMED);
    return false;
  }

  base::DictionaryValue* response_object = NULL;
  if (!response_value->GetAsDictionary(&response_object)) {
    PrintGeolocationError(
        server_url,
        "Unexpected response type : " +
            base::StringPrintf("%u", response_value->GetType()),
        position);
    RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_MALFORMED);
    return false;
  }

  base::DictionaryValue* error_object = NULL;
  base::DictionaryValue* location_object = NULL;
  response_object->GetDictionaryWithoutPathExpansion(kLocationString,
                                                     &location_object);
  response_object->GetDictionaryWithoutPathExpansion(kErrorString,
                                                     &error_object);

  position->timestamp = base::Time::Now();

  if (error_object) {
    if (!error_object->GetStringWithoutPathExpansion(
            kMessageString, &(position->error_message))) {
      position->error_message = "Server returned error without message.";
    }

    // Ignore result (code defaults to zero).
    error_object->GetIntegerWithoutPathExpansion(kCodeString,
                                                 &(position->error_code));
  } else {
    position->error_message.erase();
  }

  if (location_object) {
    if (!location_object->GetDoubleWithoutPathExpansion(
            kLatString, &(position->latitude))) {
      PrintGeolocationError(server_url, "Missing 'lat' attribute.", position);
      RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_MALFORMED);
      return false;
    }
    if (!location_object->GetDoubleWithoutPathExpansion(
            kLngString, &(position->longitude))) {
      PrintGeolocationError(server_url, "Missing 'lon' attribute.", position);
      RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_MALFORMED);
      return false;
    }
    if (!response_object->GetDoubleWithoutPathExpansion(
            kAccuracyString, &(position->accuracy))) {
      PrintGeolocationError(
          server_url, "Missing 'accuracy' attribute.", position);
      RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_MALFORMED);
      return false;
    }
  }

  if (error_object) {
    position->status = Geoposition::STATUS_SERVER_ERROR;
    return false;
  }
  // Empty response is STATUS_OK but not Valid().
  position->status = Geoposition::STATUS_OK;
  return true;
}

// Attempts to extract a position from the response. Detects and indicates
// various failure cases.
bool GetGeolocationFromResponse(bool http_success,
                                int status_code,
                                const std::string& response_body,
                                const GURL& server_url,
                                Geoposition* position) {
  VLOG(1) << "GetGeolocationFromResponse(http_success=" << http_success
          << ", status_code=" << status_code << "): response_body:\n"
          << response_body;

  // HttpPost can fail for a number of reasons. Most likely this is because
  // we're offline, or there was no response.
  if (!http_success) {
    PrintGeolocationError(server_url, "No response received", position);
    RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_EMPTY);
    return false;
  }
  if (status_code != net::HTTP_OK) {
    std::string message = "Returned error code ";
    message += base::IntToString(status_code);
    PrintGeolocationError(server_url, message, position);
    RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_RESPONSE_NOT_OK);
    return false;
  }

  return ParseServerResponse(server_url, response_body, position);
}

void ReportUmaHasWiFiAccessPoints(bool value) {
  UMA_HISTOGRAM_BOOLEAN("SimpleGeolocation.Request.HasWiFiAccessPoints", value);
}

}  // namespace

SimpleGeolocationRequest::SimpleGeolocationRequest(
    net::URLRequestContextGetter* url_context_getter,
    const GURL& service_url,
    base::TimeDelta timeout,
    scoped_ptr<WifiAccessPointVector> wifi_data)
    : url_context_getter_(url_context_getter),
      service_url_(service_url),
      retry_sleep_on_server_error_(base::TimeDelta::FromSeconds(
          kResolveGeolocationRetrySleepOnServerErrorSeconds)),
      retry_sleep_on_bad_response_(base::TimeDelta::FromSeconds(
          kResolveGeolocationRetrySleepBadResponseSeconds)),
      timeout_(timeout),
      retries_(0),
      wifi_data_(wifi_data.release()) {}

SimpleGeolocationRequest::~SimpleGeolocationRequest() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // If callback is not empty, request is cancelled.
  if (!callback_.is_null()) {
    RecordUmaResponseTime(base::Time::Now() - request_started_at_, false);
    RecordUmaResult(SIMPLE_GEOLOCATION_REQUEST_RESULT_CANCELLED, retries_);
  }

  if (g_test_request_hook)
    g_test_request_hook->OnRequestCreated(this);
}

std::string SimpleGeolocationRequest::FormatRequestBody() const {
  if (!wifi_data_) {
    ReportUmaHasWiFiAccessPoints(false);
    return std::string(kSimpleGeolocationRequestBody);
  }

  scoped_ptr<base::DictionaryValue> request(new base::DictionaryValue);
  request->SetBooleanWithoutPathExpansion(kConsiderIp, true);

  base::ListValue* wifi_access_points(new base::ListValue);
  request->SetWithoutPathExpansion(kWifiAccessPoints, wifi_access_points);

  for (const WifiAccessPoint& access_point : *wifi_data_) {
    base::DictionaryValue* access_point_dictionary = new base::DictionaryValue;
    wifi_access_points->Append(access_point_dictionary);

    access_point_dictionary->SetStringWithoutPathExpansion(
        kMacAddress, access_point.mac_address);
    access_point_dictionary->SetIntegerWithoutPathExpansion(
        kSignalStrength, access_point.signal_strength);
    if (!access_point.timestamp.is_null()) {
      access_point_dictionary->SetStringWithoutPathExpansion(
          kAge,
          base::Int64ToString(
              (base::Time::Now() - access_point.timestamp).InMilliseconds()));
    }

    access_point_dictionary->SetIntegerWithoutPathExpansion(
        kChannel, access_point.channel);
    access_point_dictionary->SetIntegerWithoutPathExpansion(
        kSignalToNoiseRatio, access_point.signal_to_noise);
  }
  std::string result;
  if (!base::JSONWriter::Write(*request, &result)) {
    ReportUmaHasWiFiAccessPoints(false);
    return std::string(kSimpleGeolocationRequestBody);
  }
  ReportUmaHasWiFiAccessPoints(wifi_data_->size());

  return result;
}

void SimpleGeolocationRequest::StartRequest() {
  DCHECK(thread_checker_.CalledOnValidThread());
  RecordUmaEvent(SIMPLE_GEOLOCATION_REQUEST_EVENT_REQUEST_START);
  ++retries_;

  const std::string request_body = FormatRequestBody();
  VLOG(1) << "SimpleGeolocationRequest::StartRequest(): request body:\n"
          << request_body;

  url_fetcher_ =
      net::URLFetcher::Create(request_url_, net::URLFetcher::POST, this);
  url_fetcher_->SetRequestContext(url_context_getter_.get());
  url_fetcher_->SetUploadData("application/json", request_body);
  url_fetcher_->SetLoadFlags(net::LOAD_BYPASS_CACHE |
                             net::LOAD_DISABLE_CACHE |
                             net::LOAD_DO_NOT_SAVE_COOKIES |
                             net::LOAD_DO_NOT_SEND_COOKIES |
                             net::LOAD_DO_NOT_SEND_AUTH_DATA);

  // Call test hook before asynchronous request actually starts.
  if (g_test_request_hook)
    g_test_request_hook->OnStart(this);

  url_fetcher_->Start();
}

void SimpleGeolocationRequest::MakeRequest(const ResponseCallback& callback) {
  callback_ = callback;
  request_url_ = GeolocationRequestURL(service_url_);
  timeout_timer_.Start(
      FROM_HERE, timeout_, this, &SimpleGeolocationRequest::OnTimeout);
  request_started_at_ = base::Time::Now();
  StartRequest();
}

// static
void SimpleGeolocationRequest::SetTestMonitor(
    SimpleGeolocationRequestTestMonitor* monitor) {
  g_test_request_hook = monitor;
}

std::string SimpleGeolocationRequest::FormatRequestBodyForTesting() const {
  return FormatRequestBody();
}

void SimpleGeolocationRequest::Retry(bool server_error) {
  base::TimeDelta delay(server_error ? retry_sleep_on_server_error_
                                     : retry_sleep_on_bad_response_);
  request_scheduled_.Start(
      FROM_HERE, delay, this, &SimpleGeolocationRequest::StartRequest);
}

void SimpleGeolocationRequest::OnURLFetchComplete(
    const net::URLFetcher* source) {
  DCHECK_EQ(url_fetcher_.get(), source);

  net::URLRequestStatus status = source->GetStatus();
  int response_code = source->GetResponseCode();
  RecordUmaResponseCode(response_code);

  std::string data;
  source->GetResponseAsString(&data);
  const bool parse_success = GetGeolocationFromResponse(
      status.is_success(), response_code, data, source->GetURL(), &position_);
  const bool server_error =
      !status.is_success() || (response_code >= 500 && response_code < 600);
  const bool success = parse_success && position_.Valid();
  url_fetcher_.reset();

  DVLOG(1) << "SimpleGeolocationRequest::OnURLFetchComplete(): position={"
           << position_.ToString() << "}";

  if (!success) {
    Retry(server_error);
    return;
  }
  const base::TimeDelta elapsed = base::Time::Now() - request_started_at_;
  RecordUmaResponseTime(elapsed, success);

  RecordUmaResult(SIMPLE_GEOLOCATION_REQUEST_RESULT_SUCCESS, retries_);

  ReplyAndDestroySelf(elapsed, server_error);
  // "this" is already destroyed here.
}

void SimpleGeolocationRequest::ReplyAndDestroySelf(
    const base::TimeDelta elapsed,
    bool server_error) {
  url_fetcher_.reset();
  timeout_timer_.Stop();
  request_scheduled_.Stop();

  ResponseCallback callback = callback_;

  // Empty callback is used to identify "completed or not yet started request".
  callback_.Reset();

  // callback.Run() usually destroys SimpleGeolocationRequest, because this is
  // the way callback is implemented in GeolocationProvider.
  callback.Run(position_, server_error, elapsed);
  // "this" is already destroyed here.
}

void SimpleGeolocationRequest::OnTimeout() {
  const SimpleGeolocationRequestResult result =
      (position_.status == Geoposition::STATUS_SERVER_ERROR
           ? SIMPLE_GEOLOCATION_REQUEST_RESULT_SERVER_ERROR
           : SIMPLE_GEOLOCATION_REQUEST_RESULT_FAILURE);
  RecordUmaResult(result, retries_);
  position_.status = Geoposition::STATUS_TIMEOUT;
  const base::TimeDelta elapsed = base::Time::Now() - request_started_at_;
  ReplyAndDestroySelf(elapsed, true /* server_error */);
  // "this" is already destroyed here.
}

}  // namespace chromeos
