// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USBDevice_h
#define USBDevice_h

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "bindings/modules/v8/UnionTypesModules.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "core/dom/Document.h"
#include "core/page/PageLifecycleObserver.h"
#include "platform/heap/Handle.h"
#include "public/platform/modules/webusb/WebUSBDevice.h"
#include "public/platform/modules/webusb/WebUSBDeviceInfo.h"
#include "wtf/BitVector.h"
#include "wtf/Vector.h"

namespace blink {

class ScriptPromiseResolver;
class ScriptState;
class USBConfiguration;
class USBControlTransferParameters;

class USBDevice
    : public GarbageCollectedFinalized<USBDevice>
    , public ContextLifecycleObserver
    , public ScriptWrappable
    , public PageLifecycleObserver {
    USING_GARBAGE_COLLECTED_MIXIN(USBDevice);
    DEFINE_WRAPPERTYPEINFO();
public:
    using WebType = OwnPtr<WebUSBDevice>;

    static USBDevice* create(PassOwnPtr<WebUSBDevice> device, ExecutionContext* context)
    {
        return new USBDevice(device, context);
    }

    static USBDevice* take(ScriptPromiseResolver*, PassOwnPtr<WebUSBDevice>);

    explicit USBDevice(PassOwnPtr<WebUSBDevice>, ExecutionContext*);
    virtual ~USBDevice() { }

    const WebUSBDeviceInfo& info() const { return m_device->info(); }
    void onDeviceOpenedOrClosed(bool);
    void onConfigurationSelected(bool success, size_t configurationIndex);
    void onInterfaceClaimedOrUnclaimed(bool claimed, size_t interfaceIndex);
    void onAlternateInterfaceSelected(bool success, size_t interfaceIndex, size_t alternateIndex);
    bool isInterfaceClaimed(size_t configurationIndex, size_t interfaceIndex) const;
    size_t selectedAlternateInterface(size_t interfaceIndex) const;

    // IDL exposed interface:
    String guid() const { return info().guid; }
    uint8_t usbVersionMajor() { return info().usbVersionMajor; }
    uint8_t usbVersionMinor() { return info().usbVersionMinor; }
    uint8_t usbVersionSubminor() { return info().usbVersionSubminor; }
    uint8_t deviceClass() { return info().deviceClass; }
    uint8_t deviceSubclass() const { return info().deviceSubclass; }
    uint8_t deviceProtocol() const { return info().deviceProtocol; }
    uint16_t vendorId() const { return info().vendorID; }
    uint16_t productId() const { return info().productID; }
    uint8_t deviceVersionMajor() const { return info().deviceVersionMajor; }
    uint8_t deviceVersionMinor() const { return info().deviceVersionMinor; }
    uint8_t deviceVersionSubminor() const { return info().deviceVersionSubminor; }
    String manufacturerName() const { return info().manufacturerName; }
    String productName() const { return info().productName; }
    String serialNumber() const { return info().serialNumber; }
    USBConfiguration* configuration() const;
    HeapVector<Member<USBConfiguration>> configurations() const;
    bool opened() const { return m_opened; }

    ScriptPromise open(ScriptState*);
    ScriptPromise close(ScriptState*);
    ScriptPromise selectConfiguration(ScriptState*, uint8_t configurationValue);
    ScriptPromise claimInterface(ScriptState*, uint8_t interfaceNumber);
    ScriptPromise releaseInterface(ScriptState*, uint8_t interfaceNumber);
    ScriptPromise selectAlternateInterface(ScriptState*, uint8_t interfaceNumber, uint8_t alternateSetting);
    ScriptPromise controlTransferIn(ScriptState*, const USBControlTransferParameters& setup, unsigned length);
    ScriptPromise controlTransferOut(ScriptState*, const USBControlTransferParameters& setup);
    ScriptPromise controlTransferOut(ScriptState*, const USBControlTransferParameters& setup, const ArrayBufferOrArrayBufferView& data);
    ScriptPromise clearHalt(ScriptState*, String direction, uint8_t endpointNumber);
    ScriptPromise transferIn(ScriptState*, uint8_t endpointNumber, unsigned length);
    ScriptPromise transferOut(ScriptState*, uint8_t endpointNumber, const ArrayBufferOrArrayBufferView& data);
    ScriptPromise isochronousTransferIn(ScriptState*, uint8_t endpointNumber, Vector<unsigned> packetLengths);
    ScriptPromise isochronousTransferOut(ScriptState*, uint8_t endpointNumber, const ArrayBufferOrArrayBufferView& data, Vector<unsigned> packetLengths);
    ScriptPromise reset(ScriptState*);

    // ContextLifecycleObserver interface.
    void contextDestroyed() override;

    // PageLifecycleObserver interface.
    void pageVisibilityChanged() override;

    DECLARE_TRACE();

private:
    int findConfigurationIndex(uint8_t configurationValue) const;
    int findInterfaceIndex(uint8_t interfaceNumber) const;
    int findAlternateIndex(size_t interfaceIndex, uint8_t alternateSetting) const;
    bool ensurePageVisible(ScriptPromiseResolver*) const;
    bool ensureNoDeviceOrInterfaceChangeInProgress(ScriptPromiseResolver*) const;
    bool ensureDeviceConfigured(ScriptPromiseResolver*) const;
    bool ensureInterfaceClaimed(uint8_t interfaceNumber, ScriptPromiseResolver*) const;
    bool ensureEndpointAvailable(bool inTransfer, uint8_t endpointNumber, ScriptPromiseResolver*) const;
    bool anyInterfaceChangeInProgress() const;
    bool convertControlTransferParameters(WebUSBDevice::TransferDirection, const USBControlTransferParameters&, WebUSBDevice::ControlTransferParameters*, ScriptPromiseResolver*) const;
    void setEndpointsForInterface(size_t interfaceIndex, bool set);

    OwnPtr<WebUSBDevice> m_device;
    bool m_opened;
    bool m_deviceStateChangeInProgress;
    int m_configurationIndex;
    WTF::BitVector m_claimedInterfaces;
    WTF::BitVector m_interfaceStateChangeInProgress;
    WTF::Vector<size_t> m_selectedAlternates;
    WTF::BitVector m_inEndpoints;
    WTF::BitVector m_outEndpoints;
};

} // namespace blink

#endif // USBDevice_h
