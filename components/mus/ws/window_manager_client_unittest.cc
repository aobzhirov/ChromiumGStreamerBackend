// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "components/mus/common/util.h"
#include "components/mus/public/cpp/tests/window_server_test_base.h"
#include "components/mus/public/cpp/window_observer.h"
#include "components/mus/public/cpp/window_tree_connection.h"
#include "components/mus/public/cpp/window_tree_connection_observer.h"
#include "components/mus/public/cpp/window_tree_delegate.h"
#include "mojo/converters/geometry/geometry_type_converters.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/mojo/geometry/geometry_util.h"

namespace mus {
namespace ws {

namespace {

int ValidIndexOf(const Window::Children& windows, Window* window) {
  Window::Children::const_iterator it =
      std::find(windows.begin(), windows.end(), window);
  return (it != windows.end()) ? (it - windows.begin()) : -1;
}

class TestWindowManagerDelegate : public WindowManagerDelegate {
 public:
  TestWindowManagerDelegate() {}
  ~TestWindowManagerDelegate() override {}

  // WindowManagerDelegate:
  void SetWindowManagerClient(WindowManagerClient* client) override {}
  bool OnWmSetBounds(Window* window, gfx::Rect* bounds) override {
    return false;
  }
  bool OnWmSetProperty(Window* window,
                       const std::string& name,
                       scoped_ptr<std::vector<uint8_t>>* new_data) override {
    return true;
  }
  Window* OnWmCreateTopLevelWindow(
      std::map<std::string, std::vector<uint8_t>>* properties) override {
    return nullptr;
  }
  void OnAccelerator(uint32_t id, const ui::Event& event) override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(TestWindowManagerDelegate);
};

class BoundsChangeObserver : public WindowObserver {
 public:
  explicit BoundsChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~BoundsChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowBoundsChanged(Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override {
    DCHECK_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  DISALLOW_COPY_AND_ASSIGN(BoundsChangeObserver);
};

// Wait until the bounds of the supplied window change; returns false on
// timeout.
bool WaitForBoundsToChange(Window* window) {
  BoundsChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

class ClientAreaChangeObserver : public WindowObserver {
 public:
  explicit ClientAreaChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~ClientAreaChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowClientAreaChanged(
      Window* window,
      const gfx::Insets& old_client_area,
      const std::vector<gfx::Rect>& old_additional_client_areas) override {
    DCHECK_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  DISALLOW_COPY_AND_ASSIGN(ClientAreaChangeObserver);
};

// Wait until the bounds of the supplied window change; returns false on
// timeout.
bool WaitForClientAreaToChange(Window* window) {
  ClientAreaChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

// Spins a run loop until the tree beginning at |root| has |tree_size| windows
// (including |root|).
class TreeSizeMatchesObserver : public WindowObserver {
 public:
  TreeSizeMatchesObserver(Window* tree, size_t tree_size)
      : tree_(tree), tree_size_(tree_size) {
    tree_->AddObserver(this);
  }
  ~TreeSizeMatchesObserver() override { tree_->RemoveObserver(this); }

  bool IsTreeCorrectSize() { return CountWindows(tree_) == tree_size_; }

 private:
  // Overridden from WindowObserver:
  void OnTreeChanged(const TreeChangeParams& params) override {
    if (IsTreeCorrectSize())
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  size_t CountWindows(const Window* window) const {
    size_t count = 1;
    Window::Children::const_iterator it = window->children().begin();
    for (; it != window->children().end(); ++it)
      count += CountWindows(*it);
    return count;
  }

  Window* tree_;
  size_t tree_size_;

  DISALLOW_COPY_AND_ASSIGN(TreeSizeMatchesObserver);
};

// Wait until |window| has |tree_size| descendants; returns false on timeout.
// The count includes |window|. For example, if you want to wait for |window| to
// have a single child, use a |tree_size| of 2.
bool WaitForTreeSizeToMatch(Window* window, size_t tree_size) {
  TreeSizeMatchesObserver observer(window, tree_size);
  return observer.IsTreeCorrectSize() ||
         WindowServerTestBase::DoRunLoopWithTimeout();
}

class OrderChangeObserver : public WindowObserver {
 public:
  OrderChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~OrderChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowReordered(Window* window,
                         Window* relative_window,
                         mojom::OrderDirection direction) override {
    DCHECK_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  DISALLOW_COPY_AND_ASSIGN(OrderChangeObserver);
};

// Wait until |window|'s tree size matches |tree_size|; returns false on
// timeout.
bool WaitForOrderChange(WindowTreeConnection* connection, Window* window) {
  OrderChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

// Tracks a window's destruction. Query is_valid() for current state.
class WindowTracker : public WindowObserver {
 public:
  explicit WindowTracker(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~WindowTracker() override {
    if (window_)
      window_->RemoveObserver(this);
  }

  bool is_valid() const { return !!window_; }

 private:
  // Overridden from WindowObserver:
  void OnWindowDestroyed(Window* window) override {
    DCHECK_EQ(window, window_);
    window_ = nullptr;
  }

  Window* window_;

  DISALLOW_COPY_AND_ASSIGN(WindowTracker);
};

}  // namespace

// WindowServer
// -----------------------------------------------------------------

struct EmbedResult {
  EmbedResult(WindowTreeConnection* connection, ConnectionSpecificId id)
      : connection(connection), connection_id(id) {}
  EmbedResult() : connection(nullptr), connection_id(0) {}

  WindowTreeConnection* connection;

  // The id supplied to the callback from OnEmbed(). Depending upon the
  // access policy this may or may not match the connection id of
  // |connection|.
  ConnectionSpecificId connection_id;
};

Window* GetFirstRoot(WindowTreeConnection* connection) {
  return connection->GetRoots().empty() ? nullptr
                                        : *connection->GetRoots().begin();
}

// These tests model synchronization of two peer connections to the window
// manager
// service, that are given access to some root window.

class WindowServerTest : public WindowServerTestBase {
 public:
  WindowServerTest() {}

  Window* GetFirstWMRoot() { return GetFirstRoot(window_manager()); }

  Window* NewVisibleWindow(Window* parent, WindowTreeConnection* connection) {
    Window* window = connection->NewWindow();
    window->SetVisible(true);
    parent->AddChild(window);
    return window;
  }

  // Embeds another version of the test app @ window. This runs a run loop until
  // a response is received, or a timeout. On success the new WindowServer is
  // returned.
  EmbedResult Embed(Window* window) {
    DCHECK(!embed_details_);
    embed_details_.reset(new EmbedDetails);
    window->Embed(ConnectAndGetWindowServerClient(),
                  base::Bind(&WindowServerTest::EmbedCallbackImpl,
                             base::Unretained(this)));
    embed_details_->waiting = true;
    if (!WindowServerTestBase::DoRunLoopWithTimeout())
      return EmbedResult();
    const EmbedResult result(embed_details_->connection,
                             embed_details_->connection_id);
    embed_details_.reset();
    return result;
  }

  // Establishes a connection to this application and asks for a
  // WindowTreeClient.
  mus::mojom::WindowTreeClientPtr ConnectAndGetWindowServerClient() {
    mus::mojom::WindowTreeClientPtr client;
    connector()->ConnectToInterface(test_name(), &client);
    return client;
  }

  // WindowServerTestBase:
  void OnEmbed(Window* root) override {
    if (!embed_details_) {
      WindowServerTestBase::OnEmbed(root);
      return;
    }

    embed_details_->connection = root->connection();
    if (embed_details_->callback_run)
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

 private:
  // Used to track the state of a call to window->Embed().
  struct EmbedDetails {
    EmbedDetails()
        : callback_run(false),
          result(false),
          waiting(false),
          connection(nullptr) {}

    // The callback supplied to Embed() was received.
    bool callback_run;

    // The boolean supplied to the Embed() callback.
    bool result;

    // Whether a MessageLoop is running.
    bool waiting;

    // Connection id supplied to the Embed() callback.
    ConnectionSpecificId connection_id;

    // The WindowTreeConnection that resulted from the Embed(). null if |result|
    // is false.
    WindowTreeConnection* connection;
  };

  void EmbedCallbackImpl(bool result) {
    embed_details_->callback_run = true;
    embed_details_->result = result;
    if (embed_details_->waiting && (!result || embed_details_->connection))
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  scoped_ptr<EmbedDetails> embed_details_;

  DISALLOW_COPY_AND_ASSIGN(WindowServerTest);
};

TEST_F(WindowServerTest, RootWindow) {
  ASSERT_NE(nullptr, window_manager());
  EXPECT_EQ(1u, window_manager()->GetRoots().size());
}

TEST_F(WindowServerTest, Embed) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);
  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window_in_embedded = GetFirstRoot(embedded);
  ASSERT_NE(nullptr, window_in_embedded);
  EXPECT_EQ(window->id(), window_in_embedded->id());
  EXPECT_EQ(nullptr, window_in_embedded->parent());
  EXPECT_TRUE(window_in_embedded->children().empty());
}

// Window manager has two windows, N1 and N11. Embeds A at N1. A should not see
// N11.
TEST_F(WindowServerTest, EmbeddedDoesntSeeChild) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);
  Window* nested = window_manager()->NewWindow();
  ASSERT_NE(nullptr, nested);
  nested->SetVisible(true);
  window->AddChild(nested);

  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);
  Window* window_in_embedded = GetFirstRoot(embedded);
  EXPECT_EQ(window->id(), window_in_embedded->id());
  EXPECT_EQ(nullptr, window_in_embedded->parent());
  EXPECT_TRUE(window_in_embedded->children().empty());
}

// TODO(beng): write a replacement test for the one that once existed here:
// This test validates the following scenario:
// -  a window originating from one connection
// -  a window originating from a second connection
// +  the connection originating the window is destroyed
// -> the window should still exist (since the second connection is live) but
//    should be disconnected from any windows.
// http://crbug.com/396300
//
// TODO(beng): The new test should validate the scenario as described above
//             except that the second connection still has a valid tree.

// Verifies that bounds changes applied to a window hierarchy in one connection
// are reflected to another.
TEST_F(WindowServerTest, SetBounds) {
  Window* window = window_manager()->NewWindow();
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);
  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window_in_embedded = embedded->GetWindowById(window->id());
  EXPECT_EQ(window->bounds(), window_in_embedded->bounds());

  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  ASSERT_TRUE(WaitForBoundsToChange(window_in_embedded));
  EXPECT_TRUE(window->bounds() == window_in_embedded->bounds());
}

// Verifies that bounds changes applied to a window owned by a different
// connection can be refused.
TEST_F(WindowServerTest, SetBoundsSecurity) {
  TestWindowManagerDelegate wm_delegate;
  set_window_manager_delegate(&wm_delegate);

  Window* window = window_manager()->NewWindow();
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);
  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window_in_embedded = embedded->GetWindowById(window->id());
  window->SetBounds(gfx::Rect(0, 0, 800, 600));
  ASSERT_TRUE(WaitForBoundsToChange(window_in_embedded));

  window_in_embedded->SetBounds(gfx::Rect(0, 0, 1024, 768));
  // Bounds change is initially accepted, but the server declines the request.
  EXPECT_FALSE(window->bounds() == window_in_embedded->bounds());

  // The client is notified when the requested is declined, and updates the
  // local bounds accordingly.
  ASSERT_TRUE(WaitForBoundsToChange(window_in_embedded));
  EXPECT_TRUE(window->bounds() == window_in_embedded->bounds());
  set_window_manager_delegate(nullptr);
}

// Verifies that a root window can always be destroyed.
TEST_F(WindowServerTest, DestroySecurity) {
  Window* window = window_manager()->NewWindow();
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);

  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  // The root can be destroyed, even though it was not created by the
  // connection.
  Window* embed_root = embedded->GetWindowById(window->id());
  WindowTracker tracker1(window);
  WindowTracker tracker2(embed_root);
  embed_root->Destroy();
  EXPECT_FALSE(tracker2.is_valid());
  EXPECT_TRUE(tracker1.is_valid());

  window->Destroy();
  EXPECT_FALSE(tracker1.is_valid());
}

TEST_F(WindowServerTest, MultiRoots) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  GetFirstWMRoot()->AddChild(window1);
  Window* window2 = window_manager()->NewWindow();
  window2->SetVisible(true);
  GetFirstWMRoot()->AddChild(window2);
  WindowTreeConnection* embedded1 = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded1);
  WindowTreeConnection* embedded2 = Embed(window2).connection;
  ASSERT_NE(nullptr, embedded2);
  EXPECT_NE(embedded1, embedded2);
}

TEST_F(WindowServerTest, Reorder) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  GetFirstWMRoot()->AddChild(window1);

  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window11 = embedded->NewWindow();
  window11->SetVisible(true);
  GetFirstRoot(embedded)->AddChild(window11);
  Window* window12 = embedded->NewWindow();
  window12->SetVisible(true);
  GetFirstRoot(embedded)->AddChild(window12);
  ASSERT_TRUE(WaitForTreeSizeToMatch(window1, 3u));

  Window* root_in_embedded = GetFirstRoot(embedded);

  {
    window11->MoveToFront();
    // The |embedded| tree should be updated immediately.
    EXPECT_EQ(root_in_embedded->children().front(),
              embedded->GetWindowById(window12->id()));
    EXPECT_EQ(root_in_embedded->children().back(),
              embedded->GetWindowById(window11->id()));

    // The |window_manager()| tree is still not updated.
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window12->id()));

    // Wait until |window_manager()| tree is updated.
    ASSERT_TRUE(WaitForOrderChange(
        window_manager(), window_manager()->GetWindowById(window11->id())));
    EXPECT_EQ(window1->children().front(),
              window_manager()->GetWindowById(window12->id()));
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window11->id()));
  }

  {
    window11->MoveToBack();
    // |embedded| should be updated immediately.
    EXPECT_EQ(root_in_embedded->children().front(),
              embedded->GetWindowById(window11->id()));
    EXPECT_EQ(root_in_embedded->children().back(),
              embedded->GetWindowById(window12->id()));

    // |window_manager()| is also eventually updated.
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window11->id()));
    ASSERT_TRUE(WaitForOrderChange(
        window_manager(), window_manager()->GetWindowById(window11->id())));
    EXPECT_EQ(window1->children().front(),
              window_manager()->GetWindowById(window11->id()));
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window12->id()));
  }
}

namespace {

class VisibilityChangeObserver : public WindowObserver {
 public:
  explicit VisibilityChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~VisibilityChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowVisibilityChanged(Window* window) override {
    EXPECT_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  DISALLOW_COPY_AND_ASSIGN(VisibilityChangeObserver);
};

}  // namespace

TEST_F(WindowServerTest, Visible) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  GetFirstWMRoot()->AddChild(window1);

  // Embed another app and verify initial state.
  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);
  ASSERT_NE(nullptr, GetFirstRoot(embedded));
  Window* embedded_root = GetFirstRoot(embedded);
  EXPECT_TRUE(embedded_root->visible());
  EXPECT_TRUE(embedded_root->IsDrawn());

  // Change the visible state from the first connection and verify its mirrored
  // correctly to the embedded app.
  {
    VisibilityChangeObserver observer(embedded_root);
    window1->SetVisible(false);
    ASSERT_TRUE(WindowServerTestBase::DoRunLoopWithTimeout());
  }

  EXPECT_FALSE(window1->visible());
  EXPECT_FALSE(window1->IsDrawn());

  EXPECT_FALSE(embedded_root->visible());
  EXPECT_FALSE(embedded_root->IsDrawn());

  // Make the node visible again.
  {
    VisibilityChangeObserver observer(embedded_root);
    window1->SetVisible(true);
    ASSERT_TRUE(WindowServerTestBase::DoRunLoopWithTimeout());
  }

  EXPECT_TRUE(window1->visible());
  EXPECT_TRUE(window1->IsDrawn());

  EXPECT_TRUE(embedded_root->visible());
  EXPECT_TRUE(embedded_root->IsDrawn());
}

namespace {

class DrawnChangeObserver : public WindowObserver {
 public:
  explicit DrawnChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~DrawnChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowDrawnChanged(Window* window) override {
    EXPECT_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  DISALLOW_COPY_AND_ASSIGN(DrawnChangeObserver);
};

}  // namespace

TEST_F(WindowServerTest, Drawn) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  GetFirstWMRoot()->AddChild(window1);

  // Embed another app and verify initial state.
  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);
  ASSERT_NE(nullptr, GetFirstRoot(embedded));
  Window* embedded_root = GetFirstRoot(embedded);
  EXPECT_TRUE(embedded_root->visible());
  EXPECT_TRUE(embedded_root->IsDrawn());

  // Change the visibility of the root, this should propagate a drawn state
  // change to |embedded|.
  {
    DrawnChangeObserver observer(embedded_root);
    GetFirstWMRoot()->SetVisible(false);
    ASSERT_TRUE(DoRunLoopWithTimeout());
  }

  EXPECT_TRUE(window1->visible());
  EXPECT_FALSE(window1->IsDrawn());

  EXPECT_TRUE(embedded_root->visible());
  EXPECT_FALSE(embedded_root->IsDrawn());
}

// TODO(beng): tests for window event dispatcher.
// - verify that we see events for all windows.

namespace {

class FocusChangeObserver : public WindowObserver {
 public:
  explicit FocusChangeObserver(Window* window)
      : window_(window),
        last_gained_focus_(nullptr),
        last_lost_focus_(nullptr),
        quit_on_change_(true) {
    window_->AddObserver(this);
  }
  ~FocusChangeObserver() override { window_->RemoveObserver(this); }

  void set_quit_on_change(bool value) { quit_on_change_ = value; }

  Window* last_gained_focus() { return last_gained_focus_; }

  Window* last_lost_focus() { return last_lost_focus_; }

 private:
  // Overridden from WindowObserver.
  void OnWindowFocusChanged(Window* gained_focus, Window* lost_focus) override {
    EXPECT_TRUE(!gained_focus || gained_focus->HasFocus());
    EXPECT_FALSE(lost_focus && lost_focus->HasFocus());
    last_gained_focus_ = gained_focus;
    last_lost_focus_ = lost_focus;
    if (quit_on_change_)
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;
  Window* last_gained_focus_;
  Window* last_lost_focus_;
  bool quit_on_change_;

  DISALLOW_COPY_AND_ASSIGN(FocusChangeObserver);
};

class NullFocusChangeObserver : public WindowTreeConnectionObserver {
 public:
  explicit NullFocusChangeObserver(WindowTreeConnection* connection)
      : connection_(connection) {
    connection_->AddObserver(this);
  }
  ~NullFocusChangeObserver() override { connection_->RemoveObserver(this); }

 private:
  // Overridden from WindowTreeConnectionObserver.
  void OnWindowTreeFocusChanged(Window* gained_focus,
                                Window* lost_focus) override {
    if (!gained_focus)
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  WindowTreeConnection* connection_;

  DISALLOW_COPY_AND_ASSIGN(NullFocusChangeObserver);
};

bool WaitForWindowToHaveFocus(Window* window) {
  if (window->HasFocus())
    return true;
  FocusChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

bool WaitForNoWindowToHaveFocus(WindowTreeConnection* connection) {
  if (!connection->GetFocusedWindow())
    return true;
  NullFocusChangeObserver observer(connection);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

}  // namespace

TEST_F(WindowServerTest, Focus) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  GetFirstWMRoot()->AddChild(window1);

  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);
  Window* window11 = embedded->NewWindow();
  window11->SetVisible(true);
  GetFirstRoot(embedded)->AddChild(window11);

  {
    // Focus the embed root in |embedded|.
    Window* embedded_root = GetFirstRoot(embedded);
    FocusChangeObserver observer(embedded_root);
    observer.set_quit_on_change(false);
    embedded_root->SetFocus();
    ASSERT_TRUE(embedded_root->HasFocus());
    ASSERT_NE(nullptr, observer.last_gained_focus());
    EXPECT_EQ(embedded_root->id(), observer.last_gained_focus()->id());

    // |embedded_root| is the same as |window1|, make sure |window1| got
    // focus too.
    ASSERT_TRUE(WaitForWindowToHaveFocus(window1));
  }

  // Focus a child of GetFirstRoot(embedded).
  {
    FocusChangeObserver observer(window11);
    observer.set_quit_on_change(false);
    window11->SetFocus();
    ASSERT_TRUE(window11->HasFocus());
    ASSERT_NE(nullptr, observer.last_gained_focus());
    ASSERT_NE(nullptr, observer.last_lost_focus());
    EXPECT_EQ(window11->id(), observer.last_gained_focus()->id());
    EXPECT_EQ(GetFirstRoot(embedded)->id(), observer.last_lost_focus()->id());
  }

  {
    // Add an observer on the Window that loses focus, and make sure the
    // observer sees the right values.
    FocusChangeObserver observer(window11);
    observer.set_quit_on_change(false);
    GetFirstRoot(embedded)->SetFocus();
    ASSERT_NE(nullptr, observer.last_gained_focus());
    ASSERT_NE(nullptr, observer.last_lost_focus());
    EXPECT_EQ(window11->id(), observer.last_lost_focus()->id());
    EXPECT_EQ(GetFirstRoot(embedded)->id(), observer.last_gained_focus()->id());
  }
}

TEST_F(WindowServerTest, FocusNonFocusableWindow) {
  Window* window = window_manager()->NewWindow();
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);

  WindowTreeConnection* connection = Embed(window).connection;
  ASSERT_NE(nullptr, connection);
  ASSERT_FALSE(connection->GetRoots().empty());
  Window* client_window = *connection->GetRoots().begin();
  client_window->SetCanFocus(false);

  client_window->SetFocus();
  ASSERT_TRUE(client_window->HasFocus());

  WaitForNoWindowToHaveFocus(connection);
  ASSERT_FALSE(client_window->HasFocus());
}

TEST_F(WindowServerTest, Activation) {
  Window* parent = NewVisibleWindow(GetFirstWMRoot(), window_manager());

  // Allow the child windows to be activated. Do this before we wait, that way
  // we're guaranteed that when we request focus from a separate client the
  // requests are processed in order.
  window_manager_client()->AddActivationParent(parent);

  Window* child1 = NewVisibleWindow(parent, window_manager());
  Window* child2 = NewVisibleWindow(parent, window_manager());
  Window* child3 = NewVisibleWindow(parent, window_manager());

  child1->AddTransientWindow(child3);

  WindowTreeConnection* embedded1 = Embed(child1).connection;
  ASSERT_NE(nullptr, embedded1);
  WindowTreeConnection* embedded2 = Embed(child2).connection;
  ASSERT_NE(nullptr, embedded2);

  Window* child11 = NewVisibleWindow(GetFirstRoot(embedded1), embedded1);
  Window* child21 = NewVisibleWindow(GetFirstRoot(embedded2), embedded2);

  WaitForTreeSizeToMatch(parent, 6);

  // |child2| and |child3| are stacked about |child1|.
  EXPECT_GT(ValidIndexOf(parent->children(), child2),
            ValidIndexOf(parent->children(), child1));
  EXPECT_GT(ValidIndexOf(parent->children(), child3),
            ValidIndexOf(parent->children(), child1));

  // Set focus on |child11|. This should activate |child1|, and raise it over
  // |child2|. But |child3| should still be above |child1| because of
  // transiency.
  child11->SetFocus();
  ASSERT_TRUE(WaitForWindowToHaveFocus(child11));
  ASSERT_TRUE(
      WaitForWindowToHaveFocus(window_manager()->GetWindowById(child11->id())));
  EXPECT_EQ(child11->id(), window_manager()->GetFocusedWindow()->id());
  EXPECT_EQ(child11->id(), embedded1->GetFocusedWindow()->id());
  EXPECT_EQ(nullptr, embedded2->GetFocusedWindow());
  EXPECT_GT(ValidIndexOf(parent->children(), child1),
            ValidIndexOf(parent->children(), child2));
  EXPECT_GT(ValidIndexOf(parent->children(), child3),
            ValidIndexOf(parent->children(), child1));

  // Set focus on |child21|. This should activate |child2|, and raise it over
  // |child1|.
  child21->SetFocus();
  ASSERT_TRUE(WaitForWindowToHaveFocus(child21));
  ASSERT_TRUE(
      WaitForWindowToHaveFocus(window_manager()->GetWindowById(child21->id())));
  EXPECT_EQ(child21->id(), window_manager()->GetFocusedWindow()->id());
  EXPECT_EQ(child21->id(), embedded2->GetFocusedWindow()->id());
  EXPECT_TRUE(WaitForNoWindowToHaveFocus(embedded1));
  EXPECT_EQ(nullptr, embedded1->GetFocusedWindow());
  EXPECT_GT(ValidIndexOf(parent->children(), child2),
            ValidIndexOf(parent->children(), child1));
  EXPECT_GT(ValidIndexOf(parent->children(), child3),
            ValidIndexOf(parent->children(), child1));
}

TEST_F(WindowServerTest, ActivationNext) {
  Window* parent = GetFirstWMRoot();
  Window* child1 = NewVisibleWindow(parent, window_manager());
  Window* child2 = NewVisibleWindow(parent, window_manager());
  Window* child3 = NewVisibleWindow(parent, window_manager());

  WindowTreeConnection* embedded1 = Embed(child1).connection;
  ASSERT_NE(nullptr, embedded1);
  WindowTreeConnection* embedded2 = Embed(child2).connection;
  ASSERT_NE(nullptr, embedded2);
  WindowTreeConnection* embedded3 = Embed(child3).connection;
  ASSERT_NE(nullptr, embedded3);

  Window* child11 = NewVisibleWindow(GetFirstRoot(embedded1), embedded1);
  Window* child21 = NewVisibleWindow(GetFirstRoot(embedded2), embedded2);
  Window* child31 = NewVisibleWindow(GetFirstRoot(embedded3), embedded3);
  WaitForTreeSizeToMatch(parent, 7);

  Window* expecteds[] = { child3, child2, child1, child3, nullptr };
  Window* focused[] = { child31, child21, child11, child31, nullptr };
  for (size_t index = 0; expecteds[index]; ++index) {
    window_manager_client()->ActivateNextWindow();
    WaitForWindowToHaveFocus(focused[index]);
    EXPECT_TRUE(focused[index]->HasFocus());
    EXPECT_EQ(parent->children().back(), expecteds[index]);
  }
}

namespace {

class DestroyedChangedObserver : public WindowObserver {
 public:
  DestroyedChangedObserver(WindowServerTestBase* test,
                           Window* window,
                           bool* got_destroy)
      : test_(test), window_(window), got_destroy_(got_destroy) {
    window_->AddObserver(this);
  }
  ~DestroyedChangedObserver() override {
    if (window_)
      window_->RemoveObserver(this);
  }

 private:
  // Overridden from WindowObserver:
  void OnWindowDestroyed(Window* window) override {
    EXPECT_EQ(window, window_);
    window_->RemoveObserver(this);
    *got_destroy_ = true;
    window_ = nullptr;

    // We should always get OnWindowDestroyed() before OnConnectionLost().
    EXPECT_FALSE(test_->window_tree_connection_destroyed());
  }

  WindowServerTestBase* test_;
  Window* window_;
  bool* got_destroy_;

  DISALLOW_COPY_AND_ASSIGN(DestroyedChangedObserver);
};

}  // namespace

// Verifies deleting a WindowServer sends the right notifications.
TEST_F(WindowServerTest, DeleteWindowServer) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);
  WindowTreeConnection* connection = Embed(window).connection;
  ASSERT_TRUE(connection);
  bool got_destroy = false;
  DestroyedChangedObserver observer(this, GetFirstRoot(connection),
                                    &got_destroy);
  delete connection;
  EXPECT_TRUE(window_tree_connection_destroyed());
  EXPECT_TRUE(got_destroy);
}

// Verifies two Embed()s in the same window trigger deletion of the first
// WindowServer.
TEST_F(WindowServerTest, DisconnectTriggersDelete) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  GetFirstWMRoot()->AddChild(window);
  WindowTreeConnection* connection = Embed(window).connection;
  EXPECT_NE(connection, window_manager());
  Window* embedded_window = connection->NewWindow();
  // Embed again, this should trigger disconnect and deletion of connection.
  bool got_destroy;
  DestroyedChangedObserver observer(this, embedded_window, &got_destroy);
  EXPECT_FALSE(window_tree_connection_destroyed());
  Embed(window);
  EXPECT_TRUE(window_tree_connection_destroyed());
}

class WindowRemovedFromParentObserver : public WindowObserver {
 public:
  explicit WindowRemovedFromParentObserver(Window* window)
      : window_(window), was_removed_(false) {
    window_->AddObserver(this);
  }
  ~WindowRemovedFromParentObserver() override { window_->RemoveObserver(this); }

  bool was_removed() const { return was_removed_; }

 private:
  // Overridden from WindowObserver:
  void OnTreeChanged(const TreeChangeParams& params) override {
    if (params.target == window_ && !params.new_parent)
      was_removed_ = true;
  }

  Window* window_;
  bool was_removed_;

  DISALLOW_COPY_AND_ASSIGN(WindowRemovedFromParentObserver);
};

TEST_F(WindowServerTest, EmbedRemovesChildren) {
  Window* window1 = window_manager()->NewWindow();
  Window* window2 = window_manager()->NewWindow();
  GetFirstWMRoot()->AddChild(window1);
  window1->AddChild(window2);

  WindowRemovedFromParentObserver observer(window2);
  window1->Embed(ConnectAndGetWindowServerClient());
  EXPECT_TRUE(observer.was_removed());
  EXPECT_EQ(nullptr, window2->parent());
  EXPECT_TRUE(window1->children().empty());

  // Run the message loop so the Embed() call above completes. Without this
  // we may end up reconnecting to the test and rerunning the test, which is
  // problematic since the other services don't shut down.
  ASSERT_TRUE(DoRunLoopWithTimeout());
}

namespace {

class DestroyObserver : public WindowObserver {
 public:
  DestroyObserver(WindowServerTestBase* test,
                  WindowTreeConnection* connection,
                  bool* got_destroy)
      : test_(test), got_destroy_(got_destroy) {
    GetFirstRoot(connection)->AddObserver(this);
  }
  ~DestroyObserver() override {}

 private:
  // Overridden from WindowObserver:
  void OnWindowDestroyed(Window* window) override {
    *got_destroy_ = true;
    window->RemoveObserver(this);

    // We should always get OnWindowDestroyed() before
    // OnWindowManagerDestroyed().
    EXPECT_FALSE(test_->window_tree_connection_destroyed());

    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  WindowServerTestBase* test_;
  bool* got_destroy_;

  DISALLOW_COPY_AND_ASSIGN(DestroyObserver);
};

}  // namespace

// Verifies deleting a Window that is the root of another connection notifies
// observers in the right order (OnWindowDestroyed() before
// OnWindowManagerDestroyed()).
TEST_F(WindowServerTest, WindowServerDestroyedAfterRootObserver) {
  Window* embed_window = window_manager()->NewWindow();
  GetFirstWMRoot()->AddChild(embed_window);

  WindowTreeConnection* embedded_connection = Embed(embed_window).connection;

  bool got_destroy = false;
  DestroyObserver observer(this, embedded_connection, &got_destroy);
  // Delete the window |embedded_connection| is embedded in. This is async,
  // but will eventually trigger deleting |embedded_connection|.
  embed_window->Destroy();
  EXPECT_TRUE(DoRunLoopWithTimeout());
  EXPECT_TRUE(got_destroy);
}

TEST_F(WindowServerTest, ClientAreaChanged) {
  Window* embed_window = window_manager()->NewWindow();
  GetFirstWMRoot()->AddChild(embed_window);

  WindowTreeConnection* embedded_connection = Embed(embed_window).connection;

  // Verify change from embedded makes it to parent.
  GetFirstRoot(embedded_connection)->SetClientArea(gfx::Insets(1, 2, 3, 4));
  ASSERT_TRUE(WaitForClientAreaToChange(embed_window));
  EXPECT_TRUE(gfx::Insets(1, 2, 3, 4) == embed_window->client_area());

  // Changing bounds shouldn't effect client area.
  embed_window->SetBounds(gfx::Rect(21, 22, 23, 24));
  WaitForBoundsToChange(GetFirstRoot(embedded_connection));
  EXPECT_TRUE(gfx::Rect(21, 22, 23, 24) ==
              GetFirstRoot(embedded_connection)->bounds());
  EXPECT_TRUE(gfx::Insets(1, 2, 3, 4) ==
              GetFirstRoot(embedded_connection)->client_area());
}

class EstablishConnectionViaFactoryDelegate : public TestWindowManagerDelegate {
 public:
  explicit EstablishConnectionViaFactoryDelegate(
      WindowTreeConnection* connection)
      : connection_(connection), run_loop_(nullptr), created_window_(nullptr) {}
  ~EstablishConnectionViaFactoryDelegate() override {}

  bool QuitOnCreate() {
    if (run_loop_)
      return false;

    created_window_ = nullptr;
    run_loop_.reset(new base::RunLoop);
    run_loop_->Run();
    run_loop_.reset();
    return created_window_ != nullptr;
  }

  Window* created_window() { return created_window_; }

  // WindowManagerDelegate:
  Window* OnWmCreateTopLevelWindow(
      std::map<std::string, std::vector<uint8_t>>* properties) override {
    created_window_ = connection_->NewWindow(properties);
    (*connection_->GetRoots().begin())->AddChild(created_window_);
    if (run_loop_)
      run_loop_->Quit();
    return created_window_;
  }

 private:
  WindowTreeConnection* connection_;
  scoped_ptr<base::RunLoop> run_loop_;
  Window* created_window_;

  DISALLOW_COPY_AND_ASSIGN(EstablishConnectionViaFactoryDelegate);
};

TEST_F(WindowServerTest, EstablishConnectionViaFactory) {
  EstablishConnectionViaFactoryDelegate delegate(window_manager());
  set_window_manager_delegate(&delegate);
  scoped_ptr<WindowTreeConnection> second_connection(
      WindowTreeConnection::Create(this, connector()));
  Window* window_in_second_connection =
      second_connection->NewTopLevelWindow(nullptr);
  ASSERT_TRUE(window_in_second_connection);
  ASSERT_TRUE(second_connection->GetRoots().count(window_in_second_connection) >
              0);
  // Wait for the window to appear in the wm.
  ASSERT_TRUE(delegate.QuitOnCreate());

  Window* window_in_wm = delegate.created_window();
  ASSERT_TRUE(window_in_wm);

  // Change the bounds in the wm, and make sure the child sees it.
  window_in_wm->SetBounds(gfx::Rect(1, 11, 12, 101));
  ASSERT_TRUE(WaitForBoundsToChange(window_in_second_connection));
  EXPECT_EQ(gfx::Rect(1, 11, 12, 101), window_in_second_connection->bounds());
}

}  // namespace ws
}  // namespace mus
