// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/sync/ios_chrome_profile_sync_service_factory.h"

#include <utility>

#include "base/memory/singleton.h"
#include "base/time/time.h"
#include "components/browser_sync/browser/profile_sync_service.h"
#include "components/keyed_service/ios/browser_state_dependency_manager.h"
#include "components/network_time/network_time_tracker.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/sync_driver/signin_manager_wrapper.h"
#include "components/sync_driver/startup_controller.h"
#include "components/sync_driver/sync_util.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/autofill/personal_data_manager_factory.h"
#include "ios/chrome/browser/bookmarks/bookmark_model_factory.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/favicon/favicon_service_factory.h"
#include "ios/chrome/browser/history/history_service_factory.h"
#include "ios/chrome/browser/invalidation/ios_chrome_profile_invalidation_provider_factory.h"
#include "ios/chrome/browser/passwords/ios_chrome_password_store_factory.h"
#include "ios/chrome/browser/search_engines/template_url_service_factory.h"
#include "ios/chrome/browser/services/gcm/ios_chrome_gcm_profile_service_factory.h"
#include "ios/chrome/browser/sessions/ios_chrome_tab_restore_service_factory.h"
#include "ios/chrome/browser/signin/about_signin_internals_factory.h"
#include "ios/chrome/browser/signin/oauth2_token_service_factory.h"
#include "ios/chrome/browser/signin/signin_client_factory.h"
#include "ios/chrome/browser/signin/signin_manager_factory.h"
#include "ios/chrome/browser/sync/ios_chrome_sync_client.h"
#include "ios/chrome/browser/web_data_service_factory.h"
#include "ios/chrome/common/channel_info.h"
#include "ios/web/public/web_thread.h"
#include "url/gurl.h"

namespace {

void UpdateNetworkTimeOnUIThread(base::Time network_time,
                                 base::TimeDelta resolution,
                                 base::TimeDelta latency,
                                 base::TimeTicks post_time) {
  GetApplicationContext()->GetNetworkTimeTracker()->UpdateNetworkTime(
      network_time, resolution, latency, post_time);
}

void UpdateNetworkTime(const base::Time& network_time,
                       const base::TimeDelta& resolution,
                       const base::TimeDelta& latency) {
  web::WebThread::PostTask(
      web::WebThread::UI, FROM_HERE,
      base::Bind(&UpdateNetworkTimeOnUIThread, network_time, resolution,
                 latency, base::TimeTicks::Now()));
}

}  // namespace

// static
IOSChromeProfileSyncServiceFactory*
IOSChromeProfileSyncServiceFactory::GetInstance() {
  return base::Singleton<IOSChromeProfileSyncServiceFactory>::get();
}

// static
ProfileSyncService* IOSChromeProfileSyncServiceFactory::GetForBrowserState(
    ios::ChromeBrowserState* browser_state) {
  if (!ProfileSyncService::IsSyncAllowedByFlag())
    return nullptr;

  return static_cast<ProfileSyncService*>(
      GetInstance()->GetServiceForBrowserState(browser_state, true));
}

// static
ProfileSyncService*
IOSChromeProfileSyncServiceFactory::GetForBrowserStateIfExists(
    ios::ChromeBrowserState* browser_state) {
  if (!ProfileSyncService::IsSyncAllowedByFlag())
    return nullptr;

  return static_cast<ProfileSyncService*>(
      GetInstance()->GetServiceForBrowserState(browser_state, false));
}

IOSChromeProfileSyncServiceFactory::IOSChromeProfileSyncServiceFactory()
    : BrowserStateKeyedServiceFactory(
          "ProfileSyncService",
          BrowserStateDependencyManager::GetInstance()) {
  // The ProfileSyncService depends on various SyncableServices being around
  // when it is shut down.  Specify those dependencies here to build the proper
  // destruction order.
  DependsOn(ios::AboutSigninInternalsFactory::GetInstance());
  DependsOn(PersonalDataManagerFactory::GetInstance());
  DependsOn(ios::BookmarkModelFactory::GetInstance());
  DependsOn(SigninClientFactory::GetInstance());
  DependsOn(ios::HistoryServiceFactory::GetInstance());
  DependsOn(IOSChromeProfileInvalidationProviderFactory::GetInstance());
  DependsOn(OAuth2TokenServiceFactory::GetInstance());
  DependsOn(IOSChromePasswordStoreFactory::GetInstance());
  DependsOn(ios::SigninManagerFactory::GetInstance());
  DependsOn(ios::TemplateURLServiceFactory::GetInstance());
  DependsOn(ios::WebDataServiceFactory::GetInstance());
  DependsOn(ios::FaviconServiceFactory::GetInstance());
}

IOSChromeProfileSyncServiceFactory::~IOSChromeProfileSyncServiceFactory() {}

scoped_ptr<KeyedService>
IOSChromeProfileSyncServiceFactory::BuildServiceInstanceFor(
    web::BrowserState* context) const {
  ios::ChromeBrowserState* browser_state =
      ios::ChromeBrowserState::FromBrowserState(context);

  SigninManagerBase* signin =
      ios::SigninManagerFactory::GetForBrowserState(browser_state);

  // Always create the GCMProfileService instance such that we can listen to
  // the profile notifications and purge the GCM store when the profile is
  // being signed out.
  IOSChromeGCMProfileServiceFactory::GetForBrowserState(browser_state);

  // TODO(crbug.com/171406): Change AboutSigninInternalsFactory to load on
  // startup once bug has been fixed.
  ios::AboutSigninInternalsFactory::GetForBrowserState(browser_state);

  ProfileSyncService::InitParams init_params;
  init_params.signin_wrapper =
      make_scoped_ptr(new SigninManagerWrapper(signin));
  init_params.oauth2_token_service =
      OAuth2TokenServiceFactory::GetForBrowserState(browser_state);
  init_params.start_behavior = ProfileSyncService::MANUAL_START;
  init_params.sync_client =
      make_scoped_ptr(new IOSChromeSyncClient(browser_state));
  init_params.network_time_update_callback = base::Bind(&UpdateNetworkTime);
  init_params.base_directory = browser_state->GetStatePath();
  init_params.url_request_context = browser_state->GetRequestContext();
  init_params.debug_identifier = browser_state->GetDebugName();
  init_params.channel = ::GetChannel();
  init_params.db_thread =
      web::WebThread::GetTaskRunnerForThread(web::WebThread::DB);
  init_params.file_thread =
      web::WebThread::GetTaskRunnerForThread(web::WebThread::FILE);
  init_params.blocking_pool = web::WebThread::GetBlockingPool();

  auto pss = make_scoped_ptr(new ProfileSyncService(std::move(init_params)));

  // Will also initialize the sync client.
  pss->Initialize();
  return std::move(pss);
}
