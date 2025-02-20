// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/extensions/extension_message_bubble_bridge.h"

#include <utility>

#include "chrome/browser/extensions/extension_message_bubble_controller.h"
#include "chrome/browser/ui/cocoa/extensions/toolbar_actions_bar_bubble_mac.h"

ExtensionMessageBubbleBridge::ExtensionMessageBubbleBridge(
    scoped_ptr<extensions::ExtensionMessageBubbleController> controller,
    bool anchored_to_extension)
    : controller_(std::move(controller)),
      anchored_to_extension_(anchored_to_extension) {}

ExtensionMessageBubbleBridge::~ExtensionMessageBubbleBridge() {
}

base::string16 ExtensionMessageBubbleBridge::GetHeadingText() {
  return controller_->delegate()->GetTitle();
}

base::string16 ExtensionMessageBubbleBridge::GetBodyText() {
  return controller_->delegate()->GetMessageBody(
      anchored_to_extension_,
      controller_->GetExtensionIdList().size());
}

base::string16 ExtensionMessageBubbleBridge::GetItemListText() {
  return controller_->GetExtensionListForDisplay();
}

base::string16 ExtensionMessageBubbleBridge::GetActionButtonText() {
  return controller_->delegate()->GetActionButtonLabel();
}

base::string16 ExtensionMessageBubbleBridge::GetDismissButtonText() {
  return controller_->delegate()->GetDismissButtonLabel();
}

base::string16 ExtensionMessageBubbleBridge::GetLearnMoreButtonText() {
  return controller_->delegate()->GetLearnMoreLabel();
}

std::string ExtensionMessageBubbleBridge::GetAnchorActionId() {
  return controller_->GetExtensionIdList().size() == 1u ?
      controller_->GetExtensionIdList()[0] : std::string();
}

void ExtensionMessageBubbleBridge::OnBubbleShown() {
}

void ExtensionMessageBubbleBridge::OnBubbleClosed(CloseAction action) {
  switch(action) {
    case CLOSE_DISMISS_USER_ACTION:
    case CLOSE_DISMISS_DEACTIVATION: {
      bool close_by_deactivate = action == CLOSE_DISMISS_DEACTIVATION;
      controller_->OnBubbleDismiss(close_by_deactivate);
      break;
    }
    case CLOSE_EXECUTE:
      controller_->OnBubbleAction();
      break;
    case CLOSE_LEARN_MORE:
      controller_->OnLinkClicked();
      break;
  }
}
