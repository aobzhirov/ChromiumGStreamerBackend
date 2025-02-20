// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_SETTINGS_PRIVATE_PREFS_UTIL_H_
#define CHROME_BROWSER_EXTENSIONS_API_SETTINGS_PRIVATE_PREFS_UTIL_H_

#include <map>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "build/build_config.h"
#include "chrome/common/extensions/api/settings_private.h"

class PrefService;
class Profile;

namespace extensions {

class PrefsUtil {

 public:
  // Success or error statuses from calling SetPref.
  enum SetPrefResult {
    SUCCESS,
    PREF_NOT_MODIFIABLE,
    PREF_NOT_FOUND,
    PREF_TYPE_MISMATCH,
    PREF_TYPE_UNSUPPORTED
  };

  // TODO(dbeam): why is the key a std::string rather than const char*?
  using TypedPrefMap = std::map<std::string, api::settings_private::PrefType>;

  explicit PrefsUtil(Profile* profile);
  virtual ~PrefsUtil();

  // Gets the list of whitelisted pref keys -- that is, those which correspond
  // to prefs that clients of the settingsPrivate API may retrieve and
  // manipulate.
  const TypedPrefMap& GetWhitelistedKeys();

  // Gets the value of the pref with the given |name|. Returns a pointer to an
  // empty PrefObject if no pref is found for |name|.
  virtual scoped_ptr<api::settings_private::PrefObject> GetPref(
      const std::string& name);

  // Sets the pref with the given name and value in the proper PrefService.
  virtual SetPrefResult SetPref(const std::string& name,
                                const base::Value* value);

  // Appends the given |value| to the list setting specified by the path in
  // |pref_name|.
  virtual bool AppendToListCrosSetting(const std::string& pref_name,
                                       const base::Value& value);

  // Removes the given |value| from the list setting specified by the path in
  // |pref_name|.
  virtual bool RemoveFromListCrosSetting(const std::string& pref_name,
                                         const base::Value& value);

  // Returns a pointer to the appropriate PrefService instance for the given
  // |pref_name|.
  virtual PrefService* FindServiceForPref(const std::string& pref_name);

  // Returns whether or not the given pref is a CrOS-specific setting.
  virtual bool IsCrosSetting(const std::string& pref_name);

 protected:
  // Returns whether |pref_name| corresponds to a pref whose type is URL.
  bool IsPrefTypeURL(const std::string& pref_name);

#if defined(OS_CHROMEOS)
  // Returns whether |pref_name| corresponds to a pref that is enterprise
  // managed.
  bool IsPrefEnterpriseManaged(const std::string& pref_name);

  // Returns whether |pref_name| corresponds to a pref that is controlled by
  // the owner, and |profile_| is not the owner profile.
  bool IsPrefOwnerControlled(const std::string& pref_name);

  // Returns whether |pref_name| corresponds to a pref that is controlled by
  // the primary user, and |profile_| is not the primary profile.
  bool IsPrefPrimaryUserControlled(const std::string& pref_name);
#endif

  // Returns whether |pref_name| corresponds to a pref that is controlled by
  // a supervisor, and |profile_| is supervised.
  bool IsPrefSupervisorControlled(const std::string& pref_name);

  // Returns whether |pref_name| corresponds to a pref that is user modifiable
  // (i.e., not made restricted by a user or device policy).
  bool IsPrefUserModifiable(const std::string& pref_name);

  api::settings_private::PrefType GetType(const std::string& name,
                                          base::Value::Type type);

  scoped_ptr<api::settings_private::PrefObject> GetCrosSettingsPref(
      const std::string& name);

  SetPrefResult SetCrosSettingsPref(const std::string& name,
                                    const base::Value* value);

  Profile* profile_;  // weak
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_SETTINGS_PRIVATE_PREFS_UTIL_H_
