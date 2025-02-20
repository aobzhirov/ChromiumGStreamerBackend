// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api_unittest.h"

#include "base/values.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/web_contents_tester.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/test_extensions_browser_client.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/value_builder.h"

namespace utils = extensions::api_test_utils;

namespace extensions {

ApiUnitTest::ApiUnitTest()
    : notification_service_(content::NotificationService::Create()) {
}

ApiUnitTest::~ApiUnitTest() {
}

void ApiUnitTest::SetUp() {
  ExtensionsTest::SetUp();

  thread_bundle_.reset(new content::TestBrowserThreadBundle(
      content::TestBrowserThreadBundle::DEFAULT));
  user_prefs::UserPrefs::Set(browser_context(), &testing_pref_service_);

  extension_ = ExtensionBuilder()
                   .SetManifest(DictionaryBuilder()
                                    .Set("name", "Test")
                                    .Set("version", "1.0")
                                    .Build())
                   .SetLocation(Manifest::UNPACKED)
                   .Build();
}

void ApiUnitTest::CreateBackgroundPage() {
  if (!contents_) {
    GURL url = BackgroundInfo::GetBackgroundURL(extension());
    if (url.is_empty())
      url = GURL(url::kAboutBlankURL);
    contents_.reset(
        content::WebContents::Create(content::WebContents::CreateParams(
            browser_context(),
            content::SiteInstance::CreateForURL(browser_context(), url))));
  }
}

scoped_ptr<base::Value> ApiUnitTest::RunFunctionAndReturnValue(
    UIThreadExtensionFunction* function,
    const std::string& args) {
  function->set_extension(extension());
  if (contents_)
    function->SetRenderFrameHost(contents_->GetMainFrame());
  return scoped_ptr<base::Value>(utils::RunFunctionAndReturnSingleResult(
      function, args, browser_context()));
}

scoped_ptr<base::DictionaryValue> ApiUnitTest::RunFunctionAndReturnDictionary(
    UIThreadExtensionFunction* function,
    const std::string& args) {
  base::Value* value = RunFunctionAndReturnValue(function, args).release();
  base::DictionaryValue* dict = NULL;

  if (value && !value->GetAsDictionary(&dict))
    delete value;

  // We expect to either have successfuly retrieved a dictionary from the value,
  // or the value to have been NULL.
  EXPECT_TRUE(dict || !value);
  return scoped_ptr<base::DictionaryValue>(dict);
}

scoped_ptr<base::ListValue> ApiUnitTest::RunFunctionAndReturnList(
    UIThreadExtensionFunction* function,
    const std::string& args) {
  base::Value* value = RunFunctionAndReturnValue(function, args).release();
  base::ListValue* list = NULL;

  if (value && !value->GetAsList(&list))
    delete value;

  // We expect to either have successfuly retrieved a list from the value,
  // or the value to have been NULL.
  EXPECT_TRUE(list || !value);
  return scoped_ptr<base::ListValue>(list);
}

std::string ApiUnitTest::RunFunctionAndReturnError(
    UIThreadExtensionFunction* function,
    const std::string& args) {
  function->set_extension(extension());
  if (contents_)
    function->SetRenderFrameHost(contents_->GetMainFrame());
  return utils::RunFunctionAndReturnError(function, args, browser_context());
}

void ApiUnitTest::RunFunction(UIThreadExtensionFunction* function,
                              const std::string& args) {
  RunFunctionAndReturnValue(function, args);
}

}  // namespace extensions
