// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains helper functions used by Chromium OS login.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_HELPER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_HELPER_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/view.h"

class GURL;

namespace gfx {
class Rect;
class Size;
}  // namespace gfx

namespace content {
class StoragePartition;
}

namespace net {
class URLRequestContextGetter;
}

namespace chromeos {

// Returns bounds of the screen to use for login wizard.
// The rect is centered within the default monitor and sized accordingly if
// |size| is not empty. Otherwise the whole monitor is occupied.
gfx::Rect CalculateScreenBounds(const gfx::Size& size);

// Returns the size of user image required for proper display under current DPI.
int GetCurrentUserImageSize();

// Define the constants in |login| namespace to avoid potential
// conflict with other chromeos components.
namespace login {

// Maximum size of user image, in which it should be saved to be properly
// displayed under all possible DPI values.
const int kMaxUserImageSize = 512;

// Returns true if lock/login should scroll user pods into view itself when
// virtual keyboard is shown and disable vk overscroll.
bool LoginScrollIntoViewEnabled();

// A helper class for easily mocking out Network*Handler calls for tests.
class NetworkStateHelper {
 public:
  NetworkStateHelper();
  virtual ~NetworkStateHelper();

  // Returns name of the currently connected network.
  // If there are no connected networks, returns name of the network
  // that is in the "connecting" state. Otherwise empty string is returned.
  // If there are multiple connected networks, network priority:
  // Ethernet > WiFi > Cellular. Same for connecting network.
  virtual base::string16 GetCurrentNetworkName() const;

  // Get current connected Wifi network configuration. Used in shark/remora
  // mode. Note currently only unsecured Wifi network configuration can be
  // gotten since there is no way to get password for a secured Wifi newwork
  // in Cros for security reasons.
  virtual void GetConnectedWifiNetwork(std::string* out_onc_spec);

  // Add and apply a network configuration. Used in shark/remora mode.
  virtual void CreateAndConnectNetworkFromOnc(
      const std::string& onc_spec,
      const base::Closure& success_callback,
      const base::Closure& error_callback) const;

  // Returns true if the default network is in connected state.
  virtual bool IsConnected() const;

  // Returns true if the default network is in connecting state.
  virtual bool IsConnecting() const;

 private:
  void OnCreateConfiguration(const base::Closure& success_callback,
                             const base::Closure& error_callback,
                             const std::string& service_path,
                             const std::string& guid) const;
  void OnCreateOrConnectNetworkFailed(
      const base::Closure& error_callback,
      const std::string& error_name,
      scoped_ptr<base::DictionaryValue> error_data) const;

  DISALLOW_COPY_AND_ASSIGN(NetworkStateHelper);
};

//
// Webview based login helpers.
//

// Returns the storage partition for the sign-in webview. Note the function gets
// the partition via the sign-in WebContents thus returns nullptr if the sign-in
// webui is torn down.
content::StoragePartition* GetSigninPartition();

// Returns the request context that contains sign-in cookies. For old iframe
// based flow, the context of the sign-in profile is returned. For webview based
// flow, the context of the sign-in webview storage partition is returned.
net::URLRequestContextGetter* GetSigninContext();

}  // namespace login

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_HELPER_H_
