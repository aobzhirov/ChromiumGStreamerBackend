// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/geolocation/simple_geolocation_provider.h"

#include <algorithm>
#include <iterator>

#include "base/bind.h"
#include "chromeos/geolocation/geoposition.h"
#include "chromeos/network/geolocation_handler.h"
#include "chromeos/network/network_handler.h"
#include "net/url_request/url_request_context_getter.h"

namespace chromeos {

namespace {
const char kDefaultGeolocationProviderUrl[] =
    "https://www.googleapis.com/geolocation/v1/geolocate?";

scoped_ptr<WifiAccessPointVector> GetAccessPointData() {
  if (!chromeos::NetworkHandler::Get()->geolocation_handler()->wifi_enabled())
    return nullptr;

  scoped_ptr<WifiAccessPointVector> result(new chromeos::WifiAccessPointVector);
  int64_t age_ms = 0;
  if (!NetworkHandler::Get()->geolocation_handler()->GetWifiAccessPoints(
          result.get(), &age_ms)) {
    return nullptr;
  }
  return result;
}

}  // namespace

SimpleGeolocationProvider::SimpleGeolocationProvider(
    net::URLRequestContextGetter* url_context_getter,
    const GURL& url)
    : url_context_getter_(url_context_getter), url_(url) {
}

SimpleGeolocationProvider::~SimpleGeolocationProvider() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void SimpleGeolocationProvider::RequestGeolocation(
    base::TimeDelta timeout,
    bool send_wifi_access_points,
    SimpleGeolocationRequest::ResponseCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  SimpleGeolocationRequest* request(new SimpleGeolocationRequest(
      url_context_getter_.get(), url_, timeout,
      send_wifi_access_points ? GetAccessPointData() : nullptr));
  requests_.push_back(request);

  // SimpleGeolocationProvider owns all requests. It is safe to pass unretained
  // "this" because destruction of SimpleGeolocationProvider cancels all
  // requests.
  SimpleGeolocationRequest::ResponseCallback callback_tmp(
      base::Bind(&SimpleGeolocationProvider::OnGeolocationResponse,
                 base::Unretained(this),
                 request,
                 callback));
  request->MakeRequest(callback_tmp);
}

// static
GURL SimpleGeolocationProvider::DefaultGeolocationProviderURL() {
  return GURL(kDefaultGeolocationProviderUrl);
}

void SimpleGeolocationProvider::OnGeolocationResponse(
    SimpleGeolocationRequest* request,
    SimpleGeolocationRequest::ResponseCallback callback,
    const Geoposition& geoposition,
    bool server_error,
    const base::TimeDelta elapsed) {
  DCHECK(thread_checker_.CalledOnValidThread());

  callback.Run(geoposition, server_error, elapsed);

  ScopedVector<SimpleGeolocationRequest>::iterator position =
      std::find(requests_.begin(), requests_.end(), request);
  DCHECK(position != requests_.end());
  if (position != requests_.end()) {
    std::swap(*position, *requests_.rbegin());
    requests_.resize(requests_.size() - 1);
  }
}

}  // namespace chromeos
