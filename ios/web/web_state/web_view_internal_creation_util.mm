// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/web_view_internal_creation_util.h"

#import <objc/runtime.h>
#import <WebKit/WebKit.h>

#include "base/logging.h"
#include "base/mac/scoped_nsobject.h"
#include "base/strings/sys_string_conversions.h"
#import "ios/web/alloc_with_zone_interceptor.h"
#include "ios/web/public/active_state_manager.h"
#include "ios/web/public/browser_state.h"
#import "ios/web/public/browsing_data_partition.h"
#include "ios/web/public/web_client.h"
#import "ios/web/public/web_view_counter.h"
#import "ios/web/public/web_view_creation_util.h"
#import "ios/web/weak_nsobject_counter.h"
#import "ios/web/web_state/ui/wk_web_view_configuration_provider.h"
#import "ios/web/web_view_counter_impl.h"

#if !defined(NDEBUG)

namespace {
// Returns the counter of all the active WKWebViews.
// DEPRECATED. Please use web::WebViewCounter instead.
// TODO(shreyasv): Remove this once all callers have stopped using it.
// crbug.com/480507
web::WeakNSObjectCounter& GetActiveWKWebViewCounter() {
  static web::WeakNSObjectCounter active_wk_web_view_counter;
  return active_wk_web_view_counter;
}

// Decides if web views can be created.
bool gAllowWebViewCreation = NO;

// Decides if web views are associated with an ActiveStateManager which is
// active.
bool gWebViewsNeedActiveStateManager = NO;

}  // namespace

@interface WKWebView (CRWAdditions)
@end

@implementation WKWebView (CRWAdditions)

+ (void)load {
  id (^allocator)(Class klass, NSZone* zone) = ^id(Class klass, NSZone* zone) {
    if (web::IsWebViewAllocInitAllowed()) {
      return NSAllocateObject(klass, 0, zone);
    }
    // You have hit this because you are trying to create a WKWebView directly.
    // Please use one of the web::CreateWKWKWebView methods that vend a
    // WKWebView instead.
    NOTREACHED();
    return nil;
  };
  web::AddAllocWithZoneMethod([WKWebView class], allocator);
}

@end

#endif  // !defined(NDEBUG)

namespace web {

namespace {

// Verifies the preconditions for creating a WKWebView. Must be called before
// a WKWebView is allocated. Not verifying the preconditions before creating
// a WKWebView will lead to undefined behavior.
void VerifyWKWebViewCreationPreConditions(
    BrowserState* browser_state,
    WKWebViewConfiguration* configuration) {
  DCHECK(browser_state);
  DCHECK(configuration);
  DCHECK(web::BrowsingDataPartition::IsSynchronized());
  WKWebViewConfigurationProvider& config_provider =
      WKWebViewConfigurationProvider::FromBrowserState(browser_state);
  DCHECK_EQ([config_provider.GetWebViewConfiguration() processPool],
            [configuration processPool]);
}

// Called before a WKWebView is created.
void PreWKWebViewCreation(BrowserState* browser_state) {
  DCHECK(browser_state);
  DCHECK(GetWebClient());
  GetWebClient()->PreWebViewCreation();

#if !defined(NDEBUG)
  if (IsWebViewAllocInitAllowed() && gWebViewsNeedActiveStateManager) {
    DCHECK(BrowserState::GetActiveStateManager(browser_state)->IsActive());
  }
#endif
}

// Called after the WKWebView |web_view| is created.
void PostWKWebViewCreation(WKWebView* web_view, BrowserState* browser_state) {
  DCHECK(web_view);

#if !defined(NDEBUG)
  GetActiveWKWebViewCounter().Insert(web_view);
#endif

  WebViewCounterImpl* web_view_counter =
      WebViewCounterImpl::FromBrowserState(browser_state);
  DCHECK(web_view_counter);
  web_view_counter->InsertWKWebView(web_view);

  // TODO(stuartmorgan): Figure out how to make this work; two different client
  // methods for the two web view types?
  // web::GetWebClient()->PostWebViewCreation(result);
}

}  // namespace

UIWebView* CreateWebView(CGRect frame) {
  DCHECK(web::GetWebClient());
  web::GetWebClient()->PreWebViewCreation();

  UIWebView* result = [[UIWebView alloc] initWithFrame:frame];

  // Disable data detector types. Safari does the same.
  [result setDataDetectorTypes:UIDataDetectorTypeNone];
  [result setScalesPageToFit:YES];

  // By default UIWebView uses a very sluggish scroll speed. Set it to a more
  // reasonable value.
  result.scrollView.decelerationRate = UIScrollViewDecelerationRateNormal;

  web::GetWebClient()->PostWebViewCreation(result);

  return result;
}

WKWebView* CreateWKWebView(CGRect frame,
                           WKWebViewConfiguration* configuration,
                           BrowserState* browser_state,
                           BOOL use_desktop_user_agent) {
  WKWebView* web_view = CreateWKWebView(frame, configuration, browser_state);
  DCHECK(web::GetWebClient());
  web_view.customUserAgent = base::SysUTF8ToNSString(
      web::GetWebClient()->GetUserAgent(use_desktop_user_agent));
  return web_view;
}

WKWebView* CreateWKWebView(CGRect frame,
                           WKWebViewConfiguration* configuration,
                           BrowserState* browser_state) {
  VerifyWKWebViewCreationPreConditions(browser_state, configuration);

  PreWKWebViewCreation(browser_state);
#if !defined(NDEBUG)
  bool previous_allow_web_view_creation_value = gAllowWebViewCreation;
  gAllowWebViewCreation = true;
#endif
  WKWebView* result =
      [[WKWebView alloc] initWithFrame:frame configuration:configuration];
#if !defined(NDEBUG)
  gAllowWebViewCreation = previous_allow_web_view_creation_value;
#endif
  PostWKWebViewCreation(result, browser_state);

  // By default the web view uses a very sluggish scroll speed. Set it to a more
  // reasonable value.
  result.scrollView.decelerationRate = UIScrollViewDecelerationRateNormal;

  return result;
}

NSUInteger GetActiveWKWebViewsCount() {
#if defined(NDEBUG)
  // This should not be used in release builds.
  CHECK(0);
  return 0;
#else
  return GetActiveWKWebViewCounter().Size();
#endif
}

#if !defined(NDEBUG)
bool IsWebViewAllocInitAllowed() {
  static dispatch_once_t once_token = 0;
  dispatch_once(&once_token, ^{
    DCHECK(GetWebClient());
    gAllowWebViewCreation = GetWebClient()->AllowWebViewAllocInit();
    if (!gAllowWebViewCreation) {
      gWebViewsNeedActiveStateManager =
          GetWebClient()->WebViewsNeedActiveStateManager();
    }
  });
  return gAllowWebViewCreation;
}
#endif

}  // namespace web
