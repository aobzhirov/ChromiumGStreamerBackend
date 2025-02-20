// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/npapi/webplugin_impl.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/debug/crash_logging.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/linked_ptr.h"
#include "base/metrics/user_metrics_action.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "cc/blink/web_layer_impl.h"
#include "cc/layers/io_surface_layer.h"
#include "content/child/appcache/web_application_cache_host_impl.h"
#include "content/child/npapi/plugin_host.h"
#include "content/child/npapi/plugin_instance.h"
#include "content/child/npapi/webplugin_delegate_impl.h"
#include "content/child/npapi/webplugin_resource_client.h"
#include "content/common/view_messages.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/renderer/npapi/webplugin_delegate_proxy.h"
#include "content/renderer/render_frame_impl.h"
#include "content/renderer/render_process.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/render_view_impl.h"
#include "net/base/escape.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/WebKit/public/platform/WebCookieJar.h"
#include "third_party/WebKit/public/platform/WebCursorInfo.h"
#include "third_party/WebKit/public/platform/WebData.h"
#include "third_party/WebKit/public/platform/WebHTTPBody.h"
#include "third_party/WebKit/public/platform/WebHTTPHeaderVisitor.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLLoader.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"
#include "third_party/WebKit/public/web/WebConsoleMessage.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"
#include "third_party/WebKit/public/web/WebKit.h"
#include "third_party/WebKit/public/web/WebPluginContainer.h"
#include "third_party/WebKit/public/web/WebPluginParams.h"
#include "third_party/WebKit/public/web/WebURLLoaderOptions.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "url/url_util.h"

using blink::WebCanvas;
using blink::WebConsoleMessage;
using blink::WebCookieJar;
using blink::WebCursorInfo;
using blink::WebData;
using blink::WebDataSource;
using blink::WebFrame;
using blink::WebHTTPBody;
using blink::WebHTTPHeaderVisitor;
using blink::WebInputEvent;
using blink::WebInputEventResult;
using blink::WebKeyboardEvent;
using blink::WebMouseEvent;
using blink::WebPluginContainer;
using blink::WebPluginParams;
using blink::WebRect;
using blink::WebString;
using blink::WebURL;
using blink::WebURLError;
using blink::WebURLLoader;
using blink::WebURLLoaderOptions;
using blink::WebURLRequest;
using blink::WebURLResponse;
using blink::WebVector;
using blink::WebView;

namespace content {

// blink::WebPlugin ----------------------------------------------------------

bool WebPluginImpl::initialize(WebPluginContainer* container) {
  if (!render_view_.get()) {
    LOG(ERROR) << "No RenderView";
    return false;
  }

  WebPluginDelegateProxy* plugin_delegate = new WebPluginDelegateProxy(
      this, mime_type_, render_view_, render_frame_);

  // Set the container before Initialize because the plugin may
  // synchronously call NPN_GetValue to get its container, or make calls
  // passing script objects that need to be tracked, during initialization.
  SetContainer(container);

  bool ok = plugin_delegate->Initialize(
      plugin_url_, arg_names_, arg_values_, load_manually_);
  if (!ok) {
    plugin_delegate->PluginDestroyed();

    blink::WebPlugin* replacement_plugin =
        GetContentClient()->renderer()->CreatePluginReplacement(
            render_frame_, file_path_);
    if (!replacement_plugin) {
      // Maintain invariant that container() returns null when initialize()
      // returns false.
      SetContainer(nullptr);
      return false;
    }

    // The replacement plugin, if it exists, must never fail to initialize.
    container->setPlugin(replacement_plugin);
    CHECK(replacement_plugin->initialize(container));

    DCHECK(container->plugin() == replacement_plugin);
    DCHECK(replacement_plugin->container() == container);

    // Since the container now owns the replacement plugin instead of this
    // object, we must schedule ourselves for deletion. This also implicitly
    // disables scripting while un-setting the container.
    destroy();

    return true;
  }

  delegate_ = plugin_delegate;

  return true;
}

void WebPluginImpl::destroy() {
  SetContainer(NULL);
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void WebPluginImpl::updateAllLifecyclePhases() {
  if (!container_)
    return;

#if defined(OS_WIN)
  // Force a geometry update if needed to allow plugins like media player
  // which defer the initial geometry update to work. Do it now, rather
  // than in paint, so that the paint rect invalidation is registered.
  // Otherwise we may never get the paint call.
  container_->reportGeometry();
#endif  // OS_WIN
}

void WebPluginImpl::paint(WebCanvas* canvas, const WebRect& paint_rect) {
  if (!delegate_ || !container_)
    return;

  // Note that |canvas| is only used when in windowless mode.
  delegate_->Paint(canvas, paint_rect);
}

void WebPluginImpl::updateGeometry(const WebRect& window_rect,
                                   const WebRect& clip_rect,
                                   const WebRect& unobscured_rect,
                                   const WebVector<WebRect>& cut_outs_rects,
                                   bool is_visible) {
  WebPluginGeometry new_geometry;
  new_geometry.window_rect = window_rect;
  new_geometry.clip_rect = clip_rect;
  new_geometry.visible = is_visible;
  new_geometry.rects_valid = true;
  for (size_t i = 0; i < cut_outs_rects.size(); ++i)
    new_geometry.cutout_rects.push_back(cut_outs_rects[i]);

  // Only UpdateGeometry if either the window or clip rects have changed.
  if (delegate_ && (first_geometry_update_ ||
      new_geometry.window_rect != geometry_.window_rect ||
      new_geometry.clip_rect != geometry_.clip_rect)) {
    // Notify the plugin that its parameters have changed.
    delegate_->UpdateGeometry(new_geometry.window_rect, new_geometry.clip_rect);
  }

#if defined(OS_WIN)
  // Don't cache the geometry during the first geometry update. The first
  // geometry update sequence is received when Widget::setParent is called.
  // For plugins like media player which have a bug where they only honor
  // the first geometry update, we have a quirk which ignores the first
  // geometry update. To ensure that these plugins work correctly in cases
  // where we receive only one geometry update from webkit, we also force
  // a geometry update during paint which should go out correctly as the
  // initial geometry update was not cached.
  if (!first_geometry_update_)
    geometry_ = new_geometry;
#else  // OS_WIN
  geometry_ = new_geometry;
#endif  // OS_WIN
  first_geometry_update_ = false;
}

void WebPluginImpl::updateFocus(bool focused, blink::WebFocusType focus_type) {
  delegate_->SetFocus(focused);
}

void WebPluginImpl::updateVisibility(bool visible) {
}

bool WebPluginImpl::acceptsInputEvents() {
  return true;
}

WebInputEventResult WebPluginImpl::handleInputEvent(
    const WebInputEvent& event,
    WebCursorInfo& cursor_info) {
  // Swallow context menu events in order to suppress the default context menu.
  if (event.type == WebInputEvent::ContextMenu)
    return WebInputEventResult::HandledSuppressed;

  WebCursor::CursorInfo web_cursor_info;
  bool ret = delegate_->HandleInputEvent(event, &web_cursor_info);
  cursor_info.type = web_cursor_info.type;
  cursor_info.hotSpot = web_cursor_info.hotspot;
  cursor_info.customImage = web_cursor_info.custom_image;
  cursor_info.imageScaleFactor = web_cursor_info.image_scale_factor;
#if defined(OS_WIN)
  cursor_info.externalHandle = web_cursor_info.external_handle;
#endif
  return ret ? WebInputEventResult::HandledApplication
             : WebInputEventResult::NotHandled;
}

bool WebPluginImpl::isPlaceholder() {
  return false;
}

// -----------------------------------------------------------------------------

WebPluginImpl::WebPluginImpl(
    WebFrame* webframe,
    const WebPluginParams& params,
    const base::FilePath& file_path,
    const base::WeakPtr<RenderViewImpl>& render_view,
    RenderFrameImpl* render_frame)
    : render_frame_(render_frame),
      render_view_(render_view),
      webframe_(webframe),
      delegate_(NULL),
      container_(NULL),
      plugin_url_(params.url),
      load_manually_(params.loadManually),
      first_geometry_update_(true),
      ignore_response_error_(false),
      file_path_(file_path),
      mime_type_(base::ToLowerASCII(base::UTF16ToASCII(
          base::StringPiece16(params.mimeType)))) {
  DCHECK_EQ(params.attributeNames.size(), params.attributeValues.size());

  for (size_t i = 0; i < params.attributeNames.size(); ++i) {
    arg_names_.push_back(params.attributeNames[i].utf8());
    arg_values_.push_back(params.attributeValues[i].utf8());
  }

  // Set subresource URL for crash reporting.
  base::debug::SetCrashKeyValue("subresource_url", plugin_url_.spec());
}

WebPluginImpl::~WebPluginImpl() {
}

GURL WebPluginImpl::CompleteURL(const char* url) {
  if (!webframe_) {
    NOTREACHED();
    return GURL();
  }
  // TODO(darin): Is conversion from UTF8 correct here?
  return webframe_->document().completeURL(WebString::fromUTF8(url));
}

bool WebPluginImpl::SetPostData(WebURLRequest* request,
                                const char* buf,
                                uint32_t length) {
  std::vector<std::string> names;
  std::vector<std::string> values;
  std::vector<char> body;
  bool rv = PluginHost::SetPostData(buf, length, &names, &values, &body);

  for (size_t i = 0; i < names.size(); ++i) {
    request->addHTTPHeaderField(WebString::fromUTF8(names[i]),
                                WebString::fromUTF8(values[i]));
  }

  WebString content_type_header = WebString::fromUTF8("Content-Type");
  const WebString& content_type =
      request->httpHeaderField(content_type_header);
  if (content_type.isEmpty()) {
    request->setHTTPHeaderField(
        content_type_header,
        WebString::fromUTF8("application/x-www-form-urlencoded"));
  }

  WebHTTPBody http_body;
  if (body.size()) {
    http_body.initialize();
    http_body.appendData(WebData(&body[0], body.size()));
  }
  request->setHTTPBody(http_body);

  return rv;
}

bool WebPluginImpl::IsValidUrl(const GURL& url, ReferrerValue referrer_flag) {
  if (referrer_flag == PLUGIN_SRC &&
      mime_type_ == kFlashPluginSwfMimeType &&
      url.GetOrigin() != plugin_url_.GetOrigin()) {
    // Do url check to make sure that there are no @, ;, \ chars in between url
    // scheme and url path.
    const char* url_to_check(url.spec().data());
    url::Parsed parsed;
    url::ParseStandardURL(url_to_check, strlen(url_to_check), &parsed);
    if (parsed.path.begin <= parsed.scheme.end())
      return true;
    std::string string_to_search;
    string_to_search.assign(url_to_check + parsed.scheme.end(),
        parsed.path.begin - parsed.scheme.end());
    if (string_to_search.find("@") != std::string::npos ||
        string_to_search.find(";") != std::string::npos ||
        string_to_search.find("\\") != std::string::npos)
      return false;
  }

  return true;
}

WebPluginImpl::RoutingStatus WebPluginImpl::RouteToFrame(
    const char* url,
    bool is_javascript_url,
    bool popups_allowed,
    const char* method,
    const char* target,
    const char* buf,
    unsigned int len,
    ReferrerValue referrer_flag) {
  // If there is no target, there is nothing to do
  if (!target)
    return NOT_ROUTED;

  // This could happen if the WebPluginContainer was already deleted.
  if (!webframe_)
    return NOT_ROUTED;

  WebString target_str = WebString::fromUTF8(target);

  // Take special action for JavaScript URLs
  if (is_javascript_url) {
    WebFrame* target_frame =
        webframe_->view()->findFrameByName(target_str, webframe_);
    // For security reasons, do not allow JavaScript on frames
    // other than this frame.
    if (target_frame != webframe_) {
      // TODO(darin): Localize this message.
      const char kMessage[] =
          "Ignoring cross-frame javascript URL load requested by plugin.";
      webframe_->addMessageToConsole(
          WebConsoleMessage(WebConsoleMessage::LevelError,
                            WebString::fromUTF8(kMessage)));
      return ROUTED;
    }

    // Route javascript calls back to the plugin.
    return NOT_ROUTED;
  }

  // If we got this far, we're routing content to a target frame.
  // Go fetch the URL.

  GURL complete_url = CompleteURL(url);
  // Remove when flash bug is fixed. http://crbug.com/40016.
  if (!WebPluginImpl::IsValidUrl(complete_url, referrer_flag))
    return INVALID_URL;

  if (strcmp(method, "GET") != 0) {
    // We're only going to route HTTP/HTTPS requests
    if (!complete_url.SchemeIsHTTPOrHTTPS())
      return INVALID_URL;
  }

  WebURLRequest request(complete_url);
  SetReferrer(&request, referrer_flag);

  request.setHTTPMethod(WebString::fromUTF8(method));
  request.setFirstPartyForCookies(
      webframe_->document().firstPartyForCookies());
  request.setHasUserGesture(popups_allowed);
  // ServiceWorker is disabled for NPAPI.
  request.setSkipServiceWorker(true);
  if (len > 0) {
    if (!SetPostData(&request, buf, len)) {
      // Uhoh - we're in trouble.  There isn't a good way
      // to recover at this point.  Break out.
      NOTREACHED();
      return ROUTED;
    }
  }

  container_->loadFrameRequest(request, target_str);
  return ROUTED;
}

bool WebPluginImpl::FindProxyForUrl(const GURL& url, std::string* proxy_list) {
  // Proxy resolving doesn't work in single-process mode.
  return false;
}

void WebPluginImpl::SetCookie(const GURL& url,
                              const GURL& first_party_for_cookies,
                              const std::string& cookie) {
  if (!render_view_.get())
    return;

  WebCookieJar* cookie_jar = render_frame_->cookie_jar();
  if (!cookie_jar) {
    DLOG(WARNING) << "No cookie jar!";
    return;
  }

  cookie_jar->setCookie(
      url, first_party_for_cookies, WebString::fromUTF8(cookie));
}

std::string WebPluginImpl::GetCookies(const GURL& url,
                                      const GURL& first_party_for_cookies) {
  if (!render_view_.get())
    return std::string();

  WebCookieJar* cookie_jar = render_frame_->cookie_jar();
  if (!cookie_jar) {
    DLOG(WARNING) << "No cookie jar!";
    return std::string();
  }

  return base::UTF16ToUTF8(base::StringPiece16(
      cookie_jar->cookies(url, first_party_for_cookies)));
}

#if defined(OS_MACOSX)
WebPluginAcceleratedSurface* WebPluginImpl::GetAcceleratedSurface(
    gfx::GpuPreference gpu_preference) {
  return NULL;
}

void WebPluginImpl::AcceleratedPluginEnabledRendering() {
}

void WebPluginImpl::AcceleratedPluginAllocatedIOSurface(int32_t width,
                                                        int32_t height,
                                                        uint32_t surface_id) {
  next_io_surface_allocated_ = true;
  next_io_surface_width_ = width;
  next_io_surface_height_ = height;
  next_io_surface_id_ = surface_id;
}

void WebPluginImpl::AcceleratedPluginSwappedIOSurface() {
  if (!container_)
    return;
  // Deferring the call to setBackingIOSurfaceId is an attempt to
  // work around garbage occasionally showing up in the plugin's
  // area during live resizing of Core Animation plugins. The
  // assumption was that by the time this was called, the plugin
  // process would have populated the newly allocated IOSurface. It
  // is not 100% clear at this point why any garbage is getting
  // through. More investigation is needed. http://crbug.com/105346
  if (next_io_surface_allocated_) {
    if (next_io_surface_id_) {
      if (!io_surface_layer_.get()) {
        io_surface_layer_ = cc::IOSurfaceLayer::Create();
        web_layer_.reset(new cc_blink::WebLayerImpl(io_surface_layer_));
        container_->setWebLayer(web_layer_.get());
      }
      io_surface_layer_->SetIOSurfaceProperties(
          next_io_surface_id_,
          gfx::Size(next_io_surface_width_, next_io_surface_height_));
    } else {
      container_->setWebLayer(NULL);
      web_layer_.reset();
      io_surface_layer_ = NULL;
    }
    next_io_surface_allocated_ = false;
  } else {
    if (io_surface_layer_.get())
      io_surface_layer_->SetNeedsDisplay();
  }
}
#endif

void WebPluginImpl::Invalidate() {
  if (container_)
    container_->invalidate();
}

void WebPluginImpl::InvalidateRect(const gfx::Rect& rect) {
  if (container_)
    container_->invalidateRect(rect);
}

void WebPluginImpl::SetContainer(WebPluginContainer* container) {
  if (!container)
    TearDownPluginInstance(NULL);
  container_ = container;
}

unsigned long WebPluginImpl::GetNextResourceId() {
  if (!webframe_)
    return 0;
  WebView* view = webframe_->view();
  if (!view)
    return 0;
  return view->createUniqueIdentifierForRequest();
}

void WebPluginImpl::CancelDocumentLoad() {
  if (webframe_) {
    ignore_response_error_ = true;
    webframe_->stopLoading();
  }
}

void WebPluginImpl::DidStartLoading() {
  if (render_view_.get()) {
    // TODO(darin): Make is_loading_ be a counter!
    render_view_->DidStartLoading();
  }
}

void WebPluginImpl::DidStopLoading() {
  if (render_view_.get()) {
    // TODO(darin): Make is_loading_ be a counter!
    render_view_->DidStopLoading();
  }
}

bool WebPluginImpl::IsOffTheRecord() {
  return false;
}

bool WebPluginImpl::ReinitializePluginForResponse(
    WebURLLoader* loader) {
  WebFrame* webframe = webframe_;
  if (!webframe)
    return false;

  WebView* webview = webframe->view();
  if (!webview)
    return false;

  WebPluginContainer* container_widget = container_;

  // Destroy the current plugin instance.
  TearDownPluginInstance(loader);

  container_ = container_widget;
  webframe_ = webframe;

  WebPluginDelegateProxy* plugin_delegate = new WebPluginDelegateProxy(
      this, mime_type_, render_view_, render_frame_);

  bool ok = plugin_delegate && plugin_delegate->Initialize(
      plugin_url_, arg_names_, arg_values_, load_manually_);

  if (!ok) {
    container_ = NULL;
    // TODO(iyengar) Should we delete the current plugin instance here?
    return false;
  }

  delegate_ = plugin_delegate;

  // Force a geometry update to occur to ensure that the plugin becomes
  // visible.
  container_->reportGeometry();

  // The plugin move sequences accumulated via DidMove are sent to the browser
  // whenever the renderer paints. Force a paint here to ensure that changes
  // to the plugin window are propagated to the browser.
  container_->invalidate();
  return true;
}

void WebPluginImpl::TearDownPluginInstance(
    WebURLLoader* loader_to_ignore) {
  // JavaScript garbage collection may cause plugin script object references to
  // be retained long after the plugin is destroyed. Some plugins won't cope
  // with their objects being released after they've been destroyed, and once
  // we've actually unloaded the plugin the object's releaseobject() code may
  // no longer be in memory. The container tracks the plugin's objects and lets
  // us invalidate them, releasing the references to them held by the JavaScript
  // runtime.
  if (container_) {
    container_->setWebLayer(NULL);
  }

  // Call PluginDestroyed() first to prevent the plugin from calling us back
  // in the middle of tearing down the render tree.
  if (delegate_) {
    // The plugin may call into the browser and pass script objects even during
    // teardown, so temporarily re-enable plugin script objects.
    DCHECK(container_);

    delegate_->PluginDestroyed();
    delegate_ = NULL;
  }

  // This needs to be called now and not in the destructor since the
  // webframe_ might not be valid anymore.
  webframe_ = NULL;
}

void WebPluginImpl::SetReferrer(blink::WebURLRequest* request,
                                ReferrerValue referrer_flag) {
  switch (referrer_flag) {
    case DOCUMENT_URL:
      webframe_->setReferrerForRequest(*request, GURL());
      break;

    case PLUGIN_SRC:
      webframe_->setReferrerForRequest(*request, plugin_url_);
      break;

    default:
      break;
  }
}

}  // namespace content
