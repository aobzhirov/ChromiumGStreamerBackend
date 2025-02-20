// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Rotation_h
#define Rotation_h

#include "platform/geometry/FloatPoint3D.h"

namespace blink {

struct PLATFORM_EXPORT Rotation {
    Rotation()
        : axis(0, 0, 0)
        , angle(0)
    { }

    Rotation(const FloatPoint3D& axis, double angle)
        : axis(axis)
        , angle(angle)
    { }

    // If either rotation is effectively "zero" or both rotations share the same normalized axes this function returns true
    // and the "non-zero" axis is returned as resultAxis and the effective angles are returned as resultAngleA and resultAngleB.
    // Otherwise false is returned.
    static bool getCommonAxis(const Rotation& /*a*/, const Rotation& /*b*/, FloatPoint3D& resultAxis, double& resultAngleA, double& resultAngleB);

    // A progress of 0 corresponds to "from" and a progress of 1 corresponds to "to".
    static Rotation slerp(const Rotation& from, const Rotation& to, double progress);

    // No restrictions on the axis vector.
    FloatPoint3D axis;

    // Measured in degrees.
    double angle;
};

} // namespace blink

#endif // Rotation_h
