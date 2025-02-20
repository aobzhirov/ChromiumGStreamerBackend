// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_STORAGE_MANAGED_VALUE_STORE_CACHE_H_
#define CHROME_BROWSER_EXTENSIONS_API_STORAGE_MANAGED_VALUE_STORE_CACHE_H_

#include <map>
#include <string>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "components/policy/core/common/policy_service.h"
#include "extensions/browser/api/storage/settings_observer.h"
#include "extensions/browser/api/storage/value_store_cache.h"

class Profile;

namespace content {
class BrowserContext;
}

namespace policy {
class PolicyMap;
}

namespace extensions {

class PolicyValueStore;
class ValueStoreFactory;

// A ValueStoreCache that manages a PolicyValueStore for each extension that
// uses the storage.managed namespace. This class observes policy changes and
// which extensions listen for storage.onChanged(), and sends the appropriate
// updates to the corresponding PolicyValueStore on the FILE thread.
class ManagedValueStoreCache : public ValueStoreCache,
                               public policy::PolicyService::Observer {
 public:
  // |factory| is used to create databases for the PolicyValueStores.
  // |observers| is the list of SettingsObservers to notify when a ValueStore
  // changes.
  ManagedValueStoreCache(content::BrowserContext* context,
                         const scoped_refptr<ValueStoreFactory>& factory,
                         const scoped_refptr<SettingsObserverList>& observers);
  ~ManagedValueStoreCache() override;

 private:
  class ExtensionTracker;

  // Maps an extension ID to its PolicyValueStoreMap.
  typedef std::map<std::string, linked_ptr<PolicyValueStore> >
      PolicyValueStoreMap;

  // ValueStoreCache implementation:
  void ShutdownOnUI() override;
  void RunWithValueStoreForExtension(
      const StorageCallback& callback,
      scoped_refptr<const Extension> extension) override;
  void DeleteStorageSoon(const std::string& extension_id) override;

  // PolicyService::Observer implementation:
  void OnPolicyServiceInitialized(policy::PolicyDomain domain) override;
  void OnPolicyUpdated(const policy::PolicyNamespace& ns,
                       const policy::PolicyMap& previous,
                       const policy::PolicyMap& current) override;

  // Posted by OnPolicyUpdated() to update a PolicyValueStore on the FILE
  // thread.
  void UpdatePolicyOnFILE(const std::string& extension_id,
                          scoped_ptr<policy::PolicyMap> current_policy);

  // Returns an existing PolicyValueStore for |extension_id|, or NULL.
  PolicyValueStore* GetStoreFor(const std::string& extension_id);

  // Returns true if a backing store has been created for |extension_id|.
  bool HasStore(const std::string& extension_id) const;

  // The profile that owns the extension system being used. This is used to
  // get the PolicyService, the EventRouter and the ExtensionService.
  Profile* profile_;

  // The |profile_|'s PolicyService.
  policy::PolicyService* policy_service_;

  // Observes extension loading and unloading, and keeps the Profile's
  // PolicyService aware of the current list of extensions.
  scoped_ptr<ExtensionTracker> extension_tracker_;

  // These live on the FILE thread.
  scoped_refptr<ValueStoreFactory> storage_factory_;
  scoped_refptr<SettingsObserverList> observers_;

  // All the PolicyValueStores live on the FILE thread, and |store_map_| can be
  // accessed only on the FILE thread as well.
  PolicyValueStoreMap store_map_;

  DISALLOW_COPY_AND_ASSIGN(ManagedValueStoreCache);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_STORAGE_MANAGED_VALUE_STORE_CACHE_H_
