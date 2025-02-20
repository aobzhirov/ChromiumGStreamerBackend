// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included message file, hence no include guard here, but see below
// for a much smaller-than-usual include guard section.

#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/common/establish_channel_params.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/common/value_state.h"
#include "gpu/command_buffer/service/gpu_preferences.h"
#include "gpu/config/gpu_info.h"
#include "gpu/ipc/common/gpu_command_buffer_traits.h"
#include "gpu/ipc/common/gpu_memory_uma_stats.h"
#include "gpu/ipc/common/gpu_param_traits.h"
#include "gpu/ipc/common/memory_stats.h"
#include "gpu/ipc/common/surface_handle.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_message_start.h"
#include "ui/events/ipc/latency_info_param_traits.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "ui/gfx/ipc/gfx_param_traits.h"
#include "ui/gfx/ipc/skia/gfx_skia_param_traits.h"
#include "url/gurl.h"
#include "url/ipc/url_param_traits.h"

#if defined(OS_MACOSX)
#include "content/common/accelerated_surface_buffers_swapped_params_mac.h"
#include "content/common/buffer_presented_params_mac.h"
#include "ui/base/cocoa/remote_layer_api.h"
#include "ui/gfx/mac/io_surface.h"
#endif

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT

#define IPC_MESSAGE_START GpuMsgStart

IPC_STRUCT_TRAITS_BEGIN(gpu::GPUMemoryUmaStats)
  IPC_STRUCT_TRAITS_MEMBER(bytes_allocated_current)
  IPC_STRUCT_TRAITS_MEMBER(bytes_allocated_max)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::VideoMemoryUsageStats)
  IPC_STRUCT_TRAITS_MEMBER(process_map)
  IPC_STRUCT_TRAITS_MEMBER(bytes_allocated)
  IPC_STRUCT_TRAITS_MEMBER(bytes_allocated_historical_max)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::VideoMemoryUsageStats::ProcessStats)
  IPC_STRUCT_TRAITS_MEMBER(video_memory)
  IPC_STRUCT_TRAITS_MEMBER(has_duplicates)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_BEGIN(GpuMsg_CreateGpuMemoryBuffer_Params)
  IPC_STRUCT_MEMBER(gfx::GpuMemoryBufferId, id)
  IPC_STRUCT_MEMBER(gfx::Size, size)
  IPC_STRUCT_MEMBER(gfx::BufferFormat, format)
  IPC_STRUCT_MEMBER(gfx::BufferUsage, usage)
  IPC_STRUCT_MEMBER(int32_t, client_id)
  IPC_STRUCT_MEMBER(gpu::SurfaceHandle, surface_handle)
IPC_STRUCT_END()

IPC_STRUCT_BEGIN(GpuMsg_CreateGpuMemoryBufferFromHandle_Params)
  IPC_STRUCT_MEMBER(gfx::GpuMemoryBufferHandle, handle)
  IPC_STRUCT_MEMBER(gfx::GpuMemoryBufferId, id)
  IPC_STRUCT_MEMBER(gfx::Size, size)
  IPC_STRUCT_MEMBER(gfx::BufferFormat, format)
  IPC_STRUCT_MEMBER(int32_t, client_id)
IPC_STRUCT_END()

IPC_STRUCT_TRAITS_BEGIN(content::EstablishChannelParams)
  IPC_STRUCT_TRAITS_MEMBER(client_id)
  IPC_STRUCT_TRAITS_MEMBER(client_tracing_id)
  IPC_STRUCT_TRAITS_MEMBER(preempts)
  IPC_STRUCT_TRAITS_MEMBER(allow_view_command_buffers)
  IPC_STRUCT_TRAITS_MEMBER(allow_real_time_streams)
IPC_STRUCT_TRAITS_END()

#if defined(OS_MACOSX)
IPC_STRUCT_TRAITS_BEGIN(content::AcceleratedSurfaceBuffersSwappedParams)
  IPC_STRUCT_TRAITS_MEMBER(surface_id)
  // Only one of ca_context_id or io_surface may be non-0.
  IPC_STRUCT_TRAITS_MEMBER(ca_context_id)
  IPC_STRUCT_TRAITS_MEMBER(io_surface)
  IPC_STRUCT_TRAITS_MEMBER(size)
  IPC_STRUCT_TRAITS_MEMBER(scale_factor)
  IPC_STRUCT_TRAITS_MEMBER(latency_info)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(content::BufferPresentedParams)
  // The vsync parameters, to synchronize presentation with the display.
  IPC_STRUCT_TRAITS_MEMBER(surface_id)
  IPC_STRUCT_TRAITS_MEMBER(vsync_timebase)
  IPC_STRUCT_TRAITS_MEMBER(vsync_interval)
IPC_STRUCT_TRAITS_END()
#endif

IPC_STRUCT_TRAITS_BEGIN(gpu::GpuPreferences)
  IPC_STRUCT_TRAITS_MEMBER(single_process)
  IPC_STRUCT_TRAITS_MEMBER(in_process_gpu)
  IPC_STRUCT_TRAITS_MEMBER(ui_prioritize_in_gpu_process)
  IPC_STRUCT_TRAITS_MEMBER(disable_accelerated_video_decode)
#if defined(OS_CHROMEOS)
  IPC_STRUCT_TRAITS_MEMBER(disable_vaapi_accelerated_video_encode)
#endif
#if defined(ENABLE_WEBRTC)
  IPC_STRUCT_TRAITS_MEMBER(disable_web_rtc_hw_encoding)
#endif
#if defined(OS_WIN)
  IPC_STRUCT_TRAITS_MEMBER(enable_accelerated_vpx_decode)
#endif
  IPC_STRUCT_TRAITS_MEMBER(compile_shader_always_succeeds)
  IPC_STRUCT_TRAITS_MEMBER(disable_gl_error_limit)
  IPC_STRUCT_TRAITS_MEMBER(disable_glsl_translator)
  IPC_STRUCT_TRAITS_MEMBER(disable_gpu_driver_bug_workarounds)
  IPC_STRUCT_TRAITS_MEMBER(disable_shader_name_hashing)
  IPC_STRUCT_TRAITS_MEMBER(enable_gpu_command_logging)
  IPC_STRUCT_TRAITS_MEMBER(enable_gpu_debugging)
  IPC_STRUCT_TRAITS_MEMBER(enable_gpu_service_logging_gpu)
  IPC_STRUCT_TRAITS_MEMBER(disable_gpu_program_cache)
  IPC_STRUCT_TRAITS_MEMBER(enforce_gl_minimums)
  IPC_STRUCT_TRAITS_MEMBER(force_gpu_mem_available)
  IPC_STRUCT_TRAITS_MEMBER(gpu_program_cache_size)
  IPC_STRUCT_TRAITS_MEMBER(disable_gpu_shader_disk_cache)
  IPC_STRUCT_TRAITS_MEMBER(enable_share_group_async_texture_upload)
  IPC_STRUCT_TRAITS_MEMBER(enable_subscribe_uniform_extension)
  IPC_STRUCT_TRAITS_MEMBER(enable_threaded_texture_mailboxes)
  IPC_STRUCT_TRAITS_MEMBER(gl_shader_interm_output)
  IPC_STRUCT_TRAITS_MEMBER(emulate_shader_precision)
  IPC_STRUCT_TRAITS_MEMBER(enable_gpu_service_logging)
  IPC_STRUCT_TRAITS_MEMBER(enable_gpu_service_tracing)
  IPC_STRUCT_TRAITS_MEMBER(enable_unsafe_es3_apis)
IPC_STRUCT_TRAITS_END()

//------------------------------------------------------------------------------
// GPU Messages
// These are messages from the browser to the GPU process.

// Tells the GPU process to initialize itself. The browser explicitly
// requests this be done so that we are guaranteed that the channel is set
// up between the browser and GPU process before doing any work that might
// potentially crash the GPU process. Detection of the child process
// exiting abruptly is predicated on having the IPC channel set up.
IPC_MESSAGE_CONTROL1(GpuMsg_Initialize,
                     gpu::GpuPreferences /* gpu_prefernces */)

// Tells the GPU process to shutdown itself.
IPC_MESSAGE_CONTROL0(GpuMsg_Finalize)

// Tells the GPU process to create a new channel for communication with a
// given client.  The channel name is returned in a
// GpuHostMsg_ChannelEstablished message.  The client ID is passed so
// that the GPU process reuses an existing channel to that process if it exists.
// This ID is a unique opaque identifier generated by the browser process.
// The client_tracing_id is a unique ID used for the purposes of tracing.
IPC_MESSAGE_CONTROL1(GpuMsg_EstablishChannel,
                     content::EstablishChannelParams /* params */)

// Tells the GPU process to close the channel identified by |client_id|.
// If no channel can be identified, do nothing.
IPC_MESSAGE_CONTROL1(GpuMsg_CloseChannel, int32_t /* client_id */)

// Tells the GPU process to create a new gpu memory buffer.
IPC_MESSAGE_CONTROL1(GpuMsg_CreateGpuMemoryBuffer,
                     GpuMsg_CreateGpuMemoryBuffer_Params)

// Tells the GPU process to create a new gpu memory buffer from an existing
// handle.
IPC_MESSAGE_CONTROL1(GpuMsg_CreateGpuMemoryBufferFromHandle,
                     GpuMsg_CreateGpuMemoryBufferFromHandle_Params)

// Tells the GPU process to destroy buffer.
IPC_MESSAGE_CONTROL3(GpuMsg_DestroyGpuMemoryBuffer,
                     gfx::GpuMemoryBufferId, /* id */
                     int32_t,                /* client_id */
                     gpu::SyncToken /* sync_token */)

// Tells the GPU process to create a context for collecting graphics card
// information.
IPC_MESSAGE_CONTROL0(GpuMsg_CollectGraphicsInfo)

// Tells the GPU process to report video_memory information for the task manager
IPC_MESSAGE_CONTROL0(GpuMsg_GetVideoMemoryUsageStats)

#if defined(OS_MACOSX)
// Tells the GPU process that the browser process has handled the swap
// buffers or post sub-buffer request.
IPC_MESSAGE_CONTROL1(AcceleratedSurfaceMsg_BufferPresented,
                     content::BufferPresentedParams)
#endif

#if defined(OS_ANDROID)
// Tells the GPU process to wake up the GPU because we're about to draw.
IPC_MESSAGE_CONTROL0(GpuMsg_WakeUpGpu)
#endif

// Tells the GPU process to remove all contexts.
IPC_MESSAGE_CONTROL0(GpuMsg_Clean)

// Tells the GPU process to crash.
IPC_MESSAGE_CONTROL0(GpuMsg_Crash)

// Tells the GPU process to hang.
IPC_MESSAGE_CONTROL0(GpuMsg_Hang)

// Tells the GPU process to disable the watchdog thread.
IPC_MESSAGE_CONTROL0(GpuMsg_DisableWatchdog)

// Tells the GPU process that the browser has seen a GPU switch.
IPC_MESSAGE_CONTROL0(GpuMsg_GpuSwitched)

// Sends an input event to the gpu service.
IPC_MESSAGE_CONTROL3(GpuMsg_UpdateValueState,
                     int,          /* client_id */
                     unsigned int, /* target */
                     gpu::ValueState /* valuestate */)

//------------------------------------------------------------------------------
// GPU Host Messages
// These are messages to the browser.

// Response from GPU to a GputMsg_Initialize message.
IPC_MESSAGE_CONTROL2(GpuHostMsg_Initialized,
                     bool /* result */,
                     ::gpu::GPUInfo /* gpu_info */)

// Response from GPU to a GpuHostMsg_EstablishChannel message.
IPC_MESSAGE_CONTROL1(GpuHostMsg_ChannelEstablished,
                     IPC::ChannelHandle /* channel_handle */)

// Message from GPU to notify to destroy the channel.
IPC_MESSAGE_CONTROL1(GpuHostMsg_DestroyChannel, int32_t /* client_id */)

// Message to cache the given shader information.
IPC_MESSAGE_CONTROL3(GpuHostMsg_CacheShader,
                     int32_t /* client_id */,
                     std::string /* key */,
                     std::string /* shader */)

// Message to the GPU that a shader was loaded from disk.
IPC_MESSAGE_CONTROL1(GpuMsg_LoadedShader, std::string /* encoded shader */)

// Response from GPU to a GpuMsg_CreateGpuMemoryBuffer message.
IPC_MESSAGE_CONTROL1(GpuHostMsg_GpuMemoryBufferCreated,
                     gfx::GpuMemoryBufferHandle /* handle */)

// Response from GPU to a GpuMsg_CollectGraphicsInfo.
IPC_MESSAGE_CONTROL1(GpuHostMsg_GraphicsInfoCollected,
                     gpu::GPUInfo /* GPU logging stats */)

// Response from GPU to a GpuMsg_GetVideoMemory.
IPC_MESSAGE_CONTROL1(GpuHostMsg_VideoMemoryUsageStats,
                     gpu::VideoMemoryUsageStats /* GPU memory stats */)

#if defined(OS_MACOSX)
// Tells the browser that an accelerated surface has swapped.
IPC_MESSAGE_CONTROL1(GpuHostMsg_AcceleratedSurfaceBuffersSwapped,
                     content::AcceleratedSurfaceBuffersSwappedParams)
#endif

#if defined(OS_WIN)
IPC_MESSAGE_CONTROL2(GpuHostMsg_AcceleratedSurfaceCreatedChildWindow,
                     gpu::SurfaceHandle /* parent_window */,
                     gpu::SurfaceHandle /* child_window */)
#endif

IPC_MESSAGE_CONTROL1(GpuHostMsg_DidCreateOffscreenContext, GURL /* url */)

IPC_MESSAGE_CONTROL3(GpuHostMsg_DidLoseContext,
                     bool /* offscreen */,
                     gpu::error::ContextLostReason /* reason */,
                     GURL /* url */)

IPC_MESSAGE_CONTROL1(GpuHostMsg_DidDestroyOffscreenContext, GURL /* url */)

// Tells the browser about GPU memory usage statistics for UMA logging.
IPC_MESSAGE_CONTROL1(GpuHostMsg_GpuMemoryUmaStats,
                     gpu::GPUMemoryUmaStats /* GPU memory UMA stats */)

// Tells the browser that a context has subscribed to a new target and
// the browser should start sending the corresponding information
IPC_MESSAGE_CONTROL2(GpuHostMsg_AddSubscription,
                     int32_t /* client_id */,
                     unsigned int /* target */)

// Tells the browser that no contexts are subscribed to the target anymore
// so the browser should stop sending the corresponding information
IPC_MESSAGE_CONTROL2(GpuHostMsg_RemoveSubscription,
                     int32_t /* client_id */,
                     unsigned int /* target */)

// Message from GPU to add a GPU log message to the about:gpu page.
IPC_MESSAGE_CONTROL3(GpuHostMsg_OnLogMessage,
                     int /*severity*/,
                     std::string /* header */,
                     std::string /* message */)
