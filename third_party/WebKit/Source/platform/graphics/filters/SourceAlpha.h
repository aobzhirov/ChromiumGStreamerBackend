/*
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef SourceAlpha_h
#define SourceAlpha_h

#include "platform/graphics/filters/FilterEffect.h"

namespace blink {

class PLATFORM_EXPORT SourceAlpha final : public FilterEffect {
public:
    static RawPtr<SourceAlpha> create(FilterEffect*);

    FloatRect determineAbsolutePaintRect(const FloatRect& requestedRect) override;

    FilterEffectType getFilterEffectType() const override { return FilterEffectTypeSourceInput; }

    TextStream& externalRepresentation(TextStream&, int indention) const override;
    PassRefPtr<SkImageFilter> createImageFilter(SkiaImageFilterBuilder&) override;

private:
    explicit SourceAlpha(FilterEffect*);
};

} // namespace blink

#endif // SourceAlpha_h
