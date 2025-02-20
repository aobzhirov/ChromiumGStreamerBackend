// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_ROUTER_ROUTE_REQUEST_RESULT_H_
#define CHROME_BROWSER_MEDIA_ROUTER_ROUTE_REQUEST_RESULT_H_

#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"

namespace media_router {

class MediaRoute;

// Holds the result of a successful or failed route request.
// On success:
// |route|: The route created or joined.
// |presentation_id|:
//     The presentation ID of the route created or joined. In the case of
//     |CreateRoute()|, the ID is generated by MediaRouter and is guaranteed to
//     be unique.
// |error|: Empty string.
// |result_code|: RouteRequestResult::OK
// On failure:
// |route|: nullptr
// |presentation_id|: Empty string.
// |error|: Non-empty string describing the error.
// |result_code|: A value from RouteRequestResult describing the error.
class RouteRequestResult {
 public:
  enum ResultCode {
    UNKNOWN_ERROR,
    OK,
    TIMED_OUT,
    INVALID_ORIGIN,
    OFF_THE_RECORD_MISMATCH
  };

  static scoped_ptr<RouteRequestResult> FromSuccess(
      scoped_ptr<MediaRoute> route,
      const std::string& presentation_id);
  static scoped_ptr<RouteRequestResult> FromError(const std::string& error,
                                                  ResultCode result_code);

  ~RouteRequestResult();

  // Note the caller does not own the returned MediaRoute. The caller must
  // create a copy if they wish to use it after this object is destroyed.
  const MediaRoute* route() const { return route_.get(); }
  std::string presentation_id() const { return presentation_id_; }
  std::string error() const { return error_; }
  ResultCode result_code() const { return result_code_; }

 private:
  RouteRequestResult(scoped_ptr<MediaRoute> route,
                     const std::string& presentation_id,
                     const std::string& error,
                     ResultCode result_code);

  scoped_ptr<MediaRoute> route_;
  std::string presentation_id_;
  std::string error_;
  ResultCode result_code_;

  DISALLOW_COPY_AND_ASSIGN(RouteRequestResult);
};

}  // namespace media_router

#endif  // CHROME_BROWSER_MEDIA_ROUTER_ROUTE_REQUEST_RESULT_H_
