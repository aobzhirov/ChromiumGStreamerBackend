// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/metrics_service_client.h"

namespace metrics {

base::string16 MetricsServiceClient::GetRegistryBackupKey() {
  return base::string16();
}

bool MetricsServiceClient::IsReportingPolicyManaged() {
  return false;
}

MetricsServiceClient::EnableMetricsDefault
MetricsServiceClient::GetDefaultOptIn() {
  return DEFAULT_UNKNOWN;
}

}  // namespace metrics
