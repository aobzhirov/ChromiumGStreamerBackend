// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/apps/app_browsertest_util.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/keep_alive_registry.h"
#include "chrome/browser/lifetime/keep_alive_types.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "extensions/browser/app_window/native_app_window.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#include "ui/base/test/scoped_fake_nswindow_fullscreen.h"
#endif

#if defined(OS_WIN)
#include <windows.h>
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_win.h"
#include "ui/views/win/hwnd_message_handler_delegate.h"
#include "ui/views/win/hwnd_util.h"
#endif

using extensions::AppWindow;
using extensions::NativeAppWindow;

// Helper class that has to be created in the stack to check if the fullscreen
// setting of a NativeWindow has changed since the creation of the object.
class FullscreenChangeWaiter {
 public:
  explicit FullscreenChangeWaiter(NativeAppWindow* window)
      : window_(window),
        initial_fullscreen_state_(window_->IsFullscreen()) {}

  void Wait() {
    while (initial_fullscreen_state_ == window_->IsFullscreen())
      content::RunAllPendingInMessageLoop();
  }

 private:
  NativeAppWindow* window_;
  bool initial_fullscreen_state_;

  DISALLOW_COPY_AND_ASSIGN(FullscreenChangeWaiter);
};

class AppWindowInteractiveTest : public extensions::PlatformAppBrowserTest {
 public:
  bool RunAppWindowInteractiveTest(const char* testName) {
    ExtensionTestMessageListener launched_listener("Launched", true);
    LoadAndLaunchPlatformApp("window_api_interactive", &launched_listener);

    extensions::ResultCatcher catcher;
    launched_listener.Reply(testName);

    if (!catcher.GetNextResult()) {
      message_ = catcher.message();
      return false;
    }

    return true;
  }

  bool SimulateKeyPress(ui::KeyboardCode key) {
    return ui_test_utils::SendKeyPressToWindowSync(
        GetFirstAppWindow()->GetNativeWindow(),
        key,
        false,
        false,
        false,
        false);
  }

  // This method will wait until the application is able to ack a key event.
  void WaitUntilKeyFocus() {
    ExtensionTestMessageListener key_listener("KeyReceived", false);

    while (!key_listener.was_satisfied()) {
      ASSERT_TRUE(SimulateKeyPress(ui::VKEY_Z));
      content::RunAllPendingInMessageLoop();
    }
  }

  // This test is a method so that we can test with each frame type.
  void TestOuterBoundsHelper(const std::string& frame_type);
};

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, ESCLeavesFullscreenWindow) {
// This test is flaky on MacOS 10.6.
#if defined(OS_MACOSX)
  if (base::mac::IsOSSnowLeopard())
    return;

  ui::test::ScopedFakeNSWindowFullscreen fake_fullscreen;
#endif

  ExtensionTestMessageListener launched_listener("Launched", true);
  LoadAndLaunchPlatformApp("leave_fullscreen", &launched_listener);

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow()->GetNativeWindow()));

  // When receiving the reply, the application will try to go fullscreen using
  // the Window API but there is no synchronous way to know if that actually
  // succeeded. Also, failure will not be notified. A failure case will only be
  // known with a timeout.
  {
    FullscreenChangeWaiter fs_changed(GetFirstAppWindow()->GetBaseWindow());

    launched_listener.Reply("window");

    fs_changed.Wait();
  }

  // Depending on the platform, going fullscreen might create an animation.
  // We want to make sure that the ESC key we will send next is actually going
  // to be received and the application might not receive key events during the
  // animation so we should wait for the key focus to be back.
  WaitUntilKeyFocus();

  // Same idea as above but for leaving fullscreen. Fullscreen mode should be
  // left when ESC is received.
  {
    FullscreenChangeWaiter fs_changed(GetFirstAppWindow()->GetBaseWindow());

    ASSERT_TRUE(SimulateKeyPress(ui::VKEY_ESCAPE));

    fs_changed.Wait();
  }
}

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, ESCLeavesFullscreenDOM) {
// This test is flaky on MacOS 10.6.
#if defined(OS_MACOSX)
  if (base::mac::IsOSSnowLeopard())
    return;

  ui::test::ScopedFakeNSWindowFullscreen fake_fullscreen;
#endif

  ExtensionTestMessageListener launched_listener("Launched", true);
  LoadAndLaunchPlatformApp("leave_fullscreen", &launched_listener);

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow()->GetNativeWindow()));

  launched_listener.Reply("dom");

  // Because the DOM way to go fullscreen requires user gesture, we simulate a
  // key event to get the window entering in fullscreen mode. The reply will
  // make the window listen for the key event. The reply will be sent to the
  // renderer process before the keypress and should be received in that order.
  // When receiving the key event, the application will try to go fullscreen
  // using the Window API but there is no synchronous way to know if that
  // actually succeeded. Also, failure will not be notified. A failure case will
  // only be known with a timeout.
  {
    FullscreenChangeWaiter fs_changed(GetFirstAppWindow()->GetBaseWindow());

    WaitUntilKeyFocus();
    ASSERT_TRUE(SimulateKeyPress(ui::VKEY_A));

    fs_changed.Wait();
  }

  // Depending on the platform, going fullscreen might create an animation.
  // We want to make sure that the ESC key we will send next is actually going
  // to be received and the application might not receive key events during the
  // animation so we should wait for the key focus to be back.
  WaitUntilKeyFocus();

  // Same idea as above but for leaving fullscreen. Fullscreen mode should be
  // left when ESC is received.
  {
    FullscreenChangeWaiter fs_changed(GetFirstAppWindow()->GetBaseWindow());

    ASSERT_TRUE(SimulateKeyPress(ui::VKEY_ESCAPE));

    fs_changed.Wait();
  }
}

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest,
                       ESCDoesNotLeaveFullscreenWindow) {
// This test is flaky on MacOS 10.6.
#if defined(OS_MACOSX)
  if (base::mac::IsOSSnowLeopard())
    return;

  ui::test::ScopedFakeNSWindowFullscreen fake_fullscreen;
#endif

  ExtensionTestMessageListener launched_listener("Launched", true);
  LoadAndLaunchPlatformApp("prevent_leave_fullscreen", &launched_listener);

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow()->GetNativeWindow()));

  // When receiving the reply, the application will try to go fullscreen using
  // the Window API but there is no synchronous way to know if that actually
  // succeeded. Also, failure will not be notified. A failure case will only be
  // known with a timeout.
  {
    FullscreenChangeWaiter fs_changed(GetFirstAppWindow()->GetBaseWindow());

    launched_listener.Reply("window");

    fs_changed.Wait();
  }

  // Depending on the platform, going fullscreen might create an animation.
  // We want to make sure that the ESC key we will send next is actually going
  // to be received and the application might not receive key events during the
  // animation so we should wait for the key focus to be back.
  WaitUntilKeyFocus();

  ASSERT_TRUE(SimulateKeyPress(ui::VKEY_ESCAPE));

  ExtensionTestMessageListener second_key_listener("B_KEY_RECEIVED", false);

  ASSERT_TRUE(SimulateKeyPress(ui::VKEY_B));

  ASSERT_TRUE(second_key_listener.WaitUntilSatisfied());

  // We assume that at that point, if we had to leave fullscreen, we should be.
  // However, by nature, we can not guarantee that and given that we do test
  // that nothing happens, we might end up with random-success when the feature
  // is broken.
  EXPECT_TRUE(GetFirstAppWindow()->GetBaseWindow()->IsFullscreen());
}

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest,
                       ESCDoesNotLeaveFullscreenDOM) {
// This test is flaky on MacOS 10.6.
#if defined(OS_MACOSX)
  if (base::mac::IsOSSnowLeopard())
    return;

  ui::test::ScopedFakeNSWindowFullscreen fake_fullscreen;
#endif

  ExtensionTestMessageListener launched_listener("Launched", true);
  LoadAndLaunchPlatformApp("prevent_leave_fullscreen", &launched_listener);

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow()->GetNativeWindow()));

  launched_listener.Reply("dom");

  // Because the DOM way to go fullscreen requires user gesture, we simulate a
  // key event to get the window entering in fullscreen mode. The reply will
  // make the window listen for the key event. The reply will be sent to the
  // renderer process before the keypress and should be received in that order.
  // When receiving the key event, the application will try to go fullscreen
  // using the Window API but there is no synchronous way to know if that
  // actually succeeded. Also, failure will not be notified. A failure case will
  // only be known with a timeout.
  {
    FullscreenChangeWaiter fs_changed(GetFirstAppWindow()->GetBaseWindow());

    WaitUntilKeyFocus();
    ASSERT_TRUE(SimulateKeyPress(ui::VKEY_A));

    fs_changed.Wait();
  }

  // Depending on the platform, going fullscreen might create an animation.
  // We want to make sure that the ESC key we will send next is actually going
  // to be received and the application might not receive key events during the
  // animation so we should wait for the key focus to be back.
  WaitUntilKeyFocus();

  ASSERT_TRUE(SimulateKeyPress(ui::VKEY_ESCAPE));

  ExtensionTestMessageListener second_key_listener("B_KEY_RECEIVED", false);

  ASSERT_TRUE(SimulateKeyPress(ui::VKEY_B));

  ASSERT_TRUE(second_key_listener.WaitUntilSatisfied());

  // We assume that at that point, if we had to leave fullscreen, we should be.
  // However, by nature, we can not guarantee that and given that we do test
  // that nothing happens, we might end up with random-success when the feature
  // is broken.
  EXPECT_TRUE(GetFirstAppWindow()->GetBaseWindow()->IsFullscreen());
}

// This test is duplicated from ESCDoesNotLeaveFullscreenWindow.
// It runs the same test, but uses the old permission names: 'fullscreen'
// and 'overrideEscFullscreen'.
IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest,
                       ESCDoesNotLeaveFullscreenOldPermission) {
// This test is flaky on MacOS 10.6.
#if defined(OS_MACOSX)
  if (base::mac::IsOSSnowLeopard())
    return;

  ui::test::ScopedFakeNSWindowFullscreen fake_fullscreen;
#endif

  ExtensionTestMessageListener launched_listener("Launched", true);
  LoadAndLaunchPlatformApp("prevent_leave_fullscreen_old", &launched_listener);

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow()->GetNativeWindow()));

  // When receiving the reply, the application will try to go fullscreen using
  // the Window API but there is no synchronous way to know if that actually
  // succeeded. Also, failure will not be notified. A failure case will only be
  // known with a timeout.
  {
    FullscreenChangeWaiter fs_changed(GetFirstAppWindow()->GetBaseWindow());

    launched_listener.Reply("window");

    fs_changed.Wait();
  }

  // Depending on the platform, going fullscreen might create an animation.
  // We want to make sure that the ESC key we will send next is actually going
  // to be received and the application might not receive key events during the
  // animation so we should wait for the key focus to be back.
  WaitUntilKeyFocus();

  ASSERT_TRUE(SimulateKeyPress(ui::VKEY_ESCAPE));

  ExtensionTestMessageListener second_key_listener("B_KEY_RECEIVED", false);

  ASSERT_TRUE(SimulateKeyPress(ui::VKEY_B));

  ASSERT_TRUE(second_key_listener.WaitUntilSatisfied());

  // We assume that at that point, if we had to leave fullscreen, we should be.
  // However, by nature, we can not guarantee that and given that we do test
  // that nothing happens, we might end up with random-success when the feature
  // is broken.
  EXPECT_TRUE(GetFirstAppWindow()->GetBaseWindow()->IsFullscreen());
}

#if defined(OS_MACOSX) || defined(OS_WIN)
// http://crbug.com/404081
#define MAYBE_TestInnerBounds DISABLED_TestInnerBounds
#else
#define MAYBE_TestInnerBounds TestInnerBounds
#endif
IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, MAYBE_TestInnerBounds) {
  ASSERT_TRUE(RunAppWindowInteractiveTest("testInnerBounds")) << message_;
}

void AppWindowInteractiveTest::TestOuterBoundsHelper(
    const std::string& frame_type) {
  ExtensionTestMessageListener launched_listener("Launched", true);
  const extensions::Extension* app =
      LoadAndLaunchPlatformApp("outer_bounds", &launched_listener);

  launched_listener.Reply(frame_type);
  launched_listener.Reset();
  ASSERT_TRUE(launched_listener.WaitUntilSatisfied());

  AppWindow* window = GetFirstAppWindowForApp(app->id());
  gfx::Rect window_bounds;
  gfx::Size min_size, max_size;

#if defined(OS_WIN)
  // Get the bounds from the HWND.
  HWND hwnd = views::HWNDForNativeWindow(window->GetNativeWindow());
  RECT rect;
  ::GetWindowRect(hwnd, &rect);
  window_bounds = gfx::Rect(
      rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);

  // HWNDMessageHandler calls this when responding to WM_GETMINMAXSIZE, so it's
  // the closest to what the window will see.
  views::HWNDMessageHandlerDelegate* host =
      static_cast<views::HWNDMessageHandlerDelegate*>(
          static_cast<views::DesktopWindowTreeHostWin*>(
              aura::WindowTreeHost::GetForAcceleratedWidget(hwnd)));
  host->GetMinMaxSize(&min_size, &max_size);
  // Note that this does not include the the client area insets so we need to
  // add them.
  gfx::Insets insets;
  host->GetClientAreaInsets(&insets);
  min_size = gfx::Size(min_size.width() + insets.left() + insets.right(),
                       min_size.height() + insets.top() + insets.bottom());
  max_size = gfx::Size(
      max_size.width() ? max_size.width() + insets.left() + insets.right() : 0,
      max_size.height() ? max_size.height() + insets.top() + insets.bottom()
                        : 0);
#endif  // defined(OS_WIN)

  // These match the values in the outer_bounds/test.js
  EXPECT_EQ(gfx::Rect(10, 11, 300, 301), window_bounds);
  EXPECT_EQ(window->GetBaseWindow()->GetBounds(), window_bounds);
  EXPECT_EQ(200, min_size.width());
  EXPECT_EQ(201, min_size.height());
  EXPECT_EQ(400, max_size.width());
  EXPECT_EQ(401, max_size.height());
}

// TODO(jackhou): Make this test work for other OSes.
#if !defined(OS_WIN)
#define MAYBE_TestOuterBoundsFrameChrome DISABLED_TestOuterBoundsFrameChrome
#define MAYBE_TestOuterBoundsFrameNone DISABLED_TestOuterBoundsFrameNone
#define MAYBE_TestOuterBoundsFrameColor DISABLED_TestOuterBoundsFrameColor
#else
#define MAYBE_TestOuterBoundsFrameChrome TestOuterBoundsFrameChrome
#define MAYBE_TestOuterBoundsFrameNone TestOuterBoundsFrameNone
#define MAYBE_TestOuterBoundsFrameColor TestOuterBoundsFrameColor
#endif

// Test that the outer bounds match that of the native window.
IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest,
                       MAYBE_TestOuterBoundsFrameChrome) {
  TestOuterBoundsHelper("chrome");
}
IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest,
                       MAYBE_TestOuterBoundsFrameNone) {
  TestOuterBoundsHelper("none");
}
IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest,
                       MAYBE_TestOuterBoundsFrameColor) {
  TestOuterBoundsHelper("color");
}

// This test does not work on Linux Aura because ShowInactive() is not
// implemented. See http://crbug.com/325142
// It also does not work on Windows because of the document being focused even
// though the window is not activated. See http://crbug.com/326986
// It also does not work on MacOS because ::ShowInactive() ends up behaving like
// ::Show() because of Cocoa conventions. See http://crbug.com/326987
// Those tests should be disabled on Linux GTK when they are enabled on the
// other platforms, see http://crbug.com/328829
#if (defined(OS_LINUX) && defined(USE_AURA)) || \
    defined(OS_WIN) || defined(OS_MACOSX)
#define MAYBE_TestCreate DISABLED_TestCreate
#define MAYBE_TestShow DISABLED_TestShow
#else
#define MAYBE_TestCreate TestCreate
#define MAYBE_TestShow TestShow
#endif

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, MAYBE_TestCreate) {
  ASSERT_TRUE(RunAppWindowInteractiveTest("testCreate")) << message_;
}

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, MAYBE_TestShow) {
  ASSERT_TRUE(RunAppWindowInteractiveTest("testShow")) << message_;
}

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, TestDrawAttention) {
  ASSERT_TRUE(RunAppWindowInteractiveTest("testDrawAttention")) << message_;
}

IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, TestCreateHidden) {
  // Created hidden both times.
  {
    ExtensionTestMessageListener launched_listener("Launched", true);
    LoadAndLaunchPlatformApp("hidden_with_id", &launched_listener);
    EXPECT_TRUE(launched_listener.WaitUntilSatisfied());
    ExtensionTestMessageListener create_listener_1("Launched", true);
    launched_listener.Reply("createHidden");
    EXPECT_TRUE(create_listener_1.WaitUntilSatisfied());
    AppWindow* app_window = GetFirstAppWindow();
    EXPECT_TRUE(app_window->is_hidden());
    ExtensionTestMessageListener create_listener_2("Launched", false);
    create_listener_1.Reply("createHidden");
    EXPECT_TRUE(create_listener_2.WaitUntilSatisfied());
    EXPECT_TRUE(app_window->is_hidden());
    app_window->GetBaseWindow()->Close();
  }

  // Created hidden, then visible. The second create should show the window.
  {
    ExtensionTestMessageListener launched_listener("Launched", true);
    LoadAndLaunchPlatformApp("hidden_with_id", &launched_listener);
    EXPECT_TRUE(launched_listener.WaitUntilSatisfied());
    ExtensionTestMessageListener create_listener_1("Launched", true);
    launched_listener.Reply("createHidden");
    EXPECT_TRUE(create_listener_1.WaitUntilSatisfied());
    AppWindow* app_window = GetFirstAppWindow();
    EXPECT_TRUE(app_window->is_hidden());
    ExtensionTestMessageListener create_listener_2("Launched", false);
    create_listener_1.Reply("createVisible");
    EXPECT_TRUE(create_listener_2.WaitUntilSatisfied());
    EXPECT_FALSE(app_window->is_hidden());
    app_window->GetBaseWindow()->Close();
  }
}

#if defined(OS_MACOSX)
// http://crbug.com/502516
#define MAYBE_TestFullscreen DISABLED_TestFullscreen
#else
#define MAYBE_TestFullscreen TestFullscreen
#endif
IN_PROC_BROWSER_TEST_F(AppWindowInteractiveTest, MAYBE_TestFullscreen) {
  ASSERT_TRUE(RunAppWindowInteractiveTest("testFullscreen")) << message_;
}

// Only Linux and Windows use keep-alive to determine when to shut down.
#if defined(OS_LINUX) || defined(OS_WIN)

// In general, hidden windows should not keep Chrome alive. The exception is
// when windows are created hidden, we allow the app some time to show the
// the window.
class AppWindowHiddenKeepAliveTest : public extensions::PlatformAppBrowserTest {
 protected:
  AppWindowHiddenKeepAliveTest() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AppWindowHiddenKeepAliveTest);
};

// A window that becomes hidden should not keep Chrome alive.
IN_PROC_BROWSER_TEST_F(AppWindowHiddenKeepAliveTest, ShownThenHidden) {
  LoadAndLaunchPlatformApp("minimal", "Launched");
  for (auto* browser : *BrowserList::GetInstance())
    browser->window()->Close();
  EXPECT_TRUE(KeepAliveRegistry::GetInstance()->IsOriginRegistered(
      KeepAliveOrigin::CHROME_APP_DELEGATE));
  GetFirstAppWindow()->Hide();
  EXPECT_FALSE(KeepAliveRegistry::GetInstance()->IsOriginRegistered(
      KeepAliveOrigin::CHROME_APP_DELEGATE));
}

// A window that is hidden but re-shown should still keep Chrome alive.
IN_PROC_BROWSER_TEST_F(AppWindowHiddenKeepAliveTest, ShownThenHiddenThenShown) {
  LoadAndLaunchPlatformApp("minimal", "Launched");
  AppWindow* app_window = GetFirstAppWindow();
  app_window->Hide();
  app_window->Show(AppWindow::SHOW_ACTIVE);

  EXPECT_TRUE(KeepAliveRegistry::GetInstance()->IsOriginRegistered(
      KeepAliveOrigin::CHROME_APP_DELEGATE));
  for (auto* browser : *BrowserList::GetInstance())
    browser->window()->Close();
  EXPECT_TRUE(KeepAliveRegistry::GetInstance()->IsOriginRegistered(
      KeepAliveOrigin::CHROME_APP_DELEGATE));
  app_window->GetBaseWindow()->Close();
}

// A window that is created hidden and stays hidden should not keep Chrome
// alive.
IN_PROC_BROWSER_TEST_F(AppWindowHiddenKeepAliveTest, StaysHidden) {
  LoadAndLaunchPlatformApp("hidden", "Launched");
  AppWindow* app_window = GetFirstAppWindow();
  EXPECT_TRUE(app_window->is_hidden());

  for (auto* browser : *BrowserList::GetInstance())
    browser->window()->Close();
  // This will time out if the command above does not terminate Chrome.
  content::RunMessageLoop();
}

// A window that is created hidden but shown soon after should keep Chrome
// alive.
IN_PROC_BROWSER_TEST_F(AppWindowHiddenKeepAliveTest, HiddenThenShown) {
  ExtensionTestMessageListener launched_listener("Launched", true);
  LoadAndLaunchPlatformApp("hidden_then_shown", &launched_listener);
  AppWindow* app_window = GetFirstAppWindow();
  EXPECT_TRUE(app_window->is_hidden());

  // Close all browser windows.
  for (auto* browser : *BrowserList::GetInstance())
    browser->window()->Close();

  // The app window will show after 3 seconds.
  ExtensionTestMessageListener shown_listener("Shown", false);
  launched_listener.Reply("");
  EXPECT_TRUE(shown_listener.WaitUntilSatisfied());
  EXPECT_FALSE(app_window->is_hidden());
  EXPECT_TRUE(KeepAliveRegistry::GetInstance()->IsOriginRegistered(
      KeepAliveOrigin::CHROME_APP_DELEGATE));
  app_window->GetBaseWindow()->Close();
}

#endif
