// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/web_request/chrome_extension_web_request_event_router_delegate.h"

#include "chrome/browser/extensions/activity_log/activity_action_constants.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_renderer_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/resource_request_info.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/api/activity_log/web_request_constants.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/api/web_request/web_request_api_constants.h"
#include "extensions/browser/api/web_request/web_request_event_details.h"
#include "extensions/browser/extension_registry.h"
#include "net/url_request/url_request.h"

namespace keys = extension_web_request_api_constants;

namespace {

void ExtractExtraRequestDetailsInternal(const net::URLRequest* request,
                                        int* tab_id,
                                        int* window_id) {
  const content::ResourceRequestInfo* info =
      content::ResourceRequestInfo::ForRequest(request);
  if (!info)
    return;

  ExtensionRendererState::GetInstance()->GetTabAndWindowId(info, tab_id,
                                                           window_id);
}

void NotifyWebRequestWithheldOnUI(int render_process_id,
                                  int render_frame_id,
                                  const std::string& extension_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Track down the ExtensionActionRunner and the extension. Since this is
  // asynchronous, we could hit a null anywhere along the path.
  content::RenderFrameHost* rfh =
      content::RenderFrameHost::FromID(render_process_id, render_frame_id);
  if (!rfh)
    return;
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(rfh);
  if (!web_contents)
    return;
  extensions::ExtensionActionRunner* runner =
      extensions::ExtensionActionRunner::GetForWebContents(web_contents);
  if (!runner)
    return;

  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(web_contents->GetBrowserContext())
          ->enabled_extensions()
          .GetByID(extension_id);
  if (!extension)
    return;

  runner->OnWebRequestBlocked(extension);
}

}  // namespace

ChromeExtensionWebRequestEventRouterDelegate::
    ChromeExtensionWebRequestEventRouterDelegate() {
}

ChromeExtensionWebRequestEventRouterDelegate::
    ~ChromeExtensionWebRequestEventRouterDelegate() {
}

void ChromeExtensionWebRequestEventRouterDelegate::LogExtensionActivity(
    content::BrowserContext* browser_context,
    bool is_incognito,
    const std::string& extension_id,
    const GURL& url,
    const std::string& api_call,
    scoped_ptr<base::DictionaryValue> details) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!extensions::ExtensionsBrowserClient::Get()->IsValidContext(
      browser_context)) {
    return;
  }

  scoped_refptr<extensions::Action> action =
      new extensions::Action(extension_id,
                              base::Time::Now(),
                              extensions::Action::ACTION_WEB_REQUEST,
                              api_call);
  action->set_page_url(url);
  action->set_page_incognito(is_incognito);
  action->mutable_other()->Set(activity_log_constants::kActionWebRequest,
                               details.release());
  extensions::ActivityLog::GetInstance(browser_context)->LogAction(action);
}

void ChromeExtensionWebRequestEventRouterDelegate::ExtractExtraRequestDetails(
    const net::URLRequest* request,
    extensions::WebRequestEventDetails* out) {
  int tab_id = -1;
  int window_id = -1;
  ExtractExtraRequestDetailsInternal(request, &tab_id, &window_id);
  out->SetInteger(keys::kTabIdKey, tab_id);
}

bool
ChromeExtensionWebRequestEventRouterDelegate::OnGetMatchingListenersImplCheck(
    int filter_tab_id,
    int filter_window_id,
    const net::URLRequest* request) {
  int tab_id = -1;
  int window_id = -1;
  ExtractExtraRequestDetailsInternal(request, &tab_id, &window_id);
  if (filter_tab_id != -1 && tab_id != filter_tab_id)
    return true;
  return (filter_window_id != -1 && window_id != filter_window_id);
}

void ChromeExtensionWebRequestEventRouterDelegate::NotifyWebRequestWithheld(
    int render_process_id,
    int render_frame_id,
    const std::string& extension_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::Bind(&NotifyWebRequestWithheldOnUI, render_process_id,
                 render_frame_id, extension_id));
}
