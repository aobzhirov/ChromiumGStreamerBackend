// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_BOOKMARKS_BOOKMARK_BAR_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_BOOKMARKS_BOOKMARK_BAR_CONTROLLER_H_

#import <Cocoa/Cocoa.h>
#include <stdint.h>
#include <map>

#import "base/mac/cocoa_protocols.h"
#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/cocoa/bookmarks/bookmark_bar_bridge.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_bar_constants.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_bar_state.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_bar_toolbar_view.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_button.h"
#import "chrome/browser/ui/cocoa/has_weak_browser_pointer.h"
#include "chrome/browser/ui/cocoa/tabs/tab_strip_model_observer_bridge.h"
#include "ui/base/window_open_disposition.h"

@class BookmarkBarController;
@class BookmarkBarFolderController;
@class BookmarkBarView;
@class BookmarkButtonCell;
@class BookmarkContextMenuCocoaController;
@class BookmarkFolderTarget;
class BookmarkModelObserverForCocoa;
class Browser;
class GURL;
namespace ui {
class ThemeProvider;
}

namespace bookmarks {

class BookmarkModel;
class BookmarkNode;
class ManagedBookmarkService;

// Magic numbers from Cole
// TODO(jrg): create an objc-friendly version of bookmark_bar_constants.h?

// Used as a maximum width for buttons on the bar.
const CGFloat kDefaultBookmarkWidth = 150.0;

// Horizontal frame inset for buttons in the bookmark bar.
const CGFloat kBookmarkHorizontalPadding = 1.0;

// Vertical frame inset for buttons in the bookmark bar.
const CGFloat kBookmarkVerticalPadding = 2.0;

// Left margin before the first button in the bookmark bar.
const CGFloat kBookmarkLeftMargin = 2.0;

// Right margin before the last button in the bookmark bar.
const CGFloat kBookmarkRightMargin = 2.0;

// Used as a min/max width for buttons on menus (not on the bar).
const CGFloat kBookmarkMenuButtonMinimumWidth = 100.0;
const CGFloat kBookmarkMenuButtonMaximumWidth = 485.0;

// The minimum separation between a folder menu and the edge of the screen.
// If the menu gets closer to the edge of the screen (either right or left)
// then it is pops up in the opposite direction.
// (See -[BookmarkBarFolderController childFolderWindowLeftForWidth:]).
const CGFloat kBookmarkHorizontalScreenPadding = 8.0;

// Our NSScrollView is supposed to be just barely big enough to fit its
// contentView.  It is actually a hair too small.
// This turns on horizontal scrolling which, although slight, is awkward.
// Make sure our window (and NSScrollView) are wider than its documentView
// by at least this much.
const CGFloat kScrollViewContentWidthMargin = 2;

// Make subfolder menus overlap their parent menu a bit to give a better
// perception of a menuing system.
const CGFloat kBookmarkMenuOverlap = 2.0;

// When constraining a scrolling bookmark bar folder window to the
// screen, shrink the "constrain" by this much vertically.  Currently
// this is 0.0 to avoid a problem with tracking areas leaving the
// window, but should probably be 8.0 or something.
const CGFloat kScrollWindowVerticalMargin = 6.0;

// How far to offset a folder menu from the top of the bookmark bar. This
// is set just above the bar so that it become distinctive when drawn.
const CGFloat kBookmarkBarMenuOffset = 2.0;

// How far to offset a folder menu's left edge horizontally in relation to
// the left edge of the button from which it springs. Because of drawing
// differences, simply aligning the |frame| of each does not render the
// pproper result, so we have to offset.
const CGFloat kBookmarkBarButtonOffset = 2.0;

// Delay before opening a subfolder (and closing the previous one)
// when hovering over a folder button.
const NSTimeInterval kHoverOpenDelay = 0.3;

// Delay on hover before a submenu opens when dragging.
// Experimentally a drag hover open delay needs to be bigger than a
// normal (non-drag) menu hover open such as used in the bookmark folder.
//  TODO(jrg): confirm feel of this constant with ui-team.
//  http://crbug.com/36276
const NSTimeInterval kDragHoverOpenDelay = 0.7;

// Notes on use of kDragHoverCloseDelay in
// -[BookmarkBarFolderController draggingEntered:].
//
// We have an implicit delay on stop-hover-open before a submenu
// closes.  This cannot be zero since it's nice to move the mouse in a
// direct line from "current position" to "position of item in
// submenu".  However, by doing so, it's possible to overlap a
// different button on the current menu.  Example:
//
//  Folder1
//  Folder2  ---> Sub1
//  Folder3       Sub2
//                Sub3
//
// If you hover over the F in Folder2 to open the sub, and then want to
// select Sub3, a direct line movement of the mouse may cross over
// Folder3.  Without this delay, that'll cause Sub to be closed before
// you get there, since a "hover over" of Folder3 gets activated.
// It's subtle but without the delay it feels broken.
//
// This is only really a problem with vertical menu --> vertical menu
// movement; the bookmark bar (horizontal menu, sort of) seems fine,
// perhaps because mouse move direction is purely vertical so there is
// no opportunity for overlap.
const NSTimeInterval kDragHoverCloseDelay = 0.4;

}  // namespace bookmarks

// The interface for the bookmark bar controller's delegate. Currently, the
// delegate is the BWC and is responsible for ensuring that the toolbar is
// displayed correctly (as specified by |-getDesiredToolbarHeightCompression|
// and |-toolbarDividerOpacity|) at the beginning and at the end of an animation
// (or after a state change).
@protocol BookmarkBarControllerDelegate

// Sent when the state has changed (after any animation), but before the final
// display update.
- (void)bookmarkBar:(BookmarkBarController*)controller
 didChangeFromState:(BookmarkBar::State)oldState
            toState:(BookmarkBar::State)newState;

// Sent before the animation begins.
- (void)bookmarkBar:(BookmarkBarController*)controller
willAnimateFromState:(BookmarkBar::State)oldState
            toState:(BookmarkBar::State)newState;

@end

// A controller for the bookmark bar in the browser window. Handles showing
// and hiding based on the preference in the given profile.
@interface BookmarkBarController
    : NSViewController<BookmarkBarState,
                       BookmarkBarToolbarViewController,
                       BookmarkButtonDelegate,
                       BookmarkButtonControllerProtocol,
                       NSDraggingDestination,
                       HasWeakBrowserPointer> {
 @private
  // The state of the bookmark bar. If an animation is running, this is set to
  // the "destination" and |lastState_| is set to the "original" state.
  BookmarkBar::State currentState_;

  // The "original" state of the bookmark bar if an animation is running.
  BookmarkBar::State lastState_;

  // YES if an animation is running.
  BOOL isAnimationRunning_;

  Browser* browser_;              // weak; owned by its window
  bookmarks::BookmarkModel* bookmarkModel_;  // weak; part of the profile owned
                                             // by the top-level Browser object.
  bookmarks::ManagedBookmarkService* managedBookmarkService_;

  // Our initial view width, which is applied in awakeFromNib.
  CGFloat initialWidth_;

  // BookmarkNodes have a 64bit id.  NSMenuItems have a 32bit tag used
  // to represent the bookmark node they refer to.  This map provides
  // a mapping from one to the other, so we can properly identify the
  // node from the item.  When adding items in, we start with seedId_.
  int32_t seedId_;
  std::map<int32_t, int64_t> menuTagMap_;

  // Our bookmark buttons, ordered from L-->R.
  base::scoped_nsobject<NSMutableArray> buttons_;

  // The folder image so we can use one copy for all buttons
  base::scoped_nsobject<NSImage> folderImage_;

  // The default image, so we can use one copy for all buttons.
  base::scoped_nsobject<NSImage> defaultImage_;

  // If the bar is disabled, we hide it and ignore show/hide commands.
  // Set when using fullscreen mode.
  BOOL barIsEnabled_;

  // Bridge from Chrome-style C++ notifications (e.g. derived from
  // BookmarkModelObserver)
  scoped_ptr<BookmarkBarBridge> bridge_;

  // Delegate that is informed about state changes in the bookmark bar.
  id<BookmarkBarControllerDelegate> delegate_;  // weak

  // Logic for dealing with a click on a bookmark folder button.
  base::scoped_nsobject<BookmarkFolderTarget> folderTarget_;

  // A controller for a pop-up bookmark folder window (custom menu).
  // This is not a scoped_nsobject because it owns itself (when its
  // window closes the controller gets autoreleased).
  BookmarkBarFolderController* folderController_;

  // The event tap that allows monitoring of all events, to properly close with
  // a click outside the bounds of the window.
  id exitEventTap_;

  IBOutlet BookmarkBarView* buttonView_;  // Contains 'no items' text fields.
  IBOutlet BookmarkButton* offTheSideButton_;  // aka the chevron.

  NSRect originalNoItemsRect_;  // Original, pre-resized field rect.
  NSRect originalImportBookmarksRect_;  // Original, pre-resized field rect.

  // "Apps" button on the left side.
  base::scoped_nsobject<BookmarkButton> appsPageShortcutButton_;

  // "Managed bookmarks" button on the left side, next to the apps button.
  base::scoped_nsobject<BookmarkButton> managedBookmarksButton_;

  // "Supervised bookmarks" button on the left side, next to the apps button.
  base::scoped_nsobject<BookmarkButton> supervisedBookmarksButton_;

  // "Other bookmarks" button on the right side.
  base::scoped_nsobject<BookmarkButton> otherBookmarksButton_;

  // When doing a drag, this is folder button "hovered over" which we
  // may want to open after a short delay.  There are cases where a
  // mouse-enter can open a folder (e.g. if the menus are "active")
  // but that doesn't use this variable or need a delay so "hover" is
  // the wrong term.
  base::scoped_nsobject<BookmarkButton> hoverButton_;

  // We save the view width when we add bookmark buttons.  This lets
  // us avoid a rebuild until we've grown the window bigger than our
  // initial build.
  CGFloat savedFrameWidth_;

  // The number of buttons we display in the bookmark bar.  This does
  // not include the "off the side" chevron or the "Other Bookmarks"
  // button.  We use this number to determine if we need to display
  // the chevron, and to know what to place in the chevron's menu.
  // Since we create everything before doing layout we can't be sure
  // that all bookmark buttons we create will be visible.  Thus,
  // [buttons_ count] isn't a definitive check.
  int displayedButtonCount_;

  // A state flag which tracks when the bar's folder menus should be shown.
  // An initial click in any of the folder buttons turns this on and
  // one of the following will turn it off: another click in the button,
  // the window losing focus, a click somewhere other than in the bar
  // or a folder menu.
  BOOL showFolderMenus_;

  // If YES then state changes (for example, from hidden to shown) are animated.
  // This is turned off for unit tests.
  BOOL stateAnimationsEnabled_;

  // If YES then changes inside the bookmark bar (for example, removing a
  // bookmark) are animated. This is turned off for unit tests.
  BOOL innerContentAnimationsEnabled_;

  // YES if there is a possible drop about to happen in the bar.
  BOOL hasInsertionPos_;

  // The x point on the bar where the left edge of the new item will end
  // up if it is dropped.
  CGFloat insertionPos_;

  // Controller responsible for all bookmark context menus.
  base::scoped_nsobject<BookmarkContextMenuCocoaController>
      contextMenuController_;

  // Weak pointer to the pulsed button for the currently pulsing node. We need
  // to store this as it may not be possible to determine the pulsing button if
  // the pulsing node is deleted. Nil if there is no pulsing node.
  BookmarkButton* pulsingButton_;

  // Specifically watch the currently pulsing node. This lets us stop pulsing
  // when anything happens to the node. Null if there is no pulsing node.
  scoped_ptr<BookmarkModelObserverForCocoa> pulsingBookmarkObserver_;
}

@property(readonly, nonatomic) BookmarkBar::State currentState;
@property(readonly, nonatomic) BookmarkBar::State lastState;
@property(readonly, nonatomic) BOOL isAnimationRunning;
@property(assign, nonatomic) id<BookmarkBarControllerDelegate> delegate;
@property(assign, nonatomic) BOOL stateAnimationsEnabled;
@property(assign, nonatomic) BOOL innerContentAnimationsEnabled;

// Initializes the bookmark bar controller with the given browser and delegate.
// To properly manage vertical resizing of the bookmark bar, the caller must
// also call -setResizeDelegate on the -controlledView. This should be done once
// the initializer returns, since it will trigger nib loading.
- (id)initWithBrowser:(Browser*)browser
         initialWidth:(CGFloat)initialWidth
             delegate:(id<BookmarkBarControllerDelegate>)delegate;

// The Browser corresponding to this BookmarkBarController.
- (Browser*)browser;

// Strongly-typed version of [self view]. Note this may trigger nib loading.
- (BookmarkBarToolbarView*)controlledView;

// The controller for all bookmark bar context menus.
- (BookmarkContextMenuCocoaController*)menuController;

// Pulses the given bookmark node, or the closest parent node that is visible.
- (void)startPulsingBookmarkNode:(const bookmarks::BookmarkNode*)node;

// Stops pulsing any bookmark nodes.
- (void)stopPulsingBookmarkNode;

// Updates the bookmark bar (from its current, possibly in-transition) state to
// the new state.
- (void)updateState:(BookmarkBar::State)newState
         changeType:(BookmarkBar::AnimateChangeType)changeType;

// Update the visible state of the bookmark bar.
- (void)updateVisibility;

// Update the visible state of the extra buttons on the bookmark bar: the
// apps shortcut, the managed bookmarks folder, and the supervised bookmarks
// folder.
- (void)updateExtraButtonsVisibility;

// Hides or shows the bookmark bar depending on the current state.
- (void)updateHiddenState;

// Turn on or off the bookmark bar and prevent or reallow its appearance. On
// disable, toggle off if shown. On enable, show only if needed. App and popup
// windows do not show a bookmark bar.
- (void)setBookmarkBarEnabled:(BOOL)enabled;

// Returns the amount by which the toolbar above should be compressed.
- (CGFloat)getDesiredToolbarHeightCompression;

// Gets the appropriate opacity for the toolbar's divider; 0 means that it
// shouldn't be shown.
- (CGFloat)toolbarDividerOpacity;

// Updates the sizes and positions of the subviews.
// TODO(viettrungluu): I'm not convinced this should be public, but I currently
// need it for animations. Try not to propagate its use.
- (void)layoutSubviews;

// Called by our view when it is moved to a window.
- (void)viewDidMoveToWindow;

// Provide a favicon for a bookmark node.  May return nil.
- (NSImage*)faviconForNode:(const bookmarks::BookmarkNode*)node;

// Used for situations where the bookmark bar folder menus should no longer
// be actively popping up. Called when the window loses focus, a click has
// occured outside the menus or a bookmark has been activated. (Note that this
// differs from the behavior of the -[BookmarkButtonControllerProtocol
// closeAllBookmarkFolders] method in that the latter does not terminate menu
// tracking since it may be being called in response to actions (such as
// dragging) where a 'stale' menu presentation should first be collapsed before
// presenting a new menu.)
- (void)closeFolderAndStopTrackingMenus;

// Checks if operations such as edit or delete are allowed.
- (BOOL)canEditBookmark:(const bookmarks::BookmarkNode*)node;

// Checks if bookmark editing is enabled at all.
- (BOOL)canEditBookmarks;

// Actions for manipulating bookmarks.
// Open a normal bookmark or folder from a button, ...
- (IBAction)openBookmark:(id)sender;
- (IBAction)openBookmarkFolderFromButton:(id)sender;
// From the "off the side" button, ...
- (IBAction)openOffTheSideFolderFromButton:(id)sender;
// Import bookmarks from another browser.
- (IBAction)importBookmarks:(id)sender;
@end

// Redirects from BookmarkBarBridge, the C++ object which glues us to
// the rest of Chromium.  Internal to BookmarkBarController.
@interface BookmarkBarController(BridgeRedirect)
- (void)loaded:(bookmarks::BookmarkModel*)model;
- (void)beingDeleted:(bookmarks::BookmarkModel*)model;
- (void)nodeAdded:(bookmarks::BookmarkModel*)model
           parent:(const bookmarks::BookmarkNode*)oldParent index:(int)index;
- (void)nodeChanged:(bookmarks::BookmarkModel*)model
               node:(const bookmarks::BookmarkNode*)node;
- (void)nodeMoved:(bookmarks::BookmarkModel*)model
        oldParent:(const bookmarks::BookmarkNode*)oldParent
         oldIndex:(int)oldIndex
        newParent:(const bookmarks::BookmarkNode*)newParent
         newIndex:(int)newIndex;
- (void)nodeRemoved:(bookmarks::BookmarkModel*)model
             parent:(const bookmarks::BookmarkNode*)oldParent
              index:(int)index;
- (void)nodeFaviconLoaded:(bookmarks::BookmarkModel*)model
                     node:(const bookmarks::BookmarkNode*)node;
- (void)nodeChildrenReordered:(bookmarks::BookmarkModel*)model
                         node:(const bookmarks::BookmarkNode*)node;
@end

// These APIs should only be used by unit tests (or used internally).
@interface BookmarkBarController(InternalOrTestingAPI)
- (void)openBookmarkFolder:(id)sender;
- (void)openOrCloseBookmarkFolderForOffTheSideButton;
- (BookmarkBarView*)buttonView;
- (NSMutableArray*)buttons;
- (NSButton*)offTheSideButton;
- (NSButton*)appsPageShortcutButton;
- (BOOL)offTheSideButtonIsHidden;
- (BOOL)appsPageShortcutButtonIsHidden;
- (BookmarkButton*)otherBookmarksButton;
- (BookmarkBarFolderController*)folderController;
- (id)folderTarget;
- (int)displayedButtonCount;
- (void)openURL:(GURL)url disposition:(WindowOpenDisposition)disposition;
- (void)clearBookmarkBar;
- (BookmarkButtonCell*)cellForBookmarkNode:(const bookmarks::BookmarkNode*)node;
- (BookmarkButtonCell*)cellForCustomButtonWithText:(NSString*)text
                                             image:(NSImage*)image;
- (NSRect)frameForBookmarkButtonFromCell:(NSCell*)cell xOffset:(int*)xOffset;
- (void)checkForBookmarkButtonGrowth:(NSButton*)button;
- (void)frameDidChange;
- (int64_t)nodeIdFromMenuTag:(int32_t)tag;
- (int32_t)menuTagFromNodeId:(int64_t)menuid;
- (void)updateTheme:(const ui::ThemeProvider*)themeProvider;
- (BookmarkButton*)buttonForDroppingOnAtPoint:(NSPoint)point;
- (BOOL)isEventAnExitEvent:(NSEvent*)event;
- (BOOL)shrinkOrHideView:(NSView*)view forMaxX:(CGFloat)maxViewX;
- (void)unhighlightBookmark:(const bookmarks::BookmarkNode*)node;

// The following are for testing purposes only and are not used internally.
- (NSMenu *)menuForFolderNode:(const bookmarks::BookmarkNode*)node;
@end

#endif  // CHROME_BROWSER_UI_COCOA_BOOKMARKS_BOOKMARK_BAR_CONTROLLER_H_
