// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_NETWORK_CONFIGURATION_UPDATER_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_NETWORK_CONFIGURATION_UPDATER_H_

#include <string>

#include "base/callback_list.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/policy/network_configuration_updater.h"
#include "components/onc/onc_constants.h"

namespace base {
class DictionaryValue;
class ListValue;
class Value;
}

namespace chromeos {
class CrosSettings;
class ManagedNetworkConfigurationHandler;
class NetworkDeviceHandler;

namespace onc {
class CertificateImporter;
}
}

namespace policy {

class PolicyService;

// Implements addtional special handling of ONC device policies, which requires
// listening for notifications from CrosSettings.
class DeviceNetworkConfigurationUpdater : public NetworkConfigurationUpdater {
 public:
  ~DeviceNetworkConfigurationUpdater() override;

  // Creates an updater that applies the ONC device policy from |policy_service|
  // once the policy service is completely initialized and on each policy
  // change. The argument objects must outlive the returned updater.
  static scoped_ptr<DeviceNetworkConfigurationUpdater> CreateForDevicePolicy(
      PolicyService* policy_service,
      chromeos::ManagedNetworkConfigurationHandler* network_config_handler,
      chromeos::NetworkDeviceHandler* network_device_handler,
      chromeos::CrosSettings* cros_settings);

 private:
  DeviceNetworkConfigurationUpdater(
      PolicyService* policy_service,
      chromeos::ManagedNetworkConfigurationHandler* network_config_handler,
      chromeos::NetworkDeviceHandler* network_device_handler,
      chromeos::CrosSettings* cros_settings);

  void Init() override;
  void ImportCertificates(const base::ListValue& certificates_onc) override;
  void ApplyNetworkPolicy(
      base::ListValue* network_configs_onc,
      base::DictionaryValue* global_network_config) override;
  void OnDataRoamingSettingChanged();

  chromeos::NetworkDeviceHandler* network_device_handler_;
  chromeos::CrosSettings* cros_settings_;
  scoped_ptr<base::CallbackList<void(void)>::Subscription>
      data_roaming_setting_subscription_;

  base::WeakPtrFactory<DeviceNetworkConfigurationUpdater> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceNetworkConfigurationUpdater);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_NETWORK_CONFIGURATION_UPDATER_H_

