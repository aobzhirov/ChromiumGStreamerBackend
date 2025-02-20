// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_TEST_RUNNER_WEB_TEST_INTERFACES_H_
#define COMPONENTS_TEST_RUNNER_WEB_TEST_INTERFACES_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "components/test_runner/test_runner_export.h"

namespace blink {
class WebAppBannerClient;
class WebAudioDevice;
class WebFrame;
class WebFrameClient;
class WebMediaStreamCenter;
class WebMediaStreamCenterClient;
class WebMIDIAccessor;
class WebMIDIAccessorClient;
class WebRTCPeerConnectionHandler;
class WebRTCPeerConnectionHandlerClient;
class WebThemeEngine;
class WebURL;
class WebView;
}

namespace test_runner {

class AppBannerClient;
class TestInterfaces;
class WebFrameTestClient;
class WebTestDelegate;
class WebTestProxyBase;
class WebTestRunner;

class TEST_RUNNER_EXPORT WebTestInterfaces {
 public:
  WebTestInterfaces();
  ~WebTestInterfaces();

  void SetWebView(blink::WebView* web_view, WebTestProxyBase* proxy);
  void SetDelegate(WebTestDelegate* delegate);
  void BindTo(blink::WebFrame* frame);
  void ResetAll();
  void SetTestIsRunning(bool running);
  void ConfigureForTestWithURL(const blink::WebURL& test_url,
                               bool generate_pixels);
  void SetSendWheelGestures(bool send_gestures);

  WebTestRunner* TestRunner();
  blink::WebThemeEngine* ThemeEngine();

  blink::WebMediaStreamCenter* CreateMediaStreamCenter(
      blink::WebMediaStreamCenterClient* client);
  blink::WebRTCPeerConnectionHandler* CreateWebRTCPeerConnectionHandler(
      blink::WebRTCPeerConnectionHandlerClient* client);

  blink::WebMIDIAccessor* CreateMIDIAccessor(
      blink::WebMIDIAccessorClient* client);

  blink::WebAudioDevice* CreateAudioDevice(double sample_rate);

  scoped_ptr<blink::WebAppBannerClient> CreateAppBannerClient();
  AppBannerClient* GetAppBannerClient();

  TestInterfaces* GetTestInterfaces();

  // Gets a borrowed pointer to a WebFrameClient implementation providing
  // test behavior (i.e. forwarding javascript console output to the test
  // harness).  The caller should guarantee that the returned pointer
  // won't be used beyond the lifetime of WebTestInterfaces.
  blink::WebFrameClient* GetWebFrameTestClient();

 private:
  scoped_ptr<TestInterfaces> interfaces_;
  scoped_ptr<WebFrameTestClient> web_frame_test_client_;

  DISALLOW_COPY_AND_ASSIGN(WebTestInterfaces);
};

}  // namespace test_runner

#endif  // COMPONENTS_TEST_RUNNER_WEB_TEST_INTERFACES_H_
