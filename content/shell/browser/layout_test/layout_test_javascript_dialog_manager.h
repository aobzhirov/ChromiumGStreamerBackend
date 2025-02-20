// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_JAVASCRIPT_DIALOG_MANAGER_H_
#define CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_JAVASCRIPT_DIALOG_MANAGER_H_

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "content/shell/browser/shell_javascript_dialog_manager.h"

namespace content {

class LayoutTestJavaScriptDialogManager : public ShellJavaScriptDialogManager {
 public:
  LayoutTestJavaScriptDialogManager();
  ~LayoutTestJavaScriptDialogManager() override;

  // JavaScriptDialogManager:
  void RunJavaScriptDialog(WebContents* web_contents,
                           const GURL& origin_url,
                           const std::string& accept_lang,
                           JavaScriptMessageType javascript_message_type,
                           const base::string16& message_text,
                           const base::string16& default_prompt_text,
                           const DialogClosedCallback& callback,
                           bool* did_suppress_message) override;

  void RunBeforeUnloadDialog(WebContents* web_contents,
                             bool is_reload,
                             const DialogClosedCallback& callback) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(LayoutTestJavaScriptDialogManager);
};

}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_JAVASCRIPT_DIALOG_MANAGER_H_
