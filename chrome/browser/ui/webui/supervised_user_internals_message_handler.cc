// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/supervised_user_internals_message_handler.h"

#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/account_tracker_service_factory.h"
#include "chrome/browser/supervised_user/child_accounts/child_account_service.h"
#include "chrome/browser/supervised_user/supervised_user_service.h"
#include "chrome/browser/supervised_user/supervised_user_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_settings_service.h"
#include "chrome/browser/supervised_user/supervised_user_settings_service_factory.h"
#include "chrome/common/channel_info.h"
#include "components/signin/core/browser/account_tracker_service.h"
#include "components/supervised_user_error_page/supervised_user_error_page.h"
#include "components/url_formatter/url_fixer.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_ui.h"

using content::BrowserThread;

namespace {

// Creates a 'section' for display on about:supervised-user-internals,
// consisting of a title and a list of fields. Returns a pointer to the new
// section's contents, for use with |AddSectionEntry| below. Note that
// |parent_list|, not the caller, owns the newly added section.
base::ListValue* AddSection(base::ListValue* parent_list,
                            const std::string& title) {
  scoped_ptr<base::DictionaryValue> section(new base::DictionaryValue);
  scoped_ptr<base::ListValue> section_contents(new base::ListValue);
  section->SetString("title", title);
  // Grab a raw pointer to the result before |Pass()|ing it on.
  base::ListValue* result = section_contents.get();
  section->Set("data", std::move(section_contents));
  parent_list->Append(std::move(section));
  return result;
}

// Adds a bool entry to a section (created with |AddSection| above).
void AddSectionEntry(base::ListValue* section_list,
                     const std::string& name,
                     bool value) {
  scoped_ptr<base::DictionaryValue> entry(new base::DictionaryValue);
  entry->SetString("stat_name", name);
  entry->SetBoolean("stat_value", value);
  entry->SetBoolean("is_valid", true);
  section_list->Append(std::move(entry));
}

// Adds a string entry to a section (created with |AddSection| above).
void AddSectionEntry(base::ListValue* section_list,
                     const std::string& name,
                     const std::string& value) {
  scoped_ptr<base::DictionaryValue> entry(new base::DictionaryValue);
  entry->SetString("stat_name", name);
  entry->SetString("stat_value", value);
  entry->SetBoolean("is_valid", true);
  section_list->Append(std::move(entry));
}

std::string FilteringBehaviorToString(
    SupervisedUserURLFilter::FilteringBehavior behavior) {
  switch (behavior) {
    case SupervisedUserURLFilter::ALLOW:
      return "Allow";
    case SupervisedUserURLFilter::WARN:
      return "Warn";
    case SupervisedUserURLFilter::BLOCK:
      return "Block";
    case SupervisedUserURLFilter::INVALID:
      return "Invalid";
  }
  return "Unknown";
}

std::string FilteringBehaviorToString(
    SupervisedUserURLFilter::FilteringBehavior behavior, bool uncertain) {
  std::string result = FilteringBehaviorToString(behavior);
  if (uncertain)
    result += " (Uncertain)";
  return result;
}

std::string FilteringBehaviorReasonToString(
    supervised_user_error_page::FilteringBehaviorReason reason) {
  switch (reason) {
    case supervised_user_error_page::DEFAULT:
      return "Default";
    case supervised_user_error_page::ASYNC_CHECKER:
      return "AsyncChecker";
    case supervised_user_error_page::BLACKLIST:
      return "Blacklist";
    case supervised_user_error_page::MANUAL:
      return "Manual";
    case supervised_user_error_page::WHITELIST:
      return "Whitelist";
  }
  return "Unknown/invalid";
}

}  // namespace

// Helper class that lives on the IO thread, listens to the
// SupervisedUserURLFilter there, and posts results back to the UI thread.
class SupervisedUserInternalsMessageHandler::IOThreadHelper
    : public base::RefCountedThreadSafe<IOThreadHelper,
                                        BrowserThread::DeleteOnIOThread>,
      public SupervisedUserURLFilter::Observer {
 public:
  using OnURLCheckedCallback =
      base::Callback<void(const GURL&,
                          SupervisedUserURLFilter::FilteringBehavior,
                          supervised_user_error_page::FilteringBehaviorReason,
                          bool uncertain)>;

  IOThreadHelper(scoped_refptr<const SupervisedUserURLFilter> filter,
                 const OnURLCheckedCallback& callback)
      : filter_(filter), callback_(callback) {
    BrowserThread::PostTask(BrowserThread::IO,
                            FROM_HERE,
                            base::Bind(&IOThreadHelper::InitOnIOThread, this));
  }

 private:
  friend class base::DeleteHelper<IOThreadHelper>;
  friend struct BrowserThread::DeleteOnThread<BrowserThread::IO>;
  virtual ~IOThreadHelper() {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    filter_->RemoveObserver(this);
  }

  // SupervisedUserURLFilter::Observer:
  void OnSiteListUpdated() override {}
  void OnURLChecked(const GURL& url,
                    SupervisedUserURLFilter::FilteringBehavior behavior,
                    supervised_user_error_page::FilteringBehaviorReason reason,
                    bool uncertain) override {
    BrowserThread::PostTask(BrowserThread::UI,
                            FROM_HERE,
                            base::Bind(callback_,
                                       url, behavior, reason, uncertain));
  }

  void InitOnIOThread() {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    filter_->AddObserver(this);
  }

  scoped_refptr<const SupervisedUserURLFilter> filter_;
  OnURLCheckedCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(IOThreadHelper);
};

SupervisedUserInternalsMessageHandler::SupervisedUserInternalsMessageHandler()
    : weak_factory_(this) {
}

SupervisedUserInternalsMessageHandler::
    ~SupervisedUserInternalsMessageHandler() {
}

void SupervisedUserInternalsMessageHandler::RegisterMessages() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  web_ui()->RegisterMessageCallback("registerForEvents",
      base::Bind(&SupervisedUserInternalsMessageHandler::
                     HandleRegisterForEvents,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback("getBasicInfo",
      base::Bind(&SupervisedUserInternalsMessageHandler::HandleGetBasicInfo,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback("tryURL",
      base::Bind(&SupervisedUserInternalsMessageHandler::HandleTryURL,
                 base::Unretained(this)));
}

void SupervisedUserInternalsMessageHandler::OnURLFilterChanged() {
  SendBasicInfo();
}

SupervisedUserService*
SupervisedUserInternalsMessageHandler::GetSupervisedUserService() {
  Profile* profile = Profile::FromWebUI(web_ui());
  return SupervisedUserServiceFactory::GetForProfile(
      profile->GetOriginalProfile());
}

void SupervisedUserInternalsMessageHandler::HandleRegisterForEvents(
    const base::ListValue* args) {
  DCHECK(args->empty());

  if (!io_thread_helper_.get()) {
    io_thread_helper_ = new IOThreadHelper(
        GetSupervisedUserService()->GetURLFilterForIOThread(),
        base::Bind(&SupervisedUserInternalsMessageHandler::OnURLChecked,
                   weak_factory_.GetWeakPtr()));
  }
}

void SupervisedUserInternalsMessageHandler::HandleGetBasicInfo(
    const base::ListValue* args) {
  SendBasicInfo();
}

void SupervisedUserInternalsMessageHandler::HandleTryURL(
    const base::ListValue* args) {
  DCHECK_EQ(1u, args->GetSize());
  std::string url_str;
  if (!args->GetString(0, &url_str))
    return;

  GURL url = url_formatter::FixupURL(url_str, std::string());
  if (!url.is_valid())
    return;

  SupervisedUserURLFilter* filter =
      GetSupervisedUserService()->GetURLFilterForUIThread();
  std::map<std::string, base::string16> whitelists =
      filter->GetMatchingWhitelistTitles(url);
  filter->GetFilteringBehaviorForURLWithAsyncChecks(
      url, base::Bind(&SupervisedUserInternalsMessageHandler::OnTryURLResult,
                      weak_factory_.GetWeakPtr(), whitelists));
}

void SupervisedUserInternalsMessageHandler::SendBasicInfo() {
  scoped_ptr<base::ListValue> section_list(new base::ListValue);

  base::ListValue* section_general = AddSection(section_list.get(), "General");
  AddSectionEntry(section_general, "Chrome version",
                  chrome::GetVersionString());
  AddSectionEntry(section_general, "Child detection enabled",
                  ChildAccountService::IsChildAccountDetectionEnabled());

  Profile* profile = Profile::FromWebUI(web_ui());

  base::ListValue* section_profile = AddSection(section_list.get(), "Profile");
  AddSectionEntry(section_profile, "Account", profile->GetProfileUserName());
  AddSectionEntry(section_profile, "Legacy Supervised",
                  profile->IsLegacySupervised());
  AddSectionEntry(section_profile, "Child", profile->IsChild());

  SupervisedUserURLFilter* filter =
      GetSupervisedUserService()->GetURLFilterForUIThread();

  base::ListValue* section_filter = AddSection(section_list.get(), "Filter");
  AddSectionEntry(section_filter, "Blacklist active", filter->HasBlacklist());
  AddSectionEntry(section_filter, "Online checks active",
                  filter->HasAsyncURLChecker());
  AddSectionEntry(section_filter, "Default behavior",
                  FilteringBehaviorToString(
                      filter->GetDefaultFilteringBehavior()));

  AccountTrackerService* account_tracker =
      AccountTrackerServiceFactory::GetForProfile(profile);

  for (const auto& account: account_tracker->GetAccounts()) {
    base::ListValue* section_user = AddSection(section_list.get(),
        "User Information for " + account.full_name);
    AddSectionEntry(section_user, "Account id", account.account_id);
    AddSectionEntry(section_user, "Gaia", account.gaia);
    AddSectionEntry(section_user, "Email", account.email);
    AddSectionEntry(section_user, "Given name", account.given_name);
    AddSectionEntry(section_user, "Hosted domain", account.hosted_domain);
    AddSectionEntry(section_user, "Locale", account.locale);
    AddSectionEntry(section_user, "Is child", account.is_child_account);
    AddSectionEntry(section_user, "Is valid", account.IsValid());
  }

  base::DictionaryValue result;
  result.Set("sections", std::move(section_list));
  web_ui()->CallJavascriptFunction(
      "chrome.supervised_user_internals.receiveBasicInfo", result);

  // Trigger retrieval of the user settings
  SupervisedUserSettingsService* settings_service =
      SupervisedUserSettingsServiceFactory::GetForProfile(profile);
  user_settings_subscription_ = settings_service->Subscribe(base::Bind(
        &SupervisedUserInternalsMessageHandler::SendSupervisedUserSettings,
        weak_factory_.GetWeakPtr()));
}

void SupervisedUserInternalsMessageHandler::SendSupervisedUserSettings(
    const base::DictionaryValue* settings) {
  web_ui()->CallJavascriptFunction(
      "chrome.supervised_user_internals.receiveUserSettings",
      *(settings ? settings : base::Value::CreateNullValue().get()));
}

void SupervisedUserInternalsMessageHandler::OnTryURLResult(
    const std::map<std::string, base::string16>& whitelists,
    SupervisedUserURLFilter::FilteringBehavior behavior,
    supervised_user_error_page::FilteringBehaviorReason reason,
    bool uncertain) {
  std::vector<std::string> whitelists_list;
  for (const auto& whitelist : whitelists) {
    whitelists_list.push_back(
        base::StringPrintf("%s: %s", whitelist.first.c_str(),
                           base::UTF16ToUTF8(whitelist.second).c_str()));
  }
  std::string whitelists_str = base::JoinString(whitelists_list, "; ");
  base::DictionaryValue result;
  result.SetString("allowResult",
                   FilteringBehaviorToString(behavior, uncertain));
  result.SetBoolean("manual", reason == supervised_user_error_page::MANUAL &&
                                  behavior == SupervisedUserURLFilter::ALLOW);
  result.SetString("whitelists", whitelists_str);
  web_ui()->CallJavascriptFunction(
      "chrome.supervised_user_internals.receiveTryURLResult", result);
}

void SupervisedUserInternalsMessageHandler::OnURLChecked(
    const GURL& url,
    SupervisedUserURLFilter::FilteringBehavior behavior,
    supervised_user_error_page::FilteringBehaviorReason reason,
    bool uncertain) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  base::DictionaryValue result;
  result.SetString("url", url.possibly_invalid_spec());
  result.SetString("result", FilteringBehaviorToString(behavior, uncertain));
  result.SetString("reason", FilteringBehaviorReasonToString(reason));
  web_ui()->CallJavascriptFunction(
      "chrome.supervised_user_internals.receiveFilteringResult", result);
}
