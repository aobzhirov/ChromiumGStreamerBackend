// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_COMMON_PLATFORM_NOTIFICATION_DATA_H_
#define CONTENT_PUBLIC_COMMON_PLATFORM_NOTIFICATION_DATA_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/strings/string16.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "url/gurl.h"

namespace content {

// A notification action (button); corresponds to Blink WebNotificationAction.
struct CONTENT_EXPORT PlatformNotificationAction {
  PlatformNotificationAction();
  ~PlatformNotificationAction();

  // Action name that the author can use to distinguish them.
  std::string action;

  // Title of the button.
  base::string16 title;

  // URL of the icon for the button. May be empty if no url was specified.
  GURL icon;
};

// Structure representing the information associated with a Web Notification.
// This struct should include the developer-visible information, kept
// synchronized with the WebNotificationData structure defined in the Blink API.
struct CONTENT_EXPORT PlatformNotificationData {
  PlatformNotificationData();
  PlatformNotificationData(const PlatformNotificationData& other);
  ~PlatformNotificationData();

  // The maximum size of developer-provided data to be stored in the |data|
  // property of this structure.
  static const size_t kMaximumDeveloperDataSize = 1024 * 1024;

  enum Direction {
    DIRECTION_LEFT_TO_RIGHT,
    DIRECTION_RIGHT_TO_LEFT,
    DIRECTION_AUTO,

    DIRECTION_LAST = DIRECTION_AUTO
  };

  // Title to be displayed with the Web Notification.
  base::string16 title;

  // Hint to determine the directionality of the displayed notification.
  Direction direction = DIRECTION_LEFT_TO_RIGHT;

  // BCP 47 language tag describing the notification's contents. Optional.
  std::string lang;

  // Contents of the notification.
  base::string16 body;

  // Tag of the notification. Notifications sharing both their origin and their
  // tag will replace the first displayed notification.
  std::string tag;

  // URL of the icon which is to be displayed with the notification.
  GURL icon;

  // URL of the badge for representing the notification. May be empty if no url
  // was specified.
  GURL badge;

  // Vibration pattern for the notification, following the syntax of the
  // Vibration API. https://www.w3.org/TR/vibration/
  std::vector<int> vibration_pattern;

  // The time at which the event the notification represents took place.
  base::Time timestamp;

  // Whether default notification indicators (sound, vibration, light) should
  // be played again if the notification is replacing an older notification.
  bool renotify = false;

  // Whether default notification indicators (sound, vibration, light) should
  // be suppressed.
  bool silent = false;

  // Whether the notification should remain onscreen indefinitely, rather than
  // being auto-minimized to the notification center (if allowed by platform).
  bool require_interaction = false;

  // Developer-provided data associated with the notification, in the form of
  // a serialized string. Must not exceed |kMaximumDeveloperDataSize| bytes.
  std::vector<char> data;

  // Actions that should be shown as buttons on the notification.
  std::vector<PlatformNotificationAction> actions;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_COMMON_PLATFORM_NOTIFICATION_DATA_H_
