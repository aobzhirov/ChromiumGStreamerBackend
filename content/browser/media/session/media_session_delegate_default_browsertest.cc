// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "content/browser/media/session/media_session.h"
#include "content/browser/media/session/mock_media_session_observer.h"
#include "content/public/test/content_browser_test.h"
#include "content/shell/browser/shell.h"
#include "media/base/media_switches.h"

namespace content {

class MediaSessionDelegateDefaultBrowserTest : public ContentBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kEnableDefaultMediaSession);
  }
};

IN_PROC_BROWSER_TEST_F(MediaSessionDelegateDefaultBrowserTest,
                       ActiveWebContentsPauseOthers) {
  scoped_ptr<MockMediaSessionObserver> media_session_observer(
      new MockMediaSessionObserver);

  MediaSession* media_session = MediaSession::Get(shell()->web_contents());
  ASSERT_TRUE(media_session);

  WebContents* other_web_contents = CreateBrowser()->web_contents();
  MediaSession* other_media_session = MediaSession::Get(other_web_contents);
  ASSERT_TRUE(other_media_session);

  media_session_observer->StartNewPlayer();
  media_session->AddPlayer(
      media_session_observer.get(), 0, MediaSession::Type::Content);
  EXPECT_TRUE(media_session->IsActive());
  EXPECT_FALSE(other_media_session->IsActive());

  media_session_observer->StartNewPlayer();
  other_media_session->AddPlayer(
      media_session_observer.get(), 1, MediaSession::Type::Content);
  EXPECT_FALSE(media_session->IsActive());
  EXPECT_TRUE(other_media_session->IsActive());

  media_session->Stop(MediaSession::SuspendType::UI);
  other_media_session->Stop(MediaSession::SuspendType::UI);
}

}  // namespace content
