// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/style/StylePath.h"

#include "core/css/CSSPathValue.h"
#include "core/svg/SVGPathByteStream.h"
#include "core/svg/SVGPathUtilities.h"
#include "platform/graphics/Path.h"

namespace blink {

StylePath::StylePath(PassOwnPtr<SVGPathByteStream> pathByteStream)
    : m_byteStream(pathByteStream)
    , m_pathLength(std::numeric_limits<float>::quiet_NaN())
{
    ASSERT(m_byteStream);
}

StylePath::~StylePath()
{
}

PassRefPtr<StylePath> StylePath::create(PassOwnPtr<SVGPathByteStream> pathByteStream)
{
    return adoptRef(new StylePath(pathByteStream));
}

StylePath* StylePath::emptyPath()
{
    DEFINE_STATIC_REF(StylePath, emptyPath, StylePath::create(SVGPathByteStream::create()));
    return emptyPath;
}

const Path& StylePath::path() const
{
    if (!m_path) {
        m_path = adoptPtr(new Path);
        buildPathFromByteStream(*m_byteStream, *m_path);
    }
    return *m_path;
}

float StylePath::length() const
{
    if (std::isnan(m_pathLength))
        m_pathLength = path().length();
    return m_pathLength;
}

bool StylePath::isClosed() const
{
    return path().isClosed();
}

RawPtr<CSSValue> StylePath::computedCSSValue() const
{
    return CSSPathValue::create(const_cast<StylePath*>(this));
}

bool StylePath::operator==(const StylePath& other) const
{
    return *m_byteStream == *other.m_byteStream;
}

} // namespace blink
