// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_CLIPBOARD_H_
#define REMOTING_HOST_CLIPBOARD_H_

#include <string>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"

namespace remoting {

namespace protocol {
class ClipboardEvent;
class ClipboardStub;
}  // namespace protocol

// All Clipboard methods should be run on the UI thread, so that the Clipboard
// can get change notifications.
class Clipboard {
 public:
  virtual ~Clipboard() {}

  // Initialises any objects needed to read from or write to the clipboard.
  virtual void Start(scoped_ptr<protocol::ClipboardStub> client_clipboard) = 0;

  // Writes an item to the clipboard. It must be called after Start().
  virtual void InjectClipboardEvent(const protocol::ClipboardEvent& event) = 0;

  static scoped_ptr<Clipboard> Create();
};

}  // namespace remoting

#endif  // REMOTING_HOST_CLIPBOARD_H_
