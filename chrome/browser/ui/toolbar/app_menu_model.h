// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TOOLBAR_APP_MENU_MODEL_H_
#define CHROME_BROWSER_UI_TOOLBAR_APP_MENU_MODEL_H_

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/models/button_menu_item_model.h"
#include "ui/base/models/simple_menu_model.h"

class BookmarkSubMenuModel;
class Browser;
class RecentTabsSubMenuModel;

namespace {
class MockAppMenuModel;
}  // namespace

// Values should correspond to 'WretchMenuAction' enum in histograms.xml.
enum AppMenuAction {
  MENU_ACTION_NEW_TAB = 0,
  MENU_ACTION_NEW_WINDOW = 1,
  MENU_ACTION_NEW_INCOGNITO_WINDOW = 2,
  MENU_ACTION_SHOW_BOOKMARK_BAR = 3,
  MENU_ACTION_SHOW_BOOKMARK_MANAGER = 4,
  MENU_ACTION_IMPORT_SETTINGS = 5,
  MENU_ACTION_BOOKMARK_PAGE = 6,
  MENU_ACTION_BOOKMARK_ALL_TABS = 7,
  MENU_ACTION_PIN_TO_START_SCREEN = 8,
  MENU_ACTION_RESTORE_TAB = 9,
  MENU_ACTION_DISTILL_PAGE = 13,
  MENU_ACTION_SAVE_PAGE = 14,
  MENU_ACTION_FIND = 15,
  MENU_ACTION_PRINT = 16,
  MENU_ACTION_CUT = 17,
  MENU_ACTION_COPY = 18,
  MENU_ACTION_PASTE = 19,
  MENU_ACTION_CREATE_HOSTED_APP = 20,
  MENU_ACTION_CREATE_SHORTCUTS = 21,
  MENU_ACTION_MANAGE_EXTENSIONS = 22,
  MENU_ACTION_TASK_MANAGER = 23,
  MENU_ACTION_CLEAR_BROWSING_DATA = 24,
  MENU_ACTION_VIEW_SOURCE = 25,
  MENU_ACTION_DEV_TOOLS = 26,
  MENU_ACTION_DEV_TOOLS_CONSOLE = 27,
  MENU_ACTION_DEV_TOOLS_DEVICES = 28,
  MENU_ACTION_PROFILING_ENABLED = 29,
  MENU_ACTION_ZOOM_MINUS = 30,
  MENU_ACTION_ZOOM_PLUS = 31,
  MENU_ACTION_FULLSCREEN = 32,
  MENU_ACTION_SHOW_HISTORY = 33,
  MENU_ACTION_SHOW_DOWNLOADS = 34,
  MENU_ACTION_SHOW_SYNC_SETUP = 35,
  MENU_ACTION_OPTIONS = 36,
  MENU_ACTION_ABOUT = 37,
  MENU_ACTION_HELP_PAGE_VIA_MENU = 38,
  MENU_ACTION_FEEDBACK = 39,
  MENU_ACTION_TOGGLE_REQUEST_TABLET_SITE = 40,
  MENU_ACTION_EXIT = 43,
  MENU_ACTION_RECENT_TAB = 41,
  MENU_ACTION_BOOKMARK_OPEN = 42,
  MENU_ACTION_UPGRADE_DIALOG = 44,
  MENU_ACTION_CAST = 45,
  LIMIT_MENU_ACTION
};

// A menu model that builds the contents of an encoding menu.
class EncodingMenuModel : public ui::SimpleMenuModel,
                          public ui::SimpleMenuModel::Delegate {
 public:
  explicit EncodingMenuModel(Browser* browser);
  ~EncodingMenuModel() override;

  // Overridden from ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void Build();

  Browser* browser_;  // weak

  DISALLOW_COPY_AND_ASSIGN(EncodingMenuModel);
};

// A menu model that builds the contents of the zoom menu.
class ZoomMenuModel : public ui::SimpleMenuModel {
 public:
  explicit ZoomMenuModel(ui::SimpleMenuModel::Delegate* delegate);
  ~ZoomMenuModel() override;

 private:
  void Build();

  DISALLOW_COPY_AND_ASSIGN(ZoomMenuModel);
};

class ToolsMenuModel : public ui::SimpleMenuModel {
 public:
  ToolsMenuModel(ui::SimpleMenuModel::Delegate* delegate, Browser* browser);
  ~ToolsMenuModel() override;

 private:
  void Build(Browser* browser);

  scoped_ptr<EncodingMenuModel> encoding_menu_model_;

  DISALLOW_COPY_AND_ASSIGN(ToolsMenuModel);
};

// A menu model that builds the contents of the app menu.
class AppMenuModel : public ui::SimpleMenuModel,
                     public ui::SimpleMenuModel::Delegate,
                     public ui::ButtonMenuItemModel::Delegate,
                     public TabStripModelObserver,
                     public content::NotificationObserver {
 public:
  // Range of command IDs to use for the items in the recent tabs submenu.
  static const int kMinRecentTabsCommandId = 1001;
  static const int kMaxRecentTabsCommandId = 1200;

  AppMenuModel(ui::AcceleratorProvider* provider, Browser* browser);
  ~AppMenuModel() override;

  // Overridden for ButtonMenuItemModel::Delegate:
  bool DoesCommandIdDismissMenu(int command_id) const override;

  // Overridden for both ButtonMenuItemModel::Delegate and SimpleMenuModel:
  bool IsItemForCommandIdDynamic(int command_id) const override;
  base::string16 GetLabelForCommandId(int command_id) const override;
  bool GetIconForCommandId(int command_id, gfx::Image* icon) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdVisible(int command_id) const override;
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) override;

  // Overridden from TabStripModelObserver:
  void ActiveTabChanged(content::WebContents* old_contents,
                        content::WebContents* new_contents,
                        int index,
                        int reason) override;
  void TabReplacedAt(TabStripModel* tab_strip_model,
                     content::WebContents* old_contents,
                     content::WebContents* new_contents,
                     int index) override;

  // Overridden from content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // Getters.
  Browser* browser() const { return browser_; }

  BookmarkSubMenuModel* bookmark_sub_menu_model() const {
    return bookmark_sub_menu_model_.get();
  }

  // Calculates |zoom_label_| in response to a zoom change.
  void UpdateZoomControls();

 private:
  class HelpMenuModel;
  // Testing constructor used for mocking.
  friend class ::MockAppMenuModel;

  AppMenuModel();

  void Build();

  // Adds actionable global error menu items to the menu.
  // Examples: Extension permissions and sign in errors.
  // Returns a boolean indicating whether any menu items were added.
  bool AddGlobalErrorMenuItems();

  // Appends everything needed for the clipboard menu: a menu break, the
  // clipboard menu content and the finalizing menu break.
  void CreateCutCopyPasteMenu();

  // Add a menu item for the browser action icons.
  void CreateActionToolbarOverflowMenu();

  // Appends everything needed for the zoom menu: a menu break, then the zoom
  // menu content and then another menu break.
  void CreateZoomMenu();

  void OnZoomLevelChanged(const content::HostZoomMap::ZoomLevelChange& change);

  bool ShouldShowNewIncognitoWindowMenuItem();

  // Called when a command is selected.
  // Logs UMA metrics about which command was chosen and how long the user
  // took to select the command.
  void LogMenuMetrics(int command_id);

  // Helper function to record the menu action in a UMA histogram.
  void LogMenuAction(int action_id);

  // Time menu has been open. Used by LogMenuMetrics() to record the time
  // to action when the user selects a menu item.
  base::ElapsedTimer timer_;

  // Whether a UMA menu action has been recorded since the menu is open.
  // Only the first time to action is recorded since some commands
  // (zoom controls) don't dimiss the menu.
  bool uma_action_recorded_;

  // Models for the special menu items with buttons.
  scoped_ptr<ui::ButtonMenuItemModel> edit_menu_item_model_;
  scoped_ptr<ui::ButtonMenuItemModel> zoom_menu_item_model_;

  // Label of the zoom label in the zoom menu item.
  base::string16 zoom_label_;

#if defined(GOOGLE_CHROME_BUILD)
  // Help menu.
  scoped_ptr<HelpMenuModel> help_menu_model_;
#endif

  // Tools menu.
  scoped_ptr<ToolsMenuModel> tools_menu_model_;

  // Bookmark submenu.
  scoped_ptr<BookmarkSubMenuModel> bookmark_sub_menu_model_;

  // Recent Tabs submenu.
  scoped_ptr<RecentTabsSubMenuModel> recent_tabs_sub_menu_model_;

  ui::AcceleratorProvider* provider_;  // weak

  Browser* browser_;  // weak

  scoped_ptr<content::HostZoomMap::Subscription> browser_zoom_subscription_;
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(AppMenuModel);
};

#endif  // CHROME_BROWSER_UI_TOOLBAR_APP_MENU_MODEL_H_
