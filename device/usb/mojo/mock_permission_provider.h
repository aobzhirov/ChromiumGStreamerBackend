// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_USB_MOJO_MOCK_PERMISSION_PROVIDER_H_
#define DEVICE_USB_MOJO_MOCK_PERMISSION_PROVIDER_H_

#include <stdint.h>

#include "base/memory/weak_ptr.h"
#include "device/usb/mojo/permission_provider.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace device {
namespace usb {

class MockPermissionProvider : public PermissionProvider {
 public:
  MockPermissionProvider();
  ~MockPermissionProvider() override;

  base::WeakPtr<PermissionProvider> GetWeakPtr();
  MOCK_CONST_METHOD1(HasDevicePermission, bool(const DeviceInfo& device_info));
  MOCK_CONST_METHOD2(HasConfigurationPermission,
                     bool(uint8_t requested_configuration,
                          const DeviceInfo& device_info));
  MOCK_CONST_METHOD3(HasFunctionPermission,
                     bool(uint8_t requested_function,
                          uint8_t configuration_value,
                          const DeviceInfo& device_info));

 private:
  base::WeakPtrFactory<PermissionProvider> weak_factory_;
};

}  // namespace usb
}  // namespace device

#endif  // DEVICE_USB_MOCK_MOJO_PERMISSION_PROVIDER_H_
