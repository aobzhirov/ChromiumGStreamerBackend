// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ImageBitmapSource_h
#define ImageBitmapSource_h

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/ScriptState.h"
#include "core/CoreExport.h"
#include "core/dom/ExceptionCode.h"
#include "platform/geometry/IntSize.h"

namespace blink {

class ImageBitmap;
class ImageBitmapOptions;

class CORE_EXPORT ImageBitmapSource {
public:
    virtual IntSize bitmapSourceSize() const { return IntSize(); }
    virtual ScriptPromise createImageBitmap(ScriptState*, EventTarget&, int sx, int sy, int sw, int sh, const ImageBitmapOptions&, ExceptionState&);

    virtual bool isBlob() const { return false; }

    static ScriptPromise fulfillImageBitmap(ScriptState*, RawPtr<ImageBitmap>);
protected:
    virtual ~ImageBitmapSource() {}
};

} // namespace blink

#endif
