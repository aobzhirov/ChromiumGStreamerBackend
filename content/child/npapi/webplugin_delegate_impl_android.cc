// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/npapi/webplugin_delegate_impl.h"

#include <string.h>

#include "base/logging.h"
#include "content/child/npapi/plugin_instance.h"
#include "content/child/npapi/webplugin.h"
#include "content/common/cursors/webcursor.h"

using blink::WebInputEvent;

namespace content {

WebPluginDelegateImpl::WebPluginDelegateImpl(
    WebPlugin* plugin,
    PluginInstance* instance)
    : plugin_(plugin),
      instance_(instance),
      quirks_(0),
      handle_event_depth_(0),
      first_set_window_call_(true) {
  memset(&window_, 0, sizeof(window_));
}

WebPluginDelegateImpl::~WebPluginDelegateImpl() {
}

bool WebPluginDelegateImpl::PlatformInitialize() {
  return true;
}

void WebPluginDelegateImpl::PlatformDestroyInstance() {
  // Nothing to do here.
}

void WebPluginDelegateImpl::Paint(SkCanvas* canvas, const gfx::Rect& rect) {
}

void WebPluginDelegateImpl::WindowlessUpdateGeometry(
    const gfx::Rect& window_rect,
    const gfx::Rect& clip_rect) {
}

void WebPluginDelegateImpl::WindowlessPaint(gfx::NativeDrawingContext context,
                                            const gfx::Rect& damage_rect) {
}

bool WebPluginDelegateImpl::PlatformSetPluginHasFocus(bool focused) {
  return false;
}

bool WebPluginDelegateImpl::PlatformHandleInputEvent(
    const WebInputEvent& event, WebCursor::CursorInfo* cursor_info) {
  return false;
}

}  // content
