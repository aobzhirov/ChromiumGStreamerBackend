// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Multiply-included message file, hence no include guard.

#include <stdint.h>

#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/common/content_param_traits.h"
#include "content/common/cursors/webcursor.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_message_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/ipc/gfx_param_traits.h"
#include "ui/gfx/ipc/skia/gfx_skia_param_traits.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/surface/transport_dib.h"
#include "url/ipc/url_param_traits.h"

#if defined(OS_POSIX)
#include "base/file_descriptor_posix.h"
#endif

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT

#define IPC_MESSAGE_START PluginMsgStart

IPC_STRUCT_BEGIN(PluginMsg_Init_Params)
  IPC_STRUCT_MEMBER(GURL,  url)
  IPC_STRUCT_MEMBER(GURL,  page_url)
  IPC_STRUCT_MEMBER(std::vector<std::string>, arg_names)
  IPC_STRUCT_MEMBER(std::vector<std::string>, arg_values)
  IPC_STRUCT_MEMBER(bool, load_manually)
  IPC_STRUCT_MEMBER(int, host_render_view_routing_id)
IPC_STRUCT_END()

IPC_STRUCT_BEGIN(PluginMsg_UpdateGeometry_Param)
  IPC_STRUCT_MEMBER(gfx::Rect, window_rect)
  IPC_STRUCT_MEMBER(gfx::Rect, clip_rect)
  IPC_STRUCT_MEMBER(TransportDIB::Handle, windowless_buffer0)
  IPC_STRUCT_MEMBER(TransportDIB::Handle, windowless_buffer1)
  IPC_STRUCT_MEMBER(int, windowless_buffer_index)
IPC_STRUCT_END()

//-----------------------------------------------------------------------------
// Plugin messages
// These are messages sent from the renderer process to the plugin process.
// Tells the plugin process to create a new plugin instance with the given
// id.  A corresponding WebPluginDelegateStub is created which hosts the
// WebPluginDelegateImpl.
IPC_SYNC_MESSAGE_CONTROL1_1(PluginMsg_CreateInstance,
                            std::string /* mime_type */,
                            int /* instance_id */)

// The WebPluginDelegateProxy sends this to the WebPluginDelegateStub in its
// destructor, so that the stub deletes the actual WebPluginDelegateImpl
// object that it's hosting.
IPC_SYNC_MESSAGE_CONTROL1_0(PluginMsg_DestroyInstance,
                            int /* instance_id */)

IPC_SYNC_MESSAGE_CONTROL0_1(PluginMsg_GenerateRouteID,
                           int /* id */)

// The messages below all map to WebPluginDelegate methods.
IPC_SYNC_MESSAGE_ROUTED1_2(PluginMsg_Init,
                           PluginMsg_Init_Params,
                           bool /* transparent */,
                           bool /* result */)

// Used to synchronously request a paint for windowless plugins.
IPC_SYNC_MESSAGE_ROUTED1_0(PluginMsg_Paint,
                           gfx::Rect /* damaged_rect */)

// Sent by the renderer after it paints from its backing store so that the
// plugin knows it can send more invalidates.
IPC_MESSAGE_ROUTED0(PluginMsg_DidPaint)

// Updates the plugin location.
IPC_MESSAGE_ROUTED1(PluginMsg_UpdateGeometry,
                    PluginMsg_UpdateGeometry_Param)

// A synchronous version of above.
IPC_SYNC_MESSAGE_ROUTED1_0(PluginMsg_UpdateGeometrySync,
                           PluginMsg_UpdateGeometry_Param)

IPC_SYNC_MESSAGE_ROUTED1_0(PluginMsg_SetFocus,
                           bool /* focused */)

IPC_SYNC_MESSAGE_ROUTED1_2(PluginMsg_HandleInputEvent,
                           IPC::WebInputEventPointer /* event */,
                           bool /* handled */,
                           content::WebCursor /* cursor type*/)

IPC_MESSAGE_ROUTED1(PluginMsg_SetContentAreaFocus,
                    bool /* has_focus */)

IPC_MESSAGE_CONTROL1(PluginMsg_SignalModalDialogEvent,
                     int /* render_view_id */)

IPC_MESSAGE_CONTROL1(PluginMsg_ResetModalDialogEvent,
                     int /* render_view_id */)

#if defined(OS_MACOSX)
IPC_MESSAGE_ROUTED1(PluginMsg_SetWindowFocus,
                    bool /* has_focus */)

IPC_MESSAGE_ROUTED0(PluginMsg_ContainerHidden)

IPC_MESSAGE_ROUTED3(PluginMsg_ContainerShown,
                    gfx::Rect /* window_frame */,
                    gfx::Rect /* view_frame */,
                    bool /* has_focus */)

IPC_MESSAGE_ROUTED2(PluginMsg_WindowFrameChanged,
                    gfx::Rect /* window_frame */,
                    gfx::Rect /* view_frame */)

IPC_MESSAGE_ROUTED1(PluginMsg_ImeCompositionCompleted,
                    base::string16 /* text */)
#endif

//-----------------------------------------------------------------------------
// PluginHost messages
// These are messages sent from the plugin process to the renderer process.
// They all map to the corresponding WebPlugin methods.

IPC_MESSAGE_ROUTED1(PluginHostMsg_InvalidateRect,
                    gfx::Rect /* rect */)

IPC_SYNC_MESSAGE_ROUTED1_2(PluginHostMsg_ResolveProxy,
                           GURL /* url */,
                           bool /* result */,
                           std::string /* proxy list */)

IPC_MESSAGE_ROUTED3(PluginHostMsg_SetCookie,
                    GURL /* url */,
                    GURL /* first_party_for_cookies */,
                    std::string /* cookie */)

IPC_SYNC_MESSAGE_ROUTED2_1(PluginHostMsg_GetCookies,
                           GURL /* url */,
                           GURL /* first_party_for_cookies */,
                           std::string /* cookies */)

IPC_MESSAGE_ROUTED0(PluginHostMsg_CancelDocumentLoad)

IPC_MESSAGE_ROUTED0(PluginHostMsg_DidStartLoading)
IPC_MESSAGE_ROUTED0(PluginHostMsg_DidStopLoading)

IPC_MESSAGE_CONTROL0(PluginHostMsg_PluginShuttingDown)

#if defined(OS_MACOSX)
IPC_MESSAGE_ROUTED1(PluginHostMsg_FocusChanged,
                    bool /* focused */)

IPC_MESSAGE_ROUTED0(PluginHostMsg_StartIme)

//----------------------------------------------------------------------
// Core Animation plugin implementation rendering via compositor.

// Notifies the renderer process that this plugin will be using the
// accelerated rendering path.
IPC_MESSAGE_ROUTED0(PluginHostMsg_AcceleratedPluginEnabledRendering)

// Notifies the renderer process that the plugin allocated a new
// IOSurface into which it is rendering. The renderer process forwards
// this IOSurface to the GPU process, causing it to be bound to a
// texture from which the compositor can render. Any previous
// IOSurface allocated by this plugin must be implicitly released by
// the receipt of this message.
IPC_MESSAGE_ROUTED3(PluginHostMsg_AcceleratedPluginAllocatedIOSurface,
                    int32_t /* width */,
                    int32_t /* height */,
                    uint32_t /* surface_id */)

// Notifies the renderer process that the plugin produced a new frame
// of content into its IOSurface, and therefore that the compositor
// needs to redraw.
IPC_MESSAGE_ROUTED0(PluginHostMsg_AcceleratedPluginSwappedIOSurface)
#endif
