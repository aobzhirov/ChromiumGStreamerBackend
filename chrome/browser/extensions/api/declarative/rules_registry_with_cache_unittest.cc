// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/declarative/rules_registry.h"

// Here we test the TestRulesRegistry which is the simplest possible
// implementation of RulesRegistryWithCache as a proxy for
// RulesRegistryWithCache.

#include "base/command_line.h"
#include "base/run_loop.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/test_extension_environment.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/common/extensions/extension_test_util.h"
#include "chrome/common/extensions/features/feature_channel.h"
#include "chrome/test/base/testing_profile.h"
#include "components/version_info/version_info.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "extensions/browser/api/declarative/rules_cache_delegate.h"
#include "extensions/browser/api/declarative/rules_registry_service.h"
#include "extensions/browser/api/declarative/test_rules_registry.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/value_store/testing_value_store.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/permissions/permissions_data.h"
#include "testing/gtest/include/gtest/gtest.h"

using extension_test_util::LoadManifestUnchecked;

namespace {
const char kRuleId[] = "rule";
const char kRule2Id[] = "rule2";
}

namespace extensions {
const int kRulesRegistryID = RulesRegistryService::kDefaultRulesRegistryID;

class RulesRegistryWithCacheTest : public testing::Test {
 public:
  RulesRegistryWithCacheTest()
      : cache_delegate_(/*log_storage_init_delay=*/false),
        registry_(new TestRulesRegistry(profile(),
                                        /*event_name=*/"",
                                        content::BrowserThread::UI,
                                        &cache_delegate_,
                                        kRulesRegistryID)) {}

  void SetUp() override {
    // Note that env_.MakeExtension below also forces the creation of
    // ExtensionService.
    base::DictionaryValue manifest_extra;
    std::string key;
    CHECK(Extension::ProducePEM("test extension 1", &key));
    manifest_extra.SetString(manifest_keys::kPublicKey, key);
    extension1_ = env_.MakeExtension(manifest_extra);
    CHECK(extension1_.get());

    // Different "key" values for the two extensions ensure a different ID.
    CHECK(Extension::ProducePEM("test extension 2", &key));
    manifest_extra.SetString(manifest_keys::kPublicKey, key);
    extension2_ = env_.MakeExtension(manifest_extra);
    CHECK(extension2_.get());
    CHECK_NE(extension2_->id(), extension1_->id());
  }

  ~RulesRegistryWithCacheTest() override {}

  std::string AddRule(const std::string& extension_id,
                      const std::string& rule_id,
                      TestRulesRegistry* registry) {
    std::vector<linked_ptr<api::events::Rule>> add_rules;
    add_rules.push_back(make_linked_ptr(new api::events::Rule));
    add_rules[0]->id.reset(new std::string(rule_id));
    return registry->AddRules(extension_id, add_rules);
  }

  std::string AddRule(const std::string& extension_id,
                      const std::string& rule_id) {
    return AddRule(extension_id, rule_id, registry_.get());
  }

  std::string RemoveRule(const std::string& extension_id,
                         const std::string& rule_id) {
    std::vector<std::string> remove_rules;
    remove_rules.push_back(rule_id);
    return registry_->RemoveRules(extension_id, remove_rules);
  }

  int GetNumberOfRules(const std::string& extension_id,
                       TestRulesRegistry* registry) {
    std::vector<linked_ptr<api::events::Rule>> get_rules;
    registry->GetAllRules(extension_id, &get_rules);
    return get_rules.size();
  }

  int GetNumberOfRules(const std::string& extension_id) {
    return GetNumberOfRules(extension_id, registry_.get());
  }

  TestingProfile* profile() const { return env_.profile(); }

 protected:
  TestExtensionEnvironment env_;
  RulesCacheDelegate cache_delegate_;
  scoped_refptr<TestRulesRegistry> registry_;
  scoped_refptr<const Extension> extension1_;
  scoped_refptr<const Extension> extension2_;
};

TEST_F(RulesRegistryWithCacheTest, AddRules) {
  // Check that nothing happens if the concrete RulesRegistry refuses to insert
  // the rules.
  registry_->SetResult("Error");
  EXPECT_EQ("Error", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ(0, GetNumberOfRules(extension1_->id()));
  registry_->SetResult(std::string());

  // Check that rules can be inserted.
  EXPECT_EQ("", AddRule(extension1_->id(), kRule2Id));
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id()));

  // Check that rules cannot be inserted twice with the same kRuleId.
  EXPECT_NE("", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id()));

  // Check that different extensions may use the same kRuleId.
  EXPECT_EQ("", AddRule(extension2_->id(), kRuleId));
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id()));
  EXPECT_EQ(1, GetNumberOfRules(extension2_->id()));
}

TEST_F(RulesRegistryWithCacheTest, RemoveRules) {
  // Prime registry.
  EXPECT_EQ("", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ("", AddRule(extension2_->id(), kRuleId));
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id()));
  EXPECT_EQ(1, GetNumberOfRules(extension2_->id()));

  // Check that nothing happens if the concrete RuleRegistry refuses to remove
  // the rules.
  registry_->SetResult("Error");
  EXPECT_EQ("Error", RemoveRule(extension1_->id(), kRuleId));
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id()));
  registry_->SetResult(std::string());

  // Check that nothing happens if a rule does not exist.
  EXPECT_EQ("", RemoveRule(extension1_->id(), "unknown_rule"));
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id()));

  // Check that rules may be removed and only for the correct extension.
  EXPECT_EQ("", RemoveRule(extension1_->id(), kRuleId));
  EXPECT_EQ(0, GetNumberOfRules(extension1_->id()));
  EXPECT_EQ(1, GetNumberOfRules(extension2_->id()));
}

TEST_F(RulesRegistryWithCacheTest, RemoveAllRules) {
  // Prime registry.
  EXPECT_EQ("", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ("", AddRule(extension1_->id(), kRule2Id));
  EXPECT_EQ("", AddRule(extension2_->id(), kRuleId));
  EXPECT_EQ(2, GetNumberOfRules(extension1_->id()));
  EXPECT_EQ(1, GetNumberOfRules(extension2_->id()));

  // Check that nothing happens if the concrete RuleRegistry refuses to remove
  // the rules.
  registry_->SetResult("Error");
  EXPECT_EQ("Error", registry_->RemoveAllRules(extension1_->id()));
  EXPECT_EQ(2, GetNumberOfRules(extension1_->id()));
  registry_->SetResult(std::string());

  // Check that rules may be removed and only for the correct extension.
  EXPECT_EQ("", registry_->RemoveAllRules(extension1_->id()));
  EXPECT_EQ(0, GetNumberOfRules(extension1_->id()));
  EXPECT_EQ(1, GetNumberOfRules(extension2_->id()));
}

TEST_F(RulesRegistryWithCacheTest, GetRules) {
  // Prime registry.
  EXPECT_EQ("", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ("", AddRule(extension1_->id(), kRule2Id));
  EXPECT_EQ("", AddRule(extension2_->id(), kRuleId));

  // Check that we get the correct rule and unknown rules are ignored.
  std::vector<std::string> rules_to_get;
  rules_to_get.push_back(kRuleId);
  rules_to_get.push_back("unknown_rule");
  std::vector<linked_ptr<api::events::Rule>> gotten_rules;
  registry_->GetRules(extension1_->id(), rules_to_get, &gotten_rules);
  ASSERT_EQ(1u, gotten_rules.size());
  ASSERT_TRUE(gotten_rules[0]->id.get());
  EXPECT_EQ(kRuleId, *(gotten_rules[0]->id));
}

TEST_F(RulesRegistryWithCacheTest, GetAllRules) {
  // Prime registry.
  EXPECT_EQ("", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ("", AddRule(extension1_->id(), kRule2Id));
  EXPECT_EQ("", AddRule(extension2_->id(), kRuleId));

  // Check that we get the correct rules.
  std::vector<linked_ptr<api::events::Rule>> gotten_rules;
  registry_->GetAllRules(extension1_->id(), &gotten_rules);
  EXPECT_EQ(2u, gotten_rules.size());
  ASSERT_TRUE(gotten_rules[0]->id.get());
  ASSERT_TRUE(gotten_rules[1]->id.get());
  EXPECT_TRUE((kRuleId == *(gotten_rules[0]->id) &&
               kRule2Id == *(gotten_rules[1]->id)) ||
              (kRuleId == *(gotten_rules[1]->id) &&
               kRule2Id == *(gotten_rules[0]->id)) );
}

TEST_F(RulesRegistryWithCacheTest, OnExtensionUninstalled) {
  // Prime registry.
  EXPECT_EQ("", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ("", AddRule(extension2_->id(), kRuleId));

  // Check that the correct rules are removed.
  registry_->OnExtensionUninstalled(extension1_.get());
  EXPECT_EQ(0, GetNumberOfRules(extension1_->id()));
  EXPECT_EQ(1, GetNumberOfRules(extension2_->id()));
}

TEST_F(RulesRegistryWithCacheTest, DeclarativeRulesStored) {
  ExtensionPrefs* extension_prefs = env_.GetExtensionPrefs();

  const std::string event_name("testEvent");
  const std::string rules_stored_key(
      RulesCacheDelegate::GetRulesStoredKey(
          event_name, profile()->IsOffTheRecord()));
  scoped_ptr<RulesCacheDelegate> cache_delegate(new RulesCacheDelegate(false));
  scoped_refptr<RulesRegistry> registry(
      new TestRulesRegistry(profile(), event_name, content::BrowserThread::UI,
                            cache_delegate.get(), kRulesRegistryID));

  // 1. Test the handling of preferences.
  // Default value is always true.
  EXPECT_TRUE(cache_delegate->GetDeclarativeRulesStored(extension1_->id()));

  extension_prefs->UpdateExtensionPref(
      extension1_->id(), rules_stored_key, new base::FundamentalValue(false));
  EXPECT_FALSE(cache_delegate->GetDeclarativeRulesStored(extension1_->id()));

  extension_prefs->UpdateExtensionPref(
      extension1_->id(), rules_stored_key, new base::FundamentalValue(true));
  EXPECT_TRUE(cache_delegate->GetDeclarativeRulesStored(extension1_->id()));

  // 2. Test writing behavior.
  // The ValueStore is lazily created when reading/writing. None done yet so
  // verify that not yet created.
  TestingValueStore* store = env_.GetExtensionSystem()->value_store();
  // No write yet so no store should have been created.
  ASSERT_FALSE(store);

  scoped_ptr<base::ListValue> value(new base::ListValue);
  value->AppendBoolean(true);
  cache_delegate->WriteToStorage(extension1_->id(), std::move(value));
  EXPECT_TRUE(cache_delegate->GetDeclarativeRulesStored(extension1_->id()));
  base::RunLoop().RunUntilIdle();
  store = env_.GetExtensionSystem()->value_store();
  ASSERT_TRUE(store);
  EXPECT_EQ(1, store->write_count());
  int write_count = store->write_count();

  value.reset(new base::ListValue);
  cache_delegate->WriteToStorage(extension1_->id(), std::move(value));
  EXPECT_FALSE(cache_delegate->GetDeclarativeRulesStored(extension1_->id()));
  base::RunLoop().RunUntilIdle();
  // No rules currently, but previously there were, so we expect a write.
  EXPECT_EQ(write_count + 1, store->write_count());
  write_count = store->write_count();

  value.reset(new base::ListValue);
  cache_delegate->WriteToStorage(extension1_->id(), std::move(value));
  EXPECT_FALSE(cache_delegate->GetDeclarativeRulesStored(extension1_->id()));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(write_count, store->write_count());

  // 3. Test reading behavior.
  int read_count = store->read_count();

  cache_delegate->SetDeclarativeRulesStored(extension1_->id(), false);
  cache_delegate->ReadFromStorage(extension1_->id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(read_count, store->read_count());
  read_count = store->read_count();

  cache_delegate->SetDeclarativeRulesStored(extension1_->id(), true);
  cache_delegate->ReadFromStorage(extension1_->id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(read_count + 1, store->read_count());
}

// Test that each registry has its own "are some rules stored" flag.
TEST_F(RulesRegistryWithCacheTest, RulesStoredFlagMultipleRegistries) {
  ExtensionPrefs* extension_prefs = env_.GetExtensionPrefs();

  const std::string event_name1("testEvent1");
  const std::string event_name2("testEvent2");
  const std::string rules_stored_key1(
      RulesCacheDelegate::GetRulesStoredKey(
          event_name1, profile()->IsOffTheRecord()));
  const std::string rules_stored_key2(
      RulesCacheDelegate::GetRulesStoredKey(
          event_name2, profile()->IsOffTheRecord()));
  scoped_ptr<RulesCacheDelegate> cache_delegate1(new RulesCacheDelegate(false));
  scoped_refptr<RulesRegistry> registry1(
      new TestRulesRegistry(profile(), event_name1, content::BrowserThread::UI,
                            cache_delegate1.get(), kRulesRegistryID));

  scoped_ptr<RulesCacheDelegate> cache_delegate2(new RulesCacheDelegate(false));
  scoped_refptr<RulesRegistry> registry2(
      new TestRulesRegistry(profile(), event_name2, content::BrowserThread::UI,
                            cache_delegate2.get(), kRulesRegistryID));

  // Checkt the correct default values.
  EXPECT_TRUE(cache_delegate1->GetDeclarativeRulesStored(extension1_->id()));
  EXPECT_TRUE(cache_delegate2->GetDeclarativeRulesStored(extension1_->id()));

  // Update the flag for the first registry.
  extension_prefs->UpdateExtensionPref(
      extension1_->id(), rules_stored_key1, new base::FundamentalValue(false));
  EXPECT_FALSE(cache_delegate1->GetDeclarativeRulesStored(extension1_->id()));
  EXPECT_TRUE(cache_delegate2->GetDeclarativeRulesStored(extension1_->id()));
}

TEST_F(RulesRegistryWithCacheTest, RulesPreservedAcrossRestart) {
  // This test makes sure that rules are restored from the rule store
  // on registry (in particular, browser) restart.

  // TODO(vabr): Once some API using declarative rules enters the stable
  // channel, make sure to use that API here, and remove |channel|.
  ScopedCurrentChannel channel(version_info::Channel::UNKNOWN);

  ExtensionService* extension_service = env_.GetExtensionService();

  // 1. Add an extension, before rules registry gets created.
  std::string error;
  scoped_refptr<Extension> extension(
      LoadManifestUnchecked("permissions",
                            "web_request_all_host_permissions.json",
                            Manifest::INVALID_LOCATION,
                            Extension::NO_FLAGS,
                            extension1_->id(),
                            &error));
  ASSERT_TRUE(error.empty());
  extension_service->AddExtension(extension.get());
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(env_.profile())
                  ->enabled_extensions()
                  .Contains(extension->id()));
  EXPECT_TRUE(extension->permissions_data()->HasAPIPermission(
      APIPermission::kDeclarativeWebRequest));
  env_.GetExtensionSystem()->SetReady();

  // 2. First run, adding a rule for the extension.
  scoped_ptr<RulesCacheDelegate> cache_delegate(new RulesCacheDelegate(false));
  scoped_refptr<TestRulesRegistry> registry(
      new TestRulesRegistry(profile(), "testEvent", content::BrowserThread::UI,
                            cache_delegate.get(), kRulesRegistryID));

  AddRule(extension1_->id(), kRuleId, registry.get());
  base::RunLoop().RunUntilIdle();  // Posted tasks store the added rule.
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id(), registry.get()));

  // 3. Restart the TestRulesRegistry and see the rule still there.
  cache_delegate.reset(new RulesCacheDelegate(false));
  registry =
      new TestRulesRegistry(profile(), "testEvent", content::BrowserThread::UI,
                            cache_delegate.get(), kRulesRegistryID);

  base::RunLoop().RunUntilIdle();  // Posted tasks retrieve the stored rule.
  EXPECT_EQ(1, GetNumberOfRules(extension1_->id(), registry.get()));
}

TEST_F(RulesRegistryWithCacheTest, ConcurrentStoringOfRules) {
  // When an extension updates its rules, the new set of rules is stored to disk
  // with some delay. While it is acceptable for a quick series of updates for a
  // single extension to only write the last one, we should never forget to
  // write a rules update for extension A, just because it is immediately
  // followed by a rules update for extension B.
  extensions::TestExtensionSystem* system = env_.GetExtensionSystem();
  // ValueStore created lazily, assure none currently exists.
  ASSERT_TRUE(system->value_store() == nullptr);

  int write_count = 0;
  EXPECT_EQ("", AddRule(extension1_->id(), kRuleId));
  EXPECT_EQ("", AddRule(extension2_->id(), kRule2Id));
  env_.GetExtensionSystem()->SetReady();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(write_count + 2, system->value_store()->write_count());
}

}  //  namespace extensions
