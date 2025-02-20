// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEB_TEST_WEB_PUBLIC_TEST_TEST_WEB_CLIENT_H_
#define WEB_TEST_WEB_PUBLIC_TEST_TEST_WEB_CLIENT_H_

#import <Foundation/Foundation.h>

#include "base/compiler_specific.h"
#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "ios/web/public/web_client.h"

namespace web {

// A WebClient used for testing purposes.
class TestWebClient : public web::WebClient {
 public:
  TestWebClient();
  ~TestWebClient() override;
  // WebClient implementation.
  NSString* GetEarlyPageScript(web::WebViewType web_view_type) const override;
  bool WebViewsNeedActiveStateManager() const override;

  // Changes Early Page Script for testing purposes.
  void SetEarlyPageScript(NSString* page_script,
                          web::WebViewType web_view_type);

 private:
  base::scoped_nsobject<NSMutableDictionary> early_page_scripts_;
};

}  // namespace web

#endif // WEB_TEST_WEB_PUBLIC_TEST_TEST_WEB_CLIENT_H_
