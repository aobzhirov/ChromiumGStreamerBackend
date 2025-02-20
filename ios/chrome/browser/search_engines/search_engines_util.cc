// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/search_engines/search_engines_util.h"

#include <stddef.h>

#include "base/message_loop/message_loop.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/search_engines_pref_names.h"
#include "components/search_engines/template_url_prepopulate_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_observer.h"

namespace {

// Id of the google id in template_url_prepopulate_data.cc.
static const int kGoogleEnginePrepopulatedId = 1;

// Update the search engine of the given service to the default ones for the
// current locale.
void UpdateSearchEngine(TemplateURLService* service) {
  DCHECK(service);
  DCHECK(service->loaded());
  std::vector<TemplateURL*> old_engines = service->GetTemplateURLs();
  size_t default_engine;
  ScopedVector<TemplateURLData> new_engines =
      TemplateURLPrepopulateData::GetPrepopulatedEngines(nullptr,
                                                         &default_engine);
  DCHECK(default_engine == 0);
  DCHECK(new_engines[0]->prepopulate_id == kGoogleEnginePrepopulatedId);
  // The aim is to replace the old search engines with the new ones.
  // It is not possible to remove all of them, because removing the current
  // selected engine is not allowed.
  // It is not possible to add all the new ones first, because the service gets
  // confused when a prepopulated engine is there more than once.
  // Instead, this will in a first pass makes google as the default engine. In
  // a second pass, it will remove all other search engine. At last, in a third
  // pass, it will add all new engine but google.
  for (const auto& engine : old_engines) {
    if (engine->prepopulate_id() == kGoogleEnginePrepopulatedId)
      service->SetUserSelectedDefaultSearchProvider(engine);
  }
  for (const auto& engine : old_engines) {
    if (engine->prepopulate_id() != kGoogleEnginePrepopulatedId)
      service->Remove(engine);
  }
  ScopedVector<TemplateURLData>::iterator it = new_engines.begin();
  while (it != new_engines.end()) {
    if ((*it)->prepopulate_id != kGoogleEnginePrepopulatedId) {
      // service->Add takes ownership on Added TemplateURL.
      service->Add(new TemplateURL(**it));
      it = new_engines.weak_erase(it);
    } else {
      ++it;
    }
  }
}

// Observer class that allows to wait for the TemplateURLService to be loaded.
// This class will delete itself as soon as the TemplateURLService is loaded.
class LoadedObserver : public TemplateURLServiceObserver {
 public:
  explicit LoadedObserver(TemplateURLService* service) : service_(service) {
    DCHECK(service_);
    DCHECK(!service_->loaded());
    service_->AddObserver(this);
    service_->Load();
  }

  ~LoadedObserver() override { service_->RemoveObserver(this); }

  void OnTemplateURLServiceChanged() override {
    service_->RemoveObserver(this);
    UpdateSearchEngine(service_);
    // Only delete this class when this callback is finished.
    base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
  }

 private:
  TemplateURLService* service_;
};

}  // namespace

namespace search_engines {

void UpdateSearchEnginesIfNeeded(PrefService* preferences,
                                 TemplateURLService* service) {
  if (!preferences->HasPrefPath(prefs::kCountryIDAtInstall)) {
    // No search engines were ever installed, just return.
    return;
  }
  int old_country_id = preferences->GetInteger(prefs::kCountryIDAtInstall);
  int country_id = TemplateURLPrepopulateData::GetCurrentCountryID();
  if (country_id == old_country_id) {
    // User's locale did not change, just return.
    return;
  }
  preferences->SetInteger(prefs::kCountryIDAtInstall, country_id);
  // If the current search engine is managed by policy then we can't set the
  // default search engine, which is required by UpdateSearchEngine(). This
  // isn't a problem as long as the default search engine is enforced via
  // policy, because it can't be changed by the user anyway; if the policy is
  // removed then the engines can be updated again.
  if (!service || service->is_default_search_managed())
    return;
  if (service->loaded())
    UpdateSearchEngine(service);
  else
    new LoadedObserver(service);  // The observer manages its own lifetime.
}

}  // namespace search_engines
