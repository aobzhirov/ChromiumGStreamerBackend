// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_VIDEO_MAC_PLATFORM_VIDEO_CAPTURING_MAC_H_
#define MEDIA_CAPTURE_VIDEO_MAC_PLATFORM_VIDEO_CAPTURING_MAC_H_

#import <Foundation/Foundation.h>

namespace media {
class VideoCaptureDeviceMac;
}

// Protocol representing platform-dependent video capture on Mac.
@protocol PlatformVideoCapturingMac<NSObject>

// This method initializes the instance by calling NSObject |init| and registers
// internally a frame receiver at the same time. The frame receiver is supposed
// to be initialised before and outlive the VideoCapturingDeviceMac
// implementation.
- (id)initWithFrameReceiver:(media::VideoCaptureDeviceMac*)frameReceiver;

// Sets the frame receiver. This method executes the registration in mutual
// exclusion.
// TODO(mcasas): This method and stopCapture() are always called in sequence and
// this one is only used to clear the frameReceiver, investigate if both can be
// merged.
- (void)setFrameReceiver:(media::VideoCaptureDeviceMac*)frameReceiver;

// Sets which capture device to use by name passed as deviceId argument. The
// device names are usually obtained via VideoCaptureDevice::GetDeviceNames()
// method. This method will also configure all device properties except those in
// setCaptureHeight:width:frameRate. If |deviceId| is nil, capture is stopped
// and all potential configuration is torn down. Returns YES on success, NO
// otherwise.
- (BOOL)setCaptureDevice:(NSString*)deviceId;

// Configures the capture properties.
- (BOOL)setCaptureHeight:(int)height
                   width:(int)width
               frameRate:(float)frameRate;

// Starts video capturing, registers observers. Returns YES on success, NO
// otherwise.
- (BOOL)startCapture;

// Stops video capturing, unregisters observers.
- (void)stopCapture;

@end

#endif  // MEDIA_CAPTURE_VIDEO_MAC_PLATFORM_VIDEO_CAPTURING_MAC_H_
