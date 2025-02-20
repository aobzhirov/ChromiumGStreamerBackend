// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/applescript/window_applescript.h"

#include "base/logging.h"
#import "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#import "chrome/browser/app_controller_mac.h"
#import "chrome/browser/chrome_browser_application_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/cocoa/applescript/constants_applescript.h"
#include "chrome/browser/ui/cocoa/applescript/error_applescript.h"
#include "chrome/browser/ui/cocoa/applescript/metrics_applescript.h"
#import "chrome/browser/ui/cocoa/applescript/tab_applescript.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_context.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"

@interface WindowAppleScript(WindowAppleScriptPrivateMethods)
// The NSWindow that corresponds to this window.
- (NSWindow*)nativeHandle;
@end

@implementation WindowAppleScript

- (id)init {
  // Check which mode to open a new window.
  NSScriptCommand* command = [NSScriptCommand currentCommand];
  NSString* mode = [[[command evaluatedArguments]
      objectForKey:@"KeyDictionary"] objectForKey:@"mode"];
  AppController* appDelegate = [NSApp delegate];

  Profile* lastProfile = [appDelegate lastProfile];

  if (!lastProfile) {
    AppleScript::SetError(AppleScript::errGetProfile);
    return nil;
  }

  Profile* profile;
  if ([mode isEqualToString:AppleScript::kIncognitoWindowMode]) {
    profile = lastProfile->GetOffTheRecordProfile();
  }
  else if ([mode isEqualToString:AppleScript::kNormalWindowMode] || !mode) {
    profile = lastProfile;
  } else {
    // Mode cannot be anything else
    AppleScript::SetError(AppleScript::errInvalidMode);
    return nil;
  }
  // Set the mode to nil, to ensure that it is not set once more.
  [[[command evaluatedArguments] objectForKey:@"KeyDictionary"]
      setValue:nil forKey:@"mode"];
  return [self initWithProfile:profile];
}

- (id)initWithProfile:(Profile*)aProfile {
  if (!aProfile) {
    [self release];
    return nil;
  }

  if ((self = [super init])) {
    browser_ = new Browser(Browser::CreateParams(aProfile));
    chrome::NewTab(browser_);
    browser_->window()->Show();
    base::scoped_nsobject<NSNumber> numID(
        [[NSNumber alloc] initWithInt:browser_->session_id().id()]);
    [self setUniqueID:numID];
  }
  return self;
}

- (id)initWithBrowser:(Browser*)aBrowser {
  if (!aBrowser) {
    [self release];
    return nil;
  }

  if ((self = [super init])) {
    // It is safe to be weak, if a window goes away (eg user closing a window)
    // the applescript runtime calls appleScriptWindows in
    // BrowserCrApplication and this particular window is never returned.
    browser_ = aBrowser;
    base::scoped_nsobject<NSNumber> numID(
        [[NSNumber alloc] initWithInt:browser_->session_id().id()]);
    [self setUniqueID:numID];
  }
  return self;
}

- (NSWindow*)nativeHandle {
  // window() can be NULL during startup.
  if (browser_->window())
    return browser_->window()->GetNativeWindow();
  return nil;
}

- (NSNumber*)activeTabIndex {
  // Note: applescript is 1-based, that is lists begin with index 1.
  int activeTabIndex = browser_->tab_strip_model()->active_index() + 1;
  if (!activeTabIndex) {
    return nil;
  }
  return [NSNumber numberWithInt:activeTabIndex];
}

- (void)setActiveTabIndex:(NSNumber*)anActiveTabIndex {
  // Note: applescript is 1-based, that is lists begin with index 1.
  int atIndex = [anActiveTabIndex intValue] - 1;
  if (atIndex >= 0 && atIndex < browser_->tab_strip_model()->count())
    browser_->tab_strip_model()->ActivateTabAt(atIndex, true);
  else
    AppleScript::SetError(AppleScript::errInvalidTabIndex);
}

- (NSString*)mode {
  Profile* profile = browser_->profile();
  if (profile->IsOffTheRecord())
    return AppleScript::kIncognitoWindowMode;
  return AppleScript::kNormalWindowMode;
}

- (void)setMode:(NSString*)theMode {
  // cannot set mode after window is created.
  if (theMode) {
    AppleScript::SetError(AppleScript::errSetMode);
  }
}

- (TabAppleScript*)activeTab {
  TabAppleScript* currentTab =
      [[[TabAppleScript alloc] initWithWebContents:
          browser_->tab_strip_model()->GetActiveWebContents()] autorelease];
  [currentTab setContainer:self
                  property:AppleScript::kTabsProperty];
  return currentTab;
}

- (NSArray*)tabs {
  TabStripModel* tabStrip = browser_->tab_strip_model();
  NSMutableArray* tabs = [NSMutableArray arrayWithCapacity:tabStrip->count()];

  for (int i = 0; i < tabStrip->count(); ++i) {
    // Check to see if tab is closing.
    content::WebContents* webContents = tabStrip->GetWebContentsAt(i);
    if (webContents->IsBeingDestroyed()) {
      continue;
    }

    base::scoped_nsobject<TabAppleScript> tab(
        [[TabAppleScript alloc] initWithWebContents:webContents]);
    [tab setContainer:self
             property:AppleScript::kTabsProperty];
    [tabs addObject:tab];
  }
  return tabs;
}

- (void)insertInTabs:(TabAppleScript*)aTab {
  // This method gets called when a new tab is created so
  // the container and property are set here.
  [aTab setContainer:self
            property:AppleScript::kTabsProperty];

  // Set how long it takes a tab to be created.
  base::TimeTicks newTabStartTime = base::TimeTicks::Now();
  content::WebContents* contents = chrome::AddSelectedTabWithURL(
      browser_,
      GURL(chrome::kChromeUINewTabURL),
      ui::PAGE_TRANSITION_TYPED);
  CoreTabHelper* core_tab_helper = CoreTabHelper::FromWebContents(contents);
  core_tab_helper->set_new_tab_start_time(newTabStartTime);
  [aTab setWebContents:contents];
}

- (void)insertInTabs:(TabAppleScript*)aTab atIndex:(int)index {
  // This method gets called when a new tab is created so
  // the container and property are set here.
  [aTab setContainer:self
            property:AppleScript::kTabsProperty];

  // Set how long it takes a tab to be created.
  base::TimeTicks newTabStartTime = base::TimeTicks::Now();
  chrome::NavigateParams params(browser_, GURL(chrome::kChromeUINewTabURL),
                                ui::PAGE_TRANSITION_TYPED);
  params.disposition = NEW_FOREGROUND_TAB;
  params.tabstrip_index = index;
  chrome::Navigate(&params);
  CoreTabHelper* core_tab_helper =
      CoreTabHelper::FromWebContents(params.target_contents);
  core_tab_helper->set_new_tab_start_time(newTabStartTime);

  [aTab setWebContents:params.target_contents];
}

- (void)removeFromTabsAtIndex:(int)index {
  if (index < 0 || index >= browser_->tab_strip_model()->count())
    return;
  browser_->tab_strip_model()->CloseWebContentsAt(
      index, TabStripModel::CLOSE_CREATE_HISTORICAL_TAB);
}

- (NSNumber*)orderedIndex {
  return [NSNumber numberWithInt:[[self nativeHandle] orderedIndex]];
}

- (void)setOrderedIndex:(NSNumber*)anIndex {
  int index = [anIndex intValue] - 1;
  if (index < 0 || index >= static_cast<int>(chrome::GetTotalBrowserCount())) {
    AppleScript::SetError(AppleScript::errWrongIndex);
    return;
  }
  [[self nativeHandle] setOrderedIndex:index];
}

- (NSComparisonResult)windowComparator:(WindowAppleScript*)otherWindow {
  int thisIndex = [[self orderedIndex] intValue];
  int otherIndex = [[otherWindow orderedIndex] intValue];
  if (thisIndex < otherIndex)
    return NSOrderedAscending;
  else if (thisIndex > otherIndex)
    return NSOrderedDescending;
  // Indexes can never be same.
  NOTREACHED();
  return NSOrderedSame;
}

// Get and set values from the associated NSWindow.
- (id)valueForUndefinedKey:(NSString*)key {
  return [[self nativeHandle] valueForKey:key];
}

- (void)setValue:(id)value forUndefinedKey:(NSString*)key {
  [[self nativeHandle] setValue:(id)value forKey:key];
}

- (void)handlesCloseScriptCommand:(NSCloseCommand*)command {
  AppleScript::LogAppleScriptUMA(AppleScript::AppleScriptCommand::WINDOW_CLOSE);

  // window() can be NULL during startup.
  if (browser_->window())
    browser_->window()->Close();
}

- (NSNumber*)presenting {
  BOOL presentingValue = browser_->window() &&
                         browser_->window()->IsFullscreen() &&
                         !browser_->window()
                              ->GetExclusiveAccessContext()
                              ->IsFullscreenWithToolbar();
  return [NSNumber numberWithBool:presentingValue];
}

- (void)handlesEnterPresentationMode:(NSScriptCommand*)command {
  AppleScript::LogAppleScriptUMA(
      AppleScript::AppleScriptCommand::WINDOW_ENTER_PRESENTATION_MODE);
  browser_->exclusive_access_manager()->context()->EnterFullscreen(
      GURL(), EXCLUSIVE_ACCESS_BUBBLE_TYPE_FULLSCREEN_EXIT_INSTRUCTION, false);
}

- (void)handlesExitPresentationMode:(NSScriptCommand*)command {
  AppleScript::LogAppleScriptUMA(
      AppleScript::AppleScriptCommand::WINDOW_EXIT_PRESENTATION_MODE);
  browser_->exclusive_access_manager()->context()->ExitFullscreen();
}

@end
