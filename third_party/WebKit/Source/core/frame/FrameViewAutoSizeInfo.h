// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FrameViewAutoSizeInfo_h
#define FrameViewAutoSizeInfo_h

#include "platform/geometry/IntSize.h"
#include "platform/heap/Handle.h"
#include "wtf/Allocator.h"
#include "wtf/Noncopyable.h"
#include "wtf/RefPtr.h"

namespace blink {

class FrameView;

class FrameViewAutoSizeInfo final : public GarbageCollected<FrameViewAutoSizeInfo> {
    WTF_MAKE_NONCOPYABLE(FrameViewAutoSizeInfo);
public:
    static RawPtr<FrameViewAutoSizeInfo> create(FrameView* frameView)
    {
        return new FrameViewAutoSizeInfo(frameView);
    }

    void configureAutoSizeMode(const IntSize& minSize, const IntSize& maxSize);
    void autoSizeIfNeeded();

    DECLARE_TRACE();

private:
    explicit FrameViewAutoSizeInfo(FrameView*);

    Member<FrameView> m_frameView;

    // The lower bound on the size when autosizing.
    IntSize m_minAutoSize;
    // The upper bound on the size when autosizing.
    IntSize m_maxAutoSize;

    bool m_inAutoSize;
    // True if autosize has been run since m_shouldAutoSize was set.
    bool m_didRunAutosize;
};

} // namespace blink

#endif // FrameViewAutoSizeInfo_h
