// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl/chrome_expect_ct_reporter.h"

#include <string>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/common/chrome_switches.h"
#include "net/url_request/certificate_report_sender.h"

namespace {

std::string TimeToISO8601(const base::Time& t) {
  base::Time::Exploded exploded;
  t.UTCExplode(&exploded);
  return base::StringPrintf(
      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", exploded.year, exploded.month,
      exploded.day_of_month, exploded.hour, exploded.minute, exploded.second,
      exploded.millisecond);
}

scoped_ptr<base::ListValue> GetPEMEncodedChainAsList(
    const net::X509Certificate* cert_chain) {
  if (!cert_chain)
    return make_scoped_ptr(new base::ListValue());

  scoped_ptr<base::ListValue> result(new base::ListValue());
  std::vector<std::string> pem_encoded_chain;
  cert_chain->GetPEMEncodedChain(&pem_encoded_chain);
  for (const std::string& cert : pem_encoded_chain)
    result->Append(make_scoped_ptr(new base::StringValue(cert)));

  return result;
}

std::string SCTOriginToString(
    net::ct::SignedCertificateTimestamp::Origin origin) {
  switch (origin) {
    case net::ct::SignedCertificateTimestamp::SCT_EMBEDDED:
      return "embedded";
    case net::ct::SignedCertificateTimestamp::SCT_FROM_TLS_EXTENSION:
      return "from-tls-extension";
    case net::ct::SignedCertificateTimestamp::SCT_FROM_OCSP_RESPONSE:
      return "from-ocsp-response";
    default:
      NOTREACHED();
  }
  return "";
}

void AddUnknownSCT(
    const net::SignedCertificateTimestampAndStatus& sct_and_status,
    base::ListValue* list) {
  scoped_ptr<base::DictionaryValue> list_item(new base::DictionaryValue());
  list_item->SetString("origin", SCTOriginToString(sct_and_status.sct->origin));
  list->Append(std::move(list_item));
}

void AddInvalidSCT(
    const net::SignedCertificateTimestampAndStatus& sct_and_status,
    base::ListValue* list) {
  scoped_ptr<base::DictionaryValue> list_item(new base::DictionaryValue());
  list_item->SetString("origin", SCTOriginToString(sct_and_status.sct->origin));
  std::string log_id;
  base::Base64Encode(sct_and_status.sct->log_id, &log_id);
  list_item->SetString("id", log_id);
  list->Append(std::move(list_item));
}

void AddValidSCT(const net::SignedCertificateTimestampAndStatus& sct_and_status,
                 base::ListValue* list) {
  scoped_ptr<base::DictionaryValue> list_item(new base::DictionaryValue());
  list_item->SetString("origin", SCTOriginToString(sct_and_status.sct->origin));

  // The structure of the SCT object is defined in
  // http://tools.ietf.org/html/rfc6962#section-4.1.
  scoped_ptr<base::DictionaryValue> sct(new base::DictionaryValue());
  sct->SetInteger("sct_version", sct_and_status.sct->version);
  std::string log_id;
  base::Base64Encode(sct_and_status.sct->log_id, &log_id);
  sct->SetString("id", log_id);
  base::TimeDelta timestamp =
      sct_and_status.sct->timestamp - base::Time::UnixEpoch();
  sct->SetString("timestamp", base::Int64ToString(timestamp.InMilliseconds()));
  std::string extensions;
  base::Base64Encode(sct_and_status.sct->extensions, &extensions);
  sct->SetString("extensions", extensions);
  std::string signature;
  base::Base64Encode(sct_and_status.sct->signature.signature_data, &signature);
  sct->SetString("signature", signature);

  list_item->Set("sct", std::move(sct));
  list->Append(std::move(list_item));
}

}  // namespace

ChromeExpectCTReporter::ChromeExpectCTReporter(
    net::URLRequestContext* request_context)
    : report_sender_(new net::CertificateReportSender(
          request_context,
          net::CertificateReportSender::DO_NOT_SEND_COOKIES)) {}

ChromeExpectCTReporter::~ChromeExpectCTReporter() {}

void ChromeExpectCTReporter::OnExpectCTFailed(
    const net::HostPortPair& host_port_pair,
    const GURL& report_uri,
    const net::SSLInfo& ssl_info) {
  if (report_uri.is_empty())
    return;

  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableExpectCTReporting)) {
    return;
  }

  // TODO(estark): De-duplicate reports so that the same report isn't
  // sent too often in some period of time.

  base::DictionaryValue report;
  report.SetString("hostname", host_port_pair.host());
  report.SetInteger("port", host_port_pair.port());
  report.SetString("date-time", TimeToISO8601(base::Time::Now()));
  report.Set("served-certificate-chain",
             GetPEMEncodedChainAsList(ssl_info.unverified_cert.get()));
  report.Set("validated-certificate-chain",
             GetPEMEncodedChainAsList(ssl_info.cert.get()));

  scoped_ptr<base::ListValue> unknown_scts(new base::ListValue());
  scoped_ptr<base::ListValue> invalid_scts(new base::ListValue());
  scoped_ptr<base::ListValue> valid_scts(new base::ListValue());

  for (const auto& sct_and_status : ssl_info.signed_certificate_timestamps) {
    switch (sct_and_status.status) {
      case net::ct::SCT_STATUS_LOG_UNKNOWN:
        AddUnknownSCT(sct_and_status, unknown_scts.get());
        break;
      case net::ct::SCT_STATUS_INVALID:
        AddInvalidSCT(sct_and_status, invalid_scts.get());
        break;
      case net::ct::SCT_STATUS_OK:
        AddValidSCT(sct_and_status, valid_scts.get());
        break;
      default:
        NOTREACHED();
    }
  }

  report.Set("unknown-scts", std::move(unknown_scts));
  report.Set("invalid-scts", std::move(invalid_scts));
  report.Set("valid-scts", std::move(valid_scts));

  std::string serialized_report;
  if (!base::JSONWriter::Write(report, &serialized_report)) {
    LOG(ERROR) << "Failed to serialize Expect CT report";
    return;
  }

  report_sender_->Send(report_uri, serialized_report);
}
