// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "google_apis/gaia/gaia_auth_util.h"

#include <stddef.h>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "google_apis/gaia/gaia_urls.h"
#include "url/gurl.h"

namespace gaia {

namespace {

const char kGmailDomain[] = "gmail.com";
const char kGooglemailDomain[] = "googlemail.com";

std::string CanonicalizeEmailImpl(const std::string& email_address,
                                  bool change_googlemail_to_gmail) {
  std::vector<std::string> parts = base::SplitString(
      email_address, "@", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() != 2U) {
    NOTREACHED() << "expecting exactly one @, but got "
                 << (parts.empty() ? 0 : parts.size() - 1)
                 << " : " << email_address;
  } else {
    if (change_googlemail_to_gmail && parts[1] == kGooglemailDomain)
      parts[1] = kGmailDomain;

    if (parts[1] == kGmailDomain)  // only strip '.' for gmail accounts.
      base::RemoveChars(parts[0], ".", &parts[0]);
  }

  std::string new_email = base::ToLowerASCII(base::JoinString(parts, "@"));
  VLOG(1) << "Canonicalized " << email_address << " to " << new_email;
  return new_email;
}

}  // namespace


ListedAccount::ListedAccount() {}

ListedAccount::ListedAccount(const ListedAccount& other) = default;

ListedAccount::~ListedAccount() {}

bool ListedAccount::operator==(const ListedAccount& other) const {
  // Only use ids for comparison if they've been computed by some caller, since
  // this class does not assign the id.
  if (!id.empty() && !other.id.empty()) {
    return id == other.id;
  } else {
    return email == other.email &&
           gaia_id == other.gaia_id &&
           valid == other.valid &&
           raw_email == other.raw_email;
  }
}

std::string CanonicalizeEmail(const std::string& email_address) {
  // CanonicalizeEmail() is called to process email strings that are eventually
  // shown to the user, and may also be used in persisting email strings.  To
  // avoid breaking this existing behavior, this function will not try to
  // change googlemail to gmail.
  return CanonicalizeEmailImpl(email_address, false);
}

std::string CanonicalizeDomain(const std::string& domain) {
  // Canonicalization of domain names means lower-casing them. Make sure to
  // update this function in sync with Canonicalize if this ever changes.
  return base::ToLowerASCII(domain);
}

std::string SanitizeEmail(const std::string& email_address) {
  std::string sanitized(email_address);

  // Apply a default domain if necessary.
  if (sanitized.find('@') == std::string::npos) {
    sanitized += '@';
    sanitized += kGmailDomain;
  }

  return sanitized;
}

bool AreEmailsSame(const std::string& email1, const std::string& email2) {
  return CanonicalizeEmailImpl(gaia::SanitizeEmail(email1), true) ==
      CanonicalizeEmailImpl(gaia::SanitizeEmail(email2), true);
}

std::string ExtractDomainName(const std::string& email_address) {
  // First canonicalize which will also verify we have proper domain part.
  std::string email = CanonicalizeEmail(email_address);
  size_t separator_pos = email.find('@');
  if (separator_pos != email.npos && separator_pos < email.length() - 1)
    return email.substr(separator_pos + 1);
  else
    NOTREACHED() << "Not a proper email address: " << email;
  return std::string();
}

bool IsGaiaSignonRealm(const GURL& url) {
  if (!url.SchemeIsCryptographic())
    return false;

  return url == GaiaUrls::GetInstance()->gaia_url();
}


bool ParseListAccountsData(
    const std::string& data, std::vector<ListedAccount>* accounts) {
  accounts->clear();

  // Parse returned data and make sure we have data.
  scoped_ptr<base::Value> value = base::JSONReader::Read(data);
  if (!value)
    return false;

  base::ListValue* list;
  if (!value->GetAsList(&list) || list->GetSize() < 2)
    return false;

  // Get list of account info.
  base::ListValue* account_list;
  if (!list->GetList(1, &account_list) || accounts == NULL)
    return false;

  // Build a vector of accounts from the cookie.  Order is important: the first
  // account in the list is the primary account.
  for (size_t i = 0; i < account_list->GetSize(); ++i) {
    base::ListValue* account;
    if (account_list->GetList(i, &account) && account != NULL) {
      std::string email;
      // Canonicalize the email since ListAccounts returns "display email".
      if (account->GetString(3, &email) && !email.empty()) {
        // New version if ListAccounts indicates whether the email's session
        // is still valid or not.  If this value is present and false, assume
        // its invalid.  Otherwise assume it's valid to remain compatible with
        // old version.
        int is_email_valid = 1;
        if (!account->GetInteger(9, &is_email_valid))
          is_email_valid = 1;

        std::string gaia_id;
        // ListAccounts must also return the Gaia Id.
        if (account->GetString(10, &gaia_id) && !gaia_id.empty()) {
          ListedAccount listed_account;
          listed_account.email = CanonicalizeEmail(email);
          listed_account.gaia_id = gaia_id;
          listed_account.valid = is_email_valid != 0;
          listed_account.raw_email = email;
          accounts->push_back(listed_account);
        }
      }
    }
  }

  return true;
}

}  // namespace gaia
