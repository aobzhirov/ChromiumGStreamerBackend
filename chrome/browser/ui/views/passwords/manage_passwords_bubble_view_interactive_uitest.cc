// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/passwords/manage_passwords_bubble_view.h"

#include <utility>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/macros.h"
#include "base/metrics/histogram_samples.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/passwords/manage_passwords_test.h"
#include "chrome/browser/ui/passwords/passwords_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/passwords/manage_passwords_bubble_view.h"
#include "chrome/browser/ui/views/passwords/manage_passwords_icon_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/common/content_features.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::Eq;
using testing::Field;
using testing::_;

namespace {

const char kDisplayDispositionMetric[] = "PasswordBubble.DisplayDisposition";

// A helper class that will create FakeURLFetcher and record the requested URLs.
class TestURLFetcherCallback {
 public:
  scoped_ptr<net::FakeURLFetcher> CreateURLFetcher(
      const GURL& url,
      net::URLFetcherDelegate* d,
      const std::string& response_data,
      net::HttpStatusCode response_code,
      net::URLRequestStatus::Status status) {
    OnRequestDone(url);
    return scoped_ptr<net::FakeURLFetcher>(new net::FakeURLFetcher(
        url, d, response_data, response_code, status));
  }

  MOCK_METHOD1(OnRequestDone, void(const GURL&));
};

bool IsBubbleShowing() {
  return ManagePasswordsBubbleView::manage_password_bubble() &&
      ManagePasswordsBubbleView::manage_password_bubble()->
          GetWidget()->IsVisible();
}

}  // namespace

namespace metrics_util = password_manager::metrics_util;

class ManagePasswordsBubbleViewTest : public ManagePasswordsTest {
 public:
  ManagePasswordsBubbleViewTest() {}
  ~ManagePasswordsBubbleViewTest() override {}

  ManagePasswordsIconView* view() override {
    BrowserView* browser_view = static_cast<BrowserView*>(browser()->window());
    return browser_view->GetToolbarView()
        ->location_bar()
        ->manage_passwords_icon_view();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ManagePasswordsBubbleViewTest);
};

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, BasicOpenAndClose) {
  EXPECT_FALSE(IsBubbleShowing());
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());
  const ManagePasswordsBubbleView* bubble =
      ManagePasswordsBubbleView::manage_password_bubble();
  EXPECT_TRUE(bubble->initially_focused_view());
  EXPECT_FALSE(bubble->GetFocusManager()->GetFocusedView());
  ManagePasswordsBubbleView::CloseCurrentBubble();
  EXPECT_FALSE(IsBubbleShowing());

  // And, just for grins, ensure that we can re-open the bubble.
  ManagePasswordsBubbleView::ShowBubble(
      browser()->tab_strip_model()->GetActiveWebContents(),
      ManagePasswordsBubbleView::USER_GESTURE);
  EXPECT_TRUE(IsBubbleShowing());
  bubble = ManagePasswordsBubbleView::manage_password_bubble();
  EXPECT_TRUE(bubble->initially_focused_view());
  EXPECT_EQ(bubble->initially_focused_view(),
            bubble->GetFocusManager()->GetFocusedView());
  ManagePasswordsBubbleView::CloseCurrentBubble();
  EXPECT_FALSE(IsBubbleShowing());
}

// Same as 'BasicOpenAndClose', but use the command rather than the static
// method directly.
IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, CommandControlsBubble) {
  // The command only works if the icon is visible, so get into management mode.
  SetupManagingPasswords();
  EXPECT_FALSE(IsBubbleShowing());
  ExecuteManagePasswordsCommand();
  EXPECT_TRUE(IsBubbleShowing());
  const ManagePasswordsBubbleView* bubble =
      ManagePasswordsBubbleView::manage_password_bubble();
  EXPECT_TRUE(bubble->initially_focused_view());
  EXPECT_EQ(bubble->initially_focused_view(),
            bubble->GetFocusManager()->GetFocusedView());
  ManagePasswordsBubbleView::CloseCurrentBubble();
  EXPECT_FALSE(IsBubbleShowing());

  // And, just for grins, ensure that we can re-open the bubble.
  ExecuteManagePasswordsCommand();
  EXPECT_TRUE(IsBubbleShowing());
  ManagePasswordsBubbleView::CloseCurrentBubble();
  EXPECT_FALSE(IsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest,
                       CommandExecutionInManagingState) {
  SetupManagingPasswords();
  EXPECT_FALSE(IsBubbleShowing());
  ExecuteManagePasswordsCommand();
  EXPECT_TRUE(IsBubbleShowing());

  scoped_ptr<base::HistogramSamples> samples(
      GetSamples(kDisplayDispositionMetric));
  EXPECT_EQ(
      0,
      samples->GetCount(
          metrics_util::AUTOMATIC_WITH_PASSWORD_PENDING));
  EXPECT_EQ(0,
            samples->GetCount(
                metrics_util::MANUAL_WITH_PASSWORD_PENDING));
  EXPECT_EQ(1,
            samples->GetCount(
                metrics_util::MANUAL_MANAGE_PASSWORDS));
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest,
                       CommandExecutionInAutomaticState) {
  // Open with pending password: automagical!
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());

  // Bubble should not be focused by default.
  EXPECT_FALSE(ManagePasswordsBubbleView::manage_password_bubble()->
      GetFocusManager()->GetFocusedView());
  // Bubble can be active if user clicks it.
  EXPECT_TRUE(ManagePasswordsBubbleView::manage_password_bubble()->
      CanActivate());

  scoped_ptr<base::HistogramSamples> samples(
      GetSamples(kDisplayDispositionMetric));
  EXPECT_EQ(
      1,
      samples->GetCount(
          metrics_util::AUTOMATIC_WITH_PASSWORD_PENDING));
  EXPECT_EQ(0,
            samples->GetCount(
                metrics_util::MANUAL_WITH_PASSWORD_PENDING));
  EXPECT_EQ(0,
            samples->GetCount(
                metrics_util::MANUAL_MANAGE_PASSWORDS));
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest,
                       CommandExecutionInPendingState) {
  // Open once with pending password: automagical!
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());
  ManagePasswordsBubbleView::CloseCurrentBubble();
  // This opening should be measured as manual.
  ExecuteManagePasswordsCommand();
  EXPECT_TRUE(IsBubbleShowing());

  scoped_ptr<base::HistogramSamples> samples(
      GetSamples(kDisplayDispositionMetric));
  EXPECT_EQ(
      1,
      samples->GetCount(
          metrics_util::AUTOMATIC_WITH_PASSWORD_PENDING));
  EXPECT_EQ(1,
            samples->GetCount(
                metrics_util::MANUAL_WITH_PASSWORD_PENDING));
  EXPECT_EQ(0,
            samples->GetCount(
                metrics_util::MANUAL_MANAGE_PASSWORDS));
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest,
                       CommandExecutionInAutomaticSaveState) {
  SetupAutomaticPassword();
  EXPECT_TRUE(IsBubbleShowing());
  ManagePasswordsBubbleView::CloseCurrentBubble();
  content::RunAllPendingInMessageLoop();
  // Re-opening should count as manual.
  ExecuteManagePasswordsCommand();
  EXPECT_TRUE(IsBubbleShowing());

  scoped_ptr<base::HistogramSamples> samples(
      GetSamples(kDisplayDispositionMetric));
  EXPECT_EQ(
      1,
      samples->GetCount(
          metrics_util::AUTOMATIC_GENERATED_PASSWORD_CONFIRMATION));
  EXPECT_EQ(0,
            samples->GetCount(
                metrics_util::MANUAL_WITH_PASSWORD_PENDING));
  EXPECT_EQ(1,
            samples->GetCount(
                metrics_util::MANUAL_MANAGE_PASSWORDS));
}

// Flaky on Windows (http://crbug.com/523255).
#if defined(OS_WIN)
#define MAYBE_CloseOnClick DISABLED_CloseOnClick
#else
#define MAYBE_CloseOnClick CloseOnClick
#endif
IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, MAYBE_CloseOnClick) {
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());
  EXPECT_FALSE(ManagePasswordsBubbleView::manage_password_bubble()->
      GetFocusManager()->GetFocusedView());
  ui_test_utils::ClickOnView(browser(), VIEW_ID_TAB_CONTAINER);
  EXPECT_FALSE(IsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, CloseOnEsc) {
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
      false, false, false, false));
  EXPECT_FALSE(IsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, CloseOnKey) {
  content::WindowedNotificationObserver focus_observer(
      content::NOTIFICATION_FOCUS_CHANGED_IN_PAGE,
      content::NotificationService::AllSources());
  ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html;charset=utf-8,<input type=\"text\" autofocus>"));
  focus_observer.Wait();
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());
  EXPECT_FALSE(ManagePasswordsBubbleView::manage_password_bubble()->
      GetFocusManager()->GetFocusedView());
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_TAB_CONTAINER));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(web_contents->GetRenderViewHost()->IsFocusedElementEditable());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_K,
      false, false, false, false));
  EXPECT_FALSE(IsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, TwoTabsWithBubble) {
  // Set up the first tab with the bubble.
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());
  // Set up the second tab and bring the bubble again.
  AddTabAtIndex(1, GURL("http://example.com/"), ui::PAGE_TRANSITION_TYPED);
  TabStripModel* tab_model = browser()->tab_strip_model();
  tab_model->ActivateTabAt(1, true);
  EXPECT_FALSE(IsBubbleShowing());
  EXPECT_EQ(1, tab_model->active_index());
  SetupPendingPassword();
  EXPECT_TRUE(IsBubbleShowing());
  // Back to the first tab.
  tab_model->ActivateTabAt(0, true);
  EXPECT_FALSE(IsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, AutoSignin) {
  base::FeatureList::ClearInstanceForTesting();
  scoped_ptr<base::FeatureList> feature_list(new base::FeatureList);
  feature_list->InitializeFromCommandLine(
      features::kCredentialManagementAPI.name, std::string());
  base::FeatureList::SetInstance(std::move(feature_list));
  ASSERT_TRUE(base::FeatureList::IsEnabled(features::kCredentialManagementAPI));

  ScopedVector<autofill::PasswordForm> local_credentials;
  test_form()->origin = GURL("https://example.com");
  test_form()->display_name = base::ASCIIToUTF16("Peter");
  test_form()->username_value = base::ASCIIToUTF16("pet12@gmail.com");
  GURL icon_url("https://google.com/icon.png");
  test_form()->icon_url = icon_url;
  local_credentials.push_back(new autofill::PasswordForm(*test_form()));

  // Prepare to capture the network request.
  TestURLFetcherCallback url_callback;
  net::FakeURLFetcherFactory factory(
      NULL,
      base::Bind(&TestURLFetcherCallback::CreateURLFetcher,
                 base::Unretained(&url_callback)));
  factory.SetFakeResponse(icon_url, std::string(), net::HTTP_OK,
                          net::URLRequestStatus::FAILED);
  EXPECT_CALL(url_callback, OnRequestDone(icon_url));

  SetupAutoSignin(std::move(local_credentials));
  EXPECT_TRUE(IsBubbleShowing());

  ManagePasswordsBubbleView::CloseCurrentBubble();
  EXPECT_FALSE(IsBubbleShowing());
  content::RunAllPendingInMessageLoop();
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(password_manager::ui::MANAGE_STATE,
            PasswordsModelDelegateFromWebContents(web_contents)->GetState());
}

IN_PROC_BROWSER_TEST_F(ManagePasswordsBubbleViewTest, AutoSigninNoFocus) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  test_form()->origin = GURL("https://example.com");
  test_form()->display_name = base::ASCIIToUTF16("Peter");
  test_form()->username_value = base::ASCIIToUTF16("pet12@gmail.com");
  local_credentials.push_back(new autofill::PasswordForm(*test_form()));

  // Open another window with focus.
  Browser* focused_window = CreateBrowser(browser()->profile());
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(focused_window));
  content::RunAllPendingInMessageLoop();

  EXPECT_FALSE(browser()->window()->IsActive());
  ManagePasswordsBubbleView::set_auto_signin_toast_timeout(0);
  SetupAutoSignin(std::move(local_credentials));
  content::RunAllPendingInMessageLoop();
  EXPECT_TRUE(IsBubbleShowing());

  // Bring the first window back. The toast closes by timeout.
  focused_window->window()->Close();
  browser()->window()->Activate();
  content::RunAllPendingInMessageLoop();
  EXPECT_TRUE(browser()->window()->IsActive());
  EXPECT_FALSE(IsBubbleShowing());
}
