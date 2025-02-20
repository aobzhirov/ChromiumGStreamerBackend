// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/interstitials/html_web_interstitial_impl.h"

#include <utility>

#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "ios/web/interstitials/web_interstitial_facade_delegate.h"
#include "ios/web/public/interstitials/web_interstitial_delegate.h"
#include "ios/web/public/web_state/ui/crw_web_view_content_view.h"
#import "ios/web/public/web_view_creation_util.h"
#import "ios/web/web_state/ui/web_view_js_utils.h"
#include "ios/web/web_state/web_state_impl.h"
#import "net/base/mac/url_conversions.h"
#include "ui/gfx/geometry/size.h"

// The delegate of the web view that is used to display the HTML content.
// It intercepts JavaScript-triggered commands and forwards them
// to the interstitial.
@interface CRWWebInterstitialImplWKWebViewDelegate
    : NSObject<WKNavigationDelegate>
// Initializes a CRWWebInterstitialImplWKWebViewDelegate which will
// forward JavaScript commands from its WKWebView to |interstitial|.
- (instancetype)initWithInterstitial:
    (web::HtmlWebInterstitialImpl*)interstitial;
@end

@implementation CRWWebInterstitialImplWKWebViewDelegate {
  web::HtmlWebInterstitialImpl* _interstitial;
}

- (instancetype)initWithInterstitial:
    (web::HtmlWebInterstitialImpl*)interstitial {
  self = [super init];
  if (self)
    _interstitial = interstitial;
  return self;
}

- (BOOL)shouldStartLoadWithRequest:(NSURLRequest*)request {
  NSString* requestString = [[request URL] absoluteString];
  // If the request is a JavaScript-triggered command, parse it and forward the
  // command to |interstitial_|.
  NSString* const commandPrefix = @"js-command:";
  if ([requestString hasPrefix:commandPrefix]) {
    DCHECK(_interstitial);
    _interstitial->CommandReceivedFromWebView(
        [requestString substringFromIndex:commandPrefix.length]);
    return NO;
  }
  return YES;
}

#pragma mark -
#pragma mark WKNavigationDelegate methods

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
                    decisionHandler:
                        (void (^)(WKNavigationActionPolicy))decisionHandler {
  decisionHandler([self shouldStartLoadWithRequest:navigationAction.request]
                      ? WKNavigationActionPolicyAllow
                      : WKNavigationActionPolicyCancel);
}

@end

#pragma mark -

namespace web {

// static
WebInterstitial* WebInterstitial::CreateHtmlInterstitial(
    WebState* web_state,
    bool new_navigation,
    const GURL& url,
    scoped_ptr<HtmlWebInterstitialDelegate> delegate) {
  WebStateImpl* web_state_impl = static_cast<WebStateImpl*>(web_state);
  return new HtmlWebInterstitialImpl(web_state_impl, new_navigation, url,
                                     std::move(delegate));
}

HtmlWebInterstitialImpl::HtmlWebInterstitialImpl(
    WebStateImpl* web_state,
    bool new_navigation,
    const GURL& url,
    scoped_ptr<HtmlWebInterstitialDelegate> delegate)
    : WebInterstitialImpl(web_state, new_navigation, url),
      delegate_(std::move(delegate)) {
  DCHECK(delegate_);
}

HtmlWebInterstitialImpl::~HtmlWebInterstitialImpl() {
}

void HtmlWebInterstitialImpl::CommandReceivedFromWebView(NSString* command) {
  delegate_->CommandReceived(base::SysNSStringToUTF8(command));
}

CRWContentView* HtmlWebInterstitialImpl::GetContentView() const {
  return content_view_.get();
}

void HtmlWebInterstitialImpl::PrepareForDisplay() {
  if (!content_view_) {
    web_view_delegate_.reset([[CRWWebInterstitialImplWKWebViewDelegate alloc]
        initWithInterstitial:this]);
    web_view_.reset(
        web::CreateWKWebView(CGRectZero, GetWebStateImpl()->GetBrowserState()));
    [web_view_ setNavigationDelegate:web_view_delegate_];
    [web_view_ setAutoresizingMask:(UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight)];
    NSString* html = base::SysUTF8ToNSString(delegate_->GetHtmlContents());
    [web_view_ loadHTMLString:html baseURL:net::NSURLWithGURL(GetUrl())];
    content_view_.reset([[CRWWebViewContentView alloc]
        initWithWebView:web_view_
             scrollView:[web_view_ scrollView]]);
  }
}

WebInterstitialDelegate* HtmlWebInterstitialImpl::GetDelegate() const {
  return delegate_.get();
}

void HtmlWebInterstitialImpl::EvaluateJavaScript(
    NSString* script,
    JavaScriptCompletion completionHandler) {
  web::EvaluateJavaScript(web_view_, script, completionHandler);
}

}  // namespace web
