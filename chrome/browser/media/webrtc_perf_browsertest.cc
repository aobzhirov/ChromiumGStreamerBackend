// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/media/webrtc_browsertest_base.h"
#include "chrome/browser/media/webrtc_browsertest_common.h"
#include "chrome/browser/media/webrtc_browsertest_perf.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/feature_h264_with_openh264_ffmpeg.h"
#include "content/public/common/features.h"
#include "content/public/test/browser_test_utils.h"
#include "media/base/media_switches.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/perf/perf_test.h"

static const char kMainWebrtcTestHtmlPage[] =
    "/webrtc/webrtc_jsep01_test.html";

// Performance browsertest for WebRTC. This test is manual since it takes long
// to execute and requires the reference files provided by the webrtc.DEPS
// solution (which is only available on WebRTC internal bots).
class WebRtcPerfBrowserTest : public WebRtcTestBase {
 public:
  void SetUpInProcessBrowserTestFixture() override {
    DetectErrorsInJavaScript();  // Look for errors in our rather complex js.
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Ensure the infobar is enabled, since we expect that in this test.
    EXPECT_FALSE(command_line->HasSwitch(switches::kUseFakeUIForMediaStream));

    // Play a suitable, somewhat realistic video file.
    base::FilePath input_video = test::GetReferenceFilesDir()
        .Append(test::kReferenceFileName360p)
        .AddExtension(test::kY4mFileExtension);
    command_line->AppendSwitchPath(switches::kUseFileForFakeVideoCapture,
                                   input_video);
    command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
  }

  // Tries to extract data from peerConnectionDataStore in the webrtc-internals
  // tab. The caller owns the parsed data. Returns NULL on failure.
  base::DictionaryValue* GetWebrtcInternalsData(
      content::WebContents* webrtc_internals_tab) {
    std::string all_stats_json = ExecuteJavascript(
        "window.domAutomationController.send("
        "    JSON.stringify(peerConnectionDataStore));",
        webrtc_internals_tab);

    scoped_ptr<base::Value> parsed_json =
        base::JSONReader::Read(all_stats_json);
    base::DictionaryValue* result;
    if (parsed_json.get() && parsed_json->GetAsDictionary(&result)) {
      ignore_result(parsed_json.release());
      return result;
    }

    return NULL;
  }

  const base::DictionaryValue* GetDataOnPeerConnection(
      const base::DictionaryValue* all_data,
      int peer_connection_index) {
    base::DictionaryValue::Iterator iterator(*all_data);

    for (int i = 0; i < peer_connection_index && !iterator.IsAtEnd();
        --peer_connection_index) {
      iterator.Advance();
    }

    const base::DictionaryValue* result;
    if (!iterator.IsAtEnd() && iterator.value().GetAsDictionary(&result))
      return result;

    return NULL;
  }

  scoped_ptr<base::DictionaryValue> MeasureWebRtcInternalsData(
      int duration_msec) {
    chrome::AddTabAt(browser(), GURL(), -1, true);
    ui_test_utils::NavigateToURL(browser(), GURL("chrome://webrtc-internals"));
    content::WebContents* webrtc_internals_tab =
        browser()->tab_strip_model()->GetActiveWebContents();

    test::SleepInJavascript(webrtc_internals_tab, duration_msec);

    return scoped_ptr<base::DictionaryValue>(
        GetWebrtcInternalsData(webrtc_internals_tab));
  }

  void RunsAudioVideoCall60SecsAndLogsInternalMetrics(
      const std::string& video_codec) {
    ASSERT_TRUE(test::HasReferenceFilesInCheckout());
    ASSERT_TRUE(embedded_test_server()->Start());

    ASSERT_GE(TestTimeouts::action_max_timeout().InSeconds(), 100)
        << "This is a long-running test; you must specify "
           "--ui-test-action-max-timeout to have a value of at least 100000.";

    content::WebContents* left_tab =
        OpenTestPageAndGetUserMediaInNewTab(kMainWebrtcTestHtmlPage);
    content::WebContents* right_tab =
        OpenTestPageAndGetUserMediaInNewTab(kMainWebrtcTestHtmlPage);

    SetupPeerconnectionWithLocalStream(left_tab);
    SetupPeerconnectionWithLocalStream(right_tab);

    NegotiateCall(left_tab, right_tab, video_codec);

    StartDetectingVideo(left_tab, "remote-view");
    StartDetectingVideo(right_tab, "remote-view");

    WaitForVideoToPlay(left_tab);
    WaitForVideoToPlay(right_tab);

    // Let values stabilize, bandwidth ramp up, etc.
    test::SleepInJavascript(left_tab, 60000);

    // Start measurements.
    scoped_ptr<base::DictionaryValue> all_data =
        MeasureWebRtcInternalsData(10000);
    ASSERT_TRUE(all_data.get() != NULL);

    const base::DictionaryValue* first_pc_dict =
        GetDataOnPeerConnection(all_data.get(), 0);
    ASSERT_TRUE(first_pc_dict != NULL);
    test::PrintBweForVideoMetrics(*first_pc_dict, "", video_codec);
    test::PrintMetricsForAllStreams(*first_pc_dict, "", video_codec);

    HangUp(left_tab);
    HangUp(right_tab);
  }

  void RunsOneWayCall60SecsAndLogsInternalMetrics(
      const std::string& video_codec) {
    ASSERT_TRUE(test::HasReferenceFilesInCheckout());
    ASSERT_TRUE(embedded_test_server()->Start());

    ASSERT_GE(TestTimeouts::action_max_timeout().InSeconds(), 100)
        << "This is a long-running test; you must specify "
           "--ui-test-action-max-timeout to have a value of at least 100000.";

    content::WebContents* left_tab =
        OpenTestPageAndGetUserMediaInNewTab(kMainWebrtcTestHtmlPage);
    content::WebContents* right_tab =
        OpenTestPageAndGetUserMediaInNewTab(kMainWebrtcTestHtmlPage);

    SetupPeerconnectionWithLocalStream(left_tab);
    SetupPeerconnectionWithoutLocalStream(right_tab);

    NegotiateCall(left_tab, right_tab, video_codec);

    // Remote video will only play in one tab since the call is one-way.
    StartDetectingVideo(right_tab, "remote-view");
    WaitForVideoToPlay(right_tab);

    // Let values stabilize, bandwidth ramp up, etc.
    test::SleepInJavascript(left_tab, 60000);

    scoped_ptr<base::DictionaryValue> all_data =
        MeasureWebRtcInternalsData(10000);
    ASSERT_TRUE(all_data.get() != NULL);

    // This assumes the sending peer connection is always listed first in the
    // data store, and the receiving second.
    const base::DictionaryValue* first_pc_dict =
        GetDataOnPeerConnection(all_data.get(), 0);
    ASSERT_TRUE(first_pc_dict != NULL);
    test::PrintBweForVideoMetrics(*first_pc_dict, "_sendonly", video_codec);
    test::PrintMetricsForSendStreams(*first_pc_dict, "_sendonly", video_codec);

    const base::DictionaryValue* second_pc_dict =
        GetDataOnPeerConnection(all_data.get(), 1);
    ASSERT_TRUE(second_pc_dict != NULL);
    test::PrintBweForVideoMetrics(*second_pc_dict, "_recvonly", video_codec);
    test::PrintMetricsForRecvStreams(*second_pc_dict, "_recvonly", video_codec);

    HangUp(left_tab);
    HangUp(right_tab);
  }
};

// This is manual for its long execution time.

IN_PROC_BROWSER_TEST_F(
    WebRtcPerfBrowserTest,
    MANUAL_RunsAudioVideoCall60SecsAndLogsInternalMetricsVp8) {
  RunsAudioVideoCall60SecsAndLogsInternalMetrics("VP8");
}

IN_PROC_BROWSER_TEST_F(
    WebRtcPerfBrowserTest,
    MANUAL_RunsAudioVideoCall60SecsAndLogsInternalMetricsVp9) {
  RunsAudioVideoCall60SecsAndLogsInternalMetrics("VP9");
}

#if BUILDFLAG(RTC_USE_H264)

IN_PROC_BROWSER_TEST_F(
    WebRtcPerfBrowserTest,
    MANUAL_RunsAudioVideoCall60SecsAndLogsInternalMetricsH264) {
  // Only run test if run-time feature corresponding to |rtc_use_h264| is on.
  if (!base::FeatureList::IsEnabled(content::kWebRtcH264WithOpenH264FFmpeg)) {
    LOG(WARNING) << "Run-time feature WebRTC-H264WithOpenH264FFmpeg disabled. "
        "Skipping WebRtcPerfBrowserTest.MANUAL_RunsAudioVideoCall60SecsAndLogs"
        "InternalMetricsH264 (test \"OK\")";
    return;
  }
  RunsAudioVideoCall60SecsAndLogsInternalMetrics("H264");
}

#endif  // BUILDFLAG(RTC_USE_H264)

IN_PROC_BROWSER_TEST_F(
    WebRtcPerfBrowserTest,
    MANUAL_RunsOneWayCall60SecsAndLogsInternalMetricsDefault) {
  RunsOneWayCall60SecsAndLogsInternalMetrics("");
}

