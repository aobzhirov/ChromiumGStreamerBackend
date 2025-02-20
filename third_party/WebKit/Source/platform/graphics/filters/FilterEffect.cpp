/*
 * Copyright (C) 2008 Alex Mathews <possessedpenguinbob@gmail.com>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright (C) 2012 University of Szeged
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

#include "platform/graphics/filters/FilterEffect.h"

#include "platform/graphics/filters/Filter.h"
#include "platform/graphics/filters/SkiaImageFilterBuilder.h"
#include "third_party/skia/include/core/SkColorFilter.h"
#include "third_party/skia/include/effects/SkColorFilterImageFilter.h"
#include "third_party/skia/include/effects/SkPictureImageFilter.h"

namespace blink {

FilterEffect::FilterEffect(Filter* filter)
    : m_filter(filter)
    , m_hasX(false)
    , m_hasY(false)
    , m_hasWidth(false)
    , m_hasHeight(false)
    , m_clipsToBounds(true)
    , m_originTainted(false)
    , m_operatingColorSpace(ColorSpaceLinearRGB)
{
    ASSERT(m_filter);
}

FilterEffect::~FilterEffect()
{
}

DEFINE_TRACE(FilterEffect)
{
    visitor->trace(m_inputEffects);
    visitor->trace(m_filter);
}

FloatRect FilterEffect::determineAbsolutePaintRect(const FloatRect& originalRequestedRect)
{
    FloatRect requestedRect = originalRequestedRect;
    // Filters in SVG clip to primitive subregion, while CSS doesn't.
    if (m_clipsToBounds)
        requestedRect.intersect(maxEffectRect());

    // We may be called multiple times if result is used more than once. Return
    // quickly if if nothing new is required.
    if (absolutePaintRect().contains(enclosingIntRect(requestedRect)))
        return requestedRect;

    FloatRect inputRect = mapPaintRect(requestedRect, false);
    FloatRect inputUnion;
    unsigned size = m_inputEffects.size();

    for (unsigned i = 0; i < size; ++i)
        inputUnion.unite(m_inputEffects.at(i)->determineAbsolutePaintRect(inputRect));
    inputUnion = mapPaintRect(inputUnion, true);

    if (affectsTransparentPixels() || !size) {
        inputUnion = requestedRect;
    } else {
        // Rect may have inflated. Re-intersect with request.
        inputUnion.intersect(requestedRect);
    }

    addAbsolutePaintRect(inputUnion);
    return inputUnion;
}

FloatRect FilterEffect::mapRectRecursive(const FloatRect& rect)
{
    FloatRect result;
    if (m_inputEffects.size() > 0) {
        result = m_inputEffects.at(0)->mapRectRecursive(rect);
        for (unsigned i = 1; i < m_inputEffects.size(); ++i)
            result.unite(m_inputEffects.at(i)->mapRectRecursive(rect));
    } else
        result = rect;
    return mapRect(result);
}

FilterEffect* FilterEffect::inputEffect(unsigned number) const
{
    ASSERT_WITH_SECURITY_IMPLICATION(number < m_inputEffects.size());
    return m_inputEffects.at(number).get();
}

void FilterEffect::addAbsolutePaintRect(const FloatRect& paintRect)
{
    IntRect intPaintRect(enclosingIntRect(paintRect));
    if (m_absolutePaintRect.contains(intPaintRect))
        return;
    intPaintRect.unite(m_absolutePaintRect);
    // Make sure we are not holding on to a smaller rendering.
    clearResult();
    m_absolutePaintRect = intPaintRect;
}

void FilterEffect::clearResult()
{
    m_absolutePaintRect = IntRect();
    for (int i = 0; i < 4; i++) {
        m_imageFilters[i] = nullptr;
    }
}

Color FilterEffect::adaptColorToOperatingColorSpace(const Color& deviceColor)
{
    // |deviceColor| is assumed to be DeviceRGB.
    return ColorSpaceUtilities::convertColor(deviceColor, operatingColorSpace());
}

TextStream& FilterEffect::externalRepresentation(TextStream& ts, int) const
{
    // FIXME: We should dump the subRegions of the filter primitives here later. This isn't
    // possible at the moment, because we need more detailed informations from the target object.
    return ts;
}

FloatRect FilterEffect::applyEffectBoundaries(const FloatRect& rect) const
{
    FloatRect clippedRect = rect;
    if (hasX())
        clippedRect.setX(effectBoundaries().x());
    if (hasY())
        clippedRect.setY(effectBoundaries().y());
    if (hasWidth())
        clippedRect.setWidth(effectBoundaries().width());
    if (hasHeight())
        clippedRect.setHeight(effectBoundaries().height());
    return clippedRect;
}

FloatRect FilterEffect::determineFilterPrimitiveSubregion(DetermineSubregionFlags flags)
{
    Filter* filter = this->getFilter();
    ASSERT(filter);

    // FETile, FETurbulence, FEFlood don't have input effects, take the filter region as unite rect.
    FloatRect subregion;
    if (unsigned numberOfInputEffects = inputEffects().size()) {
        subregion = inputEffect(0)->determineFilterPrimitiveSubregion(flags);
        for (unsigned i = 1; i < numberOfInputEffects; ++i)
            subregion.unite(inputEffect(i)->determineFilterPrimitiveSubregion(flags));
    } else {
        subregion = filter->filterRegion();
    }

    // After calling determineFilterPrimitiveSubregion on the target effect, reset the subregion again for <feTile>.
    if (getFilterEffectType() == FilterEffectTypeTile)
        subregion = filter->filterRegion();

    if (flags & MapRectForward) {
        // mapRect works on absolute rectangles.
        subregion = filter->mapAbsoluteRectToLocalRect(mapRect(
            filter->mapLocalRectToAbsoluteRect(subregion)));
    }

    subregion = applyEffectBoundaries(subregion);

    setFilterPrimitiveSubregion(subregion);

    FloatRect absoluteSubregion = filter->mapLocalRectToAbsoluteRect(subregion);

    // Clip every filter effect to the filter region.
    if (flags & ClipToFilterRegion) {
        absoluteSubregion.intersect(filter->absoluteFilterRegion());
    }

    setMaxEffectRect(absoluteSubregion);
    return subregion;
}

PassRefPtr<SkImageFilter> FilterEffect::createImageFilter(SkiaImageFilterBuilder& builder)
{
    return nullptr;
}

PassRefPtr<SkImageFilter> FilterEffect::createImageFilterWithoutValidation(SkiaImageFilterBuilder& builder)
{
    return createImageFilter(builder);
}

bool FilterEffect::inputsTaintOrigin() const
{
    for (const Member<FilterEffect>& effect : m_inputEffects) {
        if (effect->originTainted())
            return true;
    }
    return false;
}

PassRefPtr<SkImageFilter> FilterEffect::createTransparentBlack(SkiaImageFilterBuilder& builder) const
{
    SkImageFilter::CropRect rect = getCropRect();
    sk_sp<SkColorFilter> colorFilter = SkColorFilter::MakeModeFilter(0, SkXfermode::kClear_Mode);
    return adoptRef(SkColorFilterImageFilter::Create(colorFilter.get(), nullptr, &rect));
}

bool FilterEffect::hasConnectedInput() const
{
    for (unsigned i = 0; i < m_inputEffects.size(); i++) {
        if (m_inputEffects[i] && m_inputEffects[i]->getFilterEffectType() != FilterEffectTypeSourceInput) {
            return true;
        }
    }
    return false;
}

SkImageFilter::CropRect FilterEffect::getCropRect() const
{
    FloatRect rect;
    uint32_t flags = 0;
    if (!hasConnectedInput() && !getFilter()->filterRegion().isEmpty()) {
        rect = getFilter()->filterRegion();
        flags = SkImageFilter::CropRect::kHasAll_CropEdge;
    }

    rect = applyEffectBoundaries(rect);

    rect.scale(getFilter()->scale());

    flags |= hasX() ? SkImageFilter::CropRect::kHasLeft_CropEdge : 0;
    flags |= hasY() ? SkImageFilter::CropRect::kHasTop_CropEdge : 0;
    flags |= hasWidth() ? SkImageFilter::CropRect::kHasWidth_CropEdge : 0;
    flags |= hasHeight() ? SkImageFilter::CropRect::kHasHeight_CropEdge : 0;

    return SkImageFilter::CropRect(rect, flags);
}

static int getImageFilterIndex(ColorSpace colorSpace, bool requiresPMColorValidation)
{
    // Map the (colorspace, bool) tuple to an integer index as follows:
    // 0 == linear colorspace, no PM validation
    // 1 == device colorspace, no PM validation
    // 2 == linear colorspace, PM validation
    // 3 == device colorspace, PM validation
    return (colorSpace == ColorSpaceLinearRGB ? 0x1 : 0x0) | (requiresPMColorValidation ? 0x2 : 0x0);
}

SkImageFilter* FilterEffect::getImageFilter(ColorSpace colorSpace, bool requiresPMColorValidation) const
{
    int index = getImageFilterIndex(colorSpace, requiresPMColorValidation);
    return m_imageFilters[index].get();
}

void FilterEffect::setImageFilter(ColorSpace colorSpace, bool requiresPMColorValidation, PassRefPtr<SkImageFilter> imageFilter)
{
    int index = getImageFilterIndex(colorSpace, requiresPMColorValidation);
    m_imageFilters[index] = imageFilter;
}

} // namespace blink
