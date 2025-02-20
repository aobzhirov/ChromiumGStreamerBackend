// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_l2cap_channel_mac.h"

#include "base/logging.h"
#include "base/mac/sdk_forward_declarations.h"
#include "device/bluetooth/bluetooth_classic_device_mac.h"
#include "device/bluetooth/bluetooth_socket_mac.h"

// A simple delegate class for an open L2CAP channel that forwards methods to
// its wrapped |channel_|.
@interface BluetoothL2capChannelDelegate
    : NSObject <IOBluetoothL2CAPChannelDelegate> {
 @private
  device::BluetoothL2capChannelMac* channel_;  // weak
}

- (id)initWithChannel:(device::BluetoothL2capChannelMac*)channel;

@end

@implementation BluetoothL2capChannelDelegate

- (id)initWithChannel:(device::BluetoothL2capChannelMac*)channel {
  if ((self = [super init]))
    channel_ = channel;

  return self;
}

- (void)l2capChannelOpenComplete:(IOBluetoothL2CAPChannel*)l2capChannel
                          status:(IOReturn)error {
  channel_->OnChannelOpenComplete(l2capChannel, error);
}

- (void)l2capChannelWriteComplete:(IOBluetoothL2CAPChannel*)l2capChannel
                           refcon:(void*)refcon
                           status:(IOReturn)error {
  channel_->OnChannelWriteComplete(l2capChannel, refcon, error);
}

- (void)l2capChannelData:(IOBluetoothL2CAPChannel*)l2capChannel
                    data:(void*)dataPointer
                  length:(size_t)dataLength {
  channel_->OnChannelDataReceived(l2capChannel, dataPointer, dataLength);
}

- (void)l2capChannelClosed:(IOBluetoothL2CAPChannel*)l2capChannel {
  channel_->OnChannelClosed(l2capChannel);
}

// These methods are marked as optional in the 10.8 SDK, but not in the 10.6
// SDK. These empty implementations can be removed once we drop the 10.6 SDK.
- (void)l2capChannelReconfigured:(IOBluetoothL2CAPChannel*)l2capChannel {
}
- (void)l2capChannelQueueSpaceAvailable:(IOBluetoothL2CAPChannel*)l2capChannel {
}

@end

namespace device {

BluetoothL2capChannelMac::BluetoothL2capChannelMac(
    BluetoothSocketMac* socket,
    IOBluetoothL2CAPChannel* channel)
    : channel_(channel),
      delegate_(nil) {
  SetSocket(socket);
}

BluetoothL2capChannelMac::~BluetoothL2capChannelMac() {
  [channel_ setDelegate:nil];
  [channel_ closeChannel];
}

// static
scoped_ptr<BluetoothL2capChannelMac> BluetoothL2capChannelMac::OpenAsync(
    BluetoothSocketMac* socket,
    IOBluetoothDevice* device,
    BluetoothL2CAPPSM psm,
    IOReturn* status) {
  DCHECK(socket);
  scoped_ptr<BluetoothL2capChannelMac> channel(
      new BluetoothL2capChannelMac(socket, nil));

  // Retain the delegate, because IOBluetoothDevice's
  // |-openL2CAPChannelAsync:withPSM:delegate:| assumes that it can take
  // ownership of the delegate without calling |-retain| on it...
  DCHECK(channel->delegate_);
  [channel->delegate_ retain];
  IOBluetoothL2CAPChannel* l2cap_channel;
  *status = [device openL2CAPChannelAsync:&l2cap_channel
                                  withPSM:psm
                                 delegate:channel->delegate_];
  if (*status == kIOReturnSuccess)
    channel->channel_.reset([l2cap_channel retain]);
  else
    channel.reset();

  return channel;
}

void BluetoothL2capChannelMac::SetSocket(BluetoothSocketMac* socket) {
  BluetoothChannelMac::SetSocket(socket);
  if (!this->socket())
    return;

  // Now that the socket is set, it's safe to associate a delegate, which can
  // call back to the socket.
  DCHECK(!delegate_);
  delegate_.reset(
      [[BluetoothL2capChannelDelegate alloc] initWithChannel:this]);
  [channel_ setDelegate:delegate_];
}

IOBluetoothDevice* BluetoothL2capChannelMac::GetDevice() {
  return [channel_ device];
}

uint16_t BluetoothL2capChannelMac::GetOutgoingMTU() {
  return [channel_ outgoingMTU];
}

IOReturn BluetoothL2capChannelMac::WriteAsync(void* data,
                                              uint16_t length,
                                              void* refcon) {
  DCHECK_LE(length, GetOutgoingMTU());
  return [channel_ writeAsync:data length:length refcon:refcon];
}

void BluetoothL2capChannelMac::OnChannelOpenComplete(
    IOBluetoothL2CAPChannel* channel,
    IOReturn status) {
  if (channel_) {
    DCHECK_EQ(channel_, channel);
  } else {
    // The (potentially) asynchronous connection occurred synchronously.
    // Should only be reachable from OpenAsync().
    DCHECK_EQ(status, kIOReturnSuccess);
  }

  socket()->OnChannelOpenComplete(
      BluetoothClassicDeviceMac::GetDeviceAddress([channel device]), status);
}

void BluetoothL2capChannelMac::OnChannelClosed(
    IOBluetoothL2CAPChannel* channel) {
  DCHECK_EQ(channel_, channel);
  socket()->OnChannelClosed();
}

void BluetoothL2capChannelMac::OnChannelDataReceived(
    IOBluetoothL2CAPChannel* channel,
    void* data,
    size_t length) {
  DCHECK_EQ(channel_, channel);
  socket()->OnChannelDataReceived(data, length);
}

void BluetoothL2capChannelMac::OnChannelWriteComplete(
    IOBluetoothL2CAPChannel* channel,
    void* refcon,
    IOReturn status) {
  // Note: We use "CHECK" below to ensure we never run into unforeseen
  // occurrences of asynchronous callbacks, which could lead to data
  // corruption.
  CHECK_EQ(channel_, channel);
  socket()->OnChannelWriteComplete(refcon, status);
}

}  // namespace device
