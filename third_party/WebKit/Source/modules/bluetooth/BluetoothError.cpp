// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/bluetooth/BluetoothError.h"

#include "core/dom/DOMException.h"
#include "core/dom/ExceptionCode.h"

namespace blink {

DOMException* BluetoothError::take(ScriptPromiseResolver*, const WebBluetoothError& webError)
{
    switch (webError) {
    case WebBluetoothError::SUCCESS:
        ASSERT_NOT_REACHED();
        return DOMException::create(UnknownError);
#define MAP_ERROR(enumeration, name, message) \
    case WebBluetoothError::enumeration:      \
        return DOMException::create(name, message)

        // InvalidModificationErrors:
        MAP_ERROR(GATT_INVALID_ATTRIBUTE_LENGTH, InvalidModificationError, "GATT Error: invalid attribute length.");

        // InvalidStateErrors:
        MAP_ERROR(SERVICE_NO_LONGER_EXISTS, InvalidStateError, "GATT Service no longer exists.");
        MAP_ERROR(CHARACTERISTIC_NO_LONGER_EXISTS, InvalidStateError, "GATT Characteristic no longer exists.");

        // NetworkErrors:
        MAP_ERROR(CONNECT_ALREADY_IN_PROGRESS, NetworkError, "Connection already in progress.");
        MAP_ERROR(CONNECT_ATTRIBUTE_LENGTH_INVALID, NetworkError, "Write operation exceeds the maximum length of the attribute.");
        MAP_ERROR(CONNECT_AUTH_CANCELED, NetworkError, "Authentication canceled.");
        MAP_ERROR(CONNECT_AUTH_FAILED, NetworkError, "Authentication failed.");
        MAP_ERROR(CONNECT_AUTH_REJECTED, NetworkError, "Authentication rejected.");
        MAP_ERROR(CONNECT_AUTH_TIMEOUT, NetworkError, "Authentication timeout.");
        MAP_ERROR(CONNECT_CONNECTION_CONGESTED, NetworkError, "Remote device connection is congested.");
        MAP_ERROR(CONNECT_INSUFFICIENT_ENCRYPTION, NetworkError, "Insufficient encryption for a given operation");
        MAP_ERROR(CONNECT_OFFSET_INVALID, NetworkError, "Read or write operation was requested with an invalid offset.");
        MAP_ERROR(CONNECT_READ_NOT_PERMITTED, NetworkError, "GATT read operation is not permitted.");
        MAP_ERROR(CONNECT_REQUEST_NOT_SUPPORTED, NetworkError, "The given request is not supported.");
        MAP_ERROR(CONNECT_UNKNOWN_ERROR, NetworkError, "Unknown error when connecting to the device.");
        MAP_ERROR(CONNECT_UNKNOWN_FAILURE, NetworkError, "Connection failed for unknown reason.");
        MAP_ERROR(CONNECT_UNSUPPORTED_DEVICE, NetworkError, "Unsupported device.");
        MAP_ERROR(CONNECT_WRITE_NOT_PERMITTED, NetworkError, "GATT write operation is not permitted.");
        MAP_ERROR(DEVICE_NO_LONGER_IN_RANGE, NetworkError, "Bluetooth Device is no longer in range.");
        MAP_ERROR(GATT_NOT_PAIRED, NetworkError, "GATT Error: Not paired.");
        MAP_ERROR(GATT_OPERATION_IN_PROGRESS, NetworkError, "GATT operation already in progress.");
        MAP_ERROR(UNTRANSLATED_CONNECT_ERROR_CODE, NetworkError, "Unknown ConnectErrorCode.");

        // NotFoundErrors:
        MAP_ERROR(NO_BLUETOOTH_ADAPTER, NotFoundError, "Bluetooth adapter not available.");
        MAP_ERROR(CHOSEN_DEVICE_VANISHED, NotFoundError, "User selected a device that doesn't exist anymore.");
        MAP_ERROR(CHOOSER_CANCELLED, NotFoundError, "User cancelled the requestDevice() chooser.");
        MAP_ERROR(CHOOSER_NOT_SHOWN_API_GLOBALLY_DISABLED, NotFoundError, "Web Bluetooth API globally disabled.");
        MAP_ERROR(CHOOSER_NOT_SHOWN_API_LOCALLY_DISABLED, NotFoundError, "User or their enterprise policy has disabled Web Bluetooth.");
        MAP_ERROR(CHOOSER_NOT_SHOWN_USER_DENIED_PERMISSION_TO_SCAN, NotFoundError, "User denied the browser permission to scan for Bluetooth devices.");
        MAP_ERROR(SERVICE_NOT_FOUND, NotFoundError, "Service not found in device.");
        MAP_ERROR(CHARACTERISTIC_NOT_FOUND, NotFoundError, "No Characteristics with specified UUID found in Service.");
        MAP_ERROR(NO_CHARACTERISTICS_FOUND, NotFoundError, "No Characteristics found in service.");

        // NotSupportedErrors:
        MAP_ERROR(GATT_UNKNOWN_ERROR, NotSupportedError, "GATT Error Unknown.");
        MAP_ERROR(GATT_UNKNOWN_FAILURE, NotSupportedError, "GATT operation failed for unknown reason.");
        MAP_ERROR(GATT_NOT_PERMITTED, NotSupportedError, "GATT operation not permitted.");
        MAP_ERROR(GATT_NOT_SUPPORTED, NotSupportedError, "GATT Error: Not supported.");
        MAP_ERROR(GATT_UNTRANSLATED_ERROR_CODE, NotSupportedError, "GATT Error: Unknown GattErrorCode.");

        // SecurityErrors:
        MAP_ERROR(GATT_NOT_AUTHORIZED, SecurityError, "GATT operation not authorized.");
        MAP_ERROR(BLACKLISTED_CHARACTERISTIC_UUID, SecurityError, "getCharacteristic(s) called with blacklisted UUID. https://goo.gl/4NeimX");
        MAP_ERROR(BLACKLISTED_READ, SecurityError, "readValue() called on blacklisted object marked exclude-reads. https://goo.gl/4NeimX");
        MAP_ERROR(BLACKLISTED_WRITE, SecurityError, "writeValue() called on blacklisted object marked exclude-writes. https://goo.gl/4NeimX");
        MAP_ERROR(NOT_ALLOWED_TO_ACCESS_SERVICE, SecurityError, "Origin is not allowed to access the service. Remember to add the service to a filter or to optionalServices in requestDevice().");
        MAP_ERROR(REQUEST_DEVICE_WITH_BLACKLISTED_UUID, SecurityError, "requestDevice() called with a filter containing a blacklisted UUID. https://goo.gl/4NeimX");
        MAP_ERROR(REQUEST_DEVICE_WITH_UNIQUE_ORIGIN, SecurityError, "requestDevice() called from sandboxed or otherwise unique origin.");
        MAP_ERROR(REQUEST_DEVICE_WITHOUT_FRAME, SecurityError, "No window to show the requestDevice() dialog.");

#undef MAP_ERROR
    }

    ASSERT_NOT_REACHED();
    return DOMException::create(UnknownError);
}

} // namespace blink
