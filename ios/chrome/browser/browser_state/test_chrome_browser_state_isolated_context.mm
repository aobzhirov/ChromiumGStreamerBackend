// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/browser_state/test_chrome_browser_state_isolated_context.h"

#include "base/logging.h"
#include "components/syncable_prefs/pref_service_syncable.h"
#include "ios/web/public/web_thread.h"
#include "net/url_request/url_request_test_util.h"

TestChromeBrowserStateWithIsolatedContext::
    TestChromeBrowserStateWithIsolatedContext()
    : TestChromeBrowserState(base::FilePath(),
                             scoped_ptr<syncable_prefs::PrefServiceSyncable>(),
                             TestingFactories(),
                             RefcountedTestingFactories()),
      main_context_called_(false),
      request_context_(new net::TestURLRequestContextGetter(
          web::WebThread::GetTaskRunnerForThread(web::WebThread::IO))) {}

TestChromeBrowserStateWithIsolatedContext::
    ~TestChromeBrowserStateWithIsolatedContext() {}

bool TestChromeBrowserStateWithIsolatedContext::MainContextCalled() const {
  return main_context_called_;
}

IOSWebViewFactoryExternalService
TestChromeBrowserStateWithIsolatedContext::SharingService() {
  return SSO_AUTHENTICATION;
}

net::URLRequestContextGetter*
TestChromeBrowserStateWithIsolatedContext::GetRequestContext() {
  main_context_called_ = true;
  return TestChromeBrowserState::GetRequestContext();
}

net::URLRequestContextGetter*
TestChromeBrowserStateWithIsolatedContext::CreateIsolatedRequestContext(
    const base::FilePath& partition_path) {
  return request_context_.get();
}
