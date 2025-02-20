/*
 * Copyright (C) 2012 Adobe Systems Incorporated. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "core/css/BasicShapeFunctions.h"

#include "core/css/CSSBasicShapeValues.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/css/CSSValuePair.h"
#include "core/css/CSSValuePool.h"
#include "core/css/resolver/StyleResolverState.h"
#include "core/style/BasicShapes.h"
#include "core/style/ComputedStyle.h"

namespace blink {

static RawPtr<CSSValue> valueForCenterCoordinate(CSSValuePool& pool, const ComputedStyle& style, const BasicShapeCenterCoordinate& center, EBoxOrient orientation)
{
    if (center.getDirection() == BasicShapeCenterCoordinate::TopLeft)
        return pool.createValue(center.length(), style);

    CSSValueID keyword = orientation == HORIZONTAL ? CSSValueRight : CSSValueBottom;

    return CSSValuePair::create(pool.createIdentifierValue(keyword), pool.createValue(center.length(), style), CSSValuePair::DropIdenticalValues);
}

static RawPtr<CSSPrimitiveValue> basicShapeRadiusToCSSValue(CSSValuePool& pool, const ComputedStyle& style, const BasicShapeRadius& radius)
{
    switch (radius.type()) {
    case BasicShapeRadius::Value:
        return pool.createValue(radius.value(), style);
    case BasicShapeRadius::ClosestSide:
        return pool.createIdentifierValue(CSSValueClosestSide);
    case BasicShapeRadius::FarthestSide:
        return pool.createIdentifierValue(CSSValueFarthestSide);
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

RawPtr<CSSValue> valueForBasicShape(const ComputedStyle& style, const BasicShape* basicShape)
{
    CSSValuePool& pool = cssValuePool();
    switch (basicShape->type()) {
    case BasicShape::BasicShapeCircleType: {
        const BasicShapeCircle* circle = toBasicShapeCircle(basicShape);
        RawPtr<CSSBasicShapeCircleValue> circleValue = CSSBasicShapeCircleValue::create();

        circleValue->setCenterX(valueForCenterCoordinate(pool, style, circle->centerX(), HORIZONTAL));
        circleValue->setCenterY(valueForCenterCoordinate(pool, style, circle->centerY(), VERTICAL));
        circleValue->setRadius(basicShapeRadiusToCSSValue(pool, style, circle->radius()));
        return circleValue.release();
    }
    case BasicShape::BasicShapeEllipseType: {
        const BasicShapeEllipse* ellipse = toBasicShapeEllipse(basicShape);
        RawPtr<CSSBasicShapeEllipseValue> ellipseValue = CSSBasicShapeEllipseValue::create();

        ellipseValue->setCenterX(valueForCenterCoordinate(pool, style, ellipse->centerX(), HORIZONTAL));
        ellipseValue->setCenterY(valueForCenterCoordinate(pool, style, ellipse->centerY(), VERTICAL));
        ellipseValue->setRadiusX(basicShapeRadiusToCSSValue(pool, style, ellipse->radiusX()));
        ellipseValue->setRadiusY(basicShapeRadiusToCSSValue(pool, style, ellipse->radiusY()));
        return ellipseValue.release();
    }
    case BasicShape::BasicShapePolygonType: {
        const BasicShapePolygon* polygon = toBasicShapePolygon(basicShape);
        RawPtr<CSSBasicShapePolygonValue> polygonValue = CSSBasicShapePolygonValue::create();

        polygonValue->setWindRule(polygon->getWindRule());
        const Vector<Length>& values = polygon->values();
        for (unsigned i = 0; i < values.size(); i += 2)
            polygonValue->appendPoint(pool.createValue(values.at(i), style), pool.createValue(values.at(i + 1), style));

        return polygonValue.release();
    }
    case BasicShape::BasicShapeInsetType: {
        const BasicShapeInset* inset = toBasicShapeInset(basicShape);
        RawPtr<CSSBasicShapeInsetValue> insetValue = CSSBasicShapeInsetValue::create();

        insetValue->setTop(pool.createValue(inset->top(), style));
        insetValue->setRight(pool.createValue(inset->right(), style));
        insetValue->setBottom(pool.createValue(inset->bottom(), style));
        insetValue->setLeft(pool.createValue(inset->left(), style));

        insetValue->setTopLeftRadius(CSSValuePair::create(inset->topLeftRadius(), style));
        insetValue->setTopRightRadius(CSSValuePair::create(inset->topRightRadius(), style));
        insetValue->setBottomRightRadius(CSSValuePair::create(inset->bottomRightRadius(), style));
        insetValue->setBottomLeftRadius(CSSValuePair::create(inset->bottomLeftRadius(), style));

        return insetValue.release();
    }
    default:
        return nullptr;
    }
}

static Length convertToLength(const StyleResolverState& state, CSSPrimitiveValue* value)
{
    if (!value)
        return Length(0, Fixed);
    return value->convertToLength(state.cssToLengthConversionData());
}

static LengthSize convertToLengthSize(const StyleResolverState& state, CSSValuePair* value)
{
    if (!value)
        return LengthSize(Length(0, Fixed), Length(0, Fixed));

    return LengthSize(convertToLength(state, &toCSSPrimitiveValue(value->first())), convertToLength(state, &toCSSPrimitiveValue(value->second())));
}

static BasicShapeCenterCoordinate convertToCenterCoordinate(const StyleResolverState& state, CSSValue* value)
{
    BasicShapeCenterCoordinate::Direction direction;
    Length offset = Length(0, Fixed);

    CSSValueID keyword = CSSValueTop;
    if (!value) {
        keyword = CSSValueCenter;
    } else if (value->isPrimitiveValue() && toCSSPrimitiveValue(value)->isValueID()) {
        keyword = toCSSPrimitiveValue(value)->getValueID();
    } else if (value->isValuePair()) {
        keyword = toCSSPrimitiveValue(toCSSValuePair(value)->first()).getValueID();
        offset = convertToLength(state, &toCSSPrimitiveValue(toCSSValuePair(value)->second()));
    } else {
        offset = convertToLength(state, toCSSPrimitiveValue(value));
    }

    switch (keyword) {
    case CSSValueTop:
    case CSSValueLeft:
        direction = BasicShapeCenterCoordinate::TopLeft;
        break;
    case CSSValueRight:
    case CSSValueBottom:
        direction = BasicShapeCenterCoordinate::BottomRight;
        break;
    case CSSValueCenter:
        direction = BasicShapeCenterCoordinate::TopLeft;
        offset = Length(50, Percent);
        break;
    default:
        ASSERT_NOT_REACHED();
        direction = BasicShapeCenterCoordinate::TopLeft;
        break;
    }

    return BasicShapeCenterCoordinate(direction, offset);
}

static BasicShapeRadius cssValueToBasicShapeRadius(const StyleResolverState& state, RawPtr<CSSPrimitiveValue> radius)
{
    if (!radius)
        return BasicShapeRadius(BasicShapeRadius::ClosestSide);

    if (radius->isValueID()) {
        switch (radius->getValueID()) {
        case CSSValueClosestSide:
            return BasicShapeRadius(BasicShapeRadius::ClosestSide);
        case CSSValueFarthestSide:
            return BasicShapeRadius(BasicShapeRadius::FarthestSide);
        default:
            ASSERT_NOT_REACHED();
            break;
        }
    }

    return BasicShapeRadius(convertToLength(state, radius.get()));
}

PassRefPtr<BasicShape> basicShapeForValue(const StyleResolverState& state, const CSSValue& basicShapeValue)
{
    RefPtr<BasicShape> basicShape;

    if (basicShapeValue.isBasicShapeCircleValue()) {
        const CSSBasicShapeCircleValue& circleValue = toCSSBasicShapeCircleValue(basicShapeValue);
        RefPtr<BasicShapeCircle> circle = BasicShapeCircle::create();

        circle->setCenterX(convertToCenterCoordinate(state, circleValue.centerX()));
        circle->setCenterY(convertToCenterCoordinate(state, circleValue.centerY()));
        circle->setRadius(cssValueToBasicShapeRadius(state, circleValue.radius()));

        basicShape = circle.release();
    } else if (basicShapeValue.isBasicShapeEllipseValue()) {
        const CSSBasicShapeEllipseValue& ellipseValue = toCSSBasicShapeEllipseValue(basicShapeValue);
        RefPtr<BasicShapeEllipse> ellipse = BasicShapeEllipse::create();

        ellipse->setCenterX(convertToCenterCoordinate(state, ellipseValue.centerX()));
        ellipse->setCenterY(convertToCenterCoordinate(state, ellipseValue.centerY()));
        ellipse->setRadiusX(cssValueToBasicShapeRadius(state, ellipseValue.radiusX()));
        ellipse->setRadiusY(cssValueToBasicShapeRadius(state, ellipseValue.radiusY()));

        basicShape = ellipse.release();
    } else if (basicShapeValue.isBasicShapePolygonValue()) {
        const CSSBasicShapePolygonValue& polygonValue = toCSSBasicShapePolygonValue(basicShapeValue);
        RefPtr<BasicShapePolygon> polygon = BasicShapePolygon::create();

        polygon->setWindRule(polygonValue.getWindRule());
        const HeapVector<Member<CSSPrimitiveValue>>& values = polygonValue.values();
        for (unsigned i = 0; i < values.size(); i += 2)
            polygon->appendPoint(convertToLength(state, values.at(i).get()), convertToLength(state, values.at(i + 1).get()));

        basicShape = polygon.release();
    } else if (basicShapeValue.isBasicShapeInsetValue()) {
        const CSSBasicShapeInsetValue& rectValue = toCSSBasicShapeInsetValue(basicShapeValue);
        RefPtr<BasicShapeInset> rect = BasicShapeInset::create();

        rect->setTop(convertToLength(state, rectValue.top()));
        rect->setRight(convertToLength(state, rectValue.right()));
        rect->setBottom(convertToLength(state, rectValue.bottom()));
        rect->setLeft(convertToLength(state, rectValue.left()));

        rect->setTopLeftRadius(convertToLengthSize(state, rectValue.topLeftRadius()));
        rect->setTopRightRadius(convertToLengthSize(state, rectValue.topRightRadius()));
        rect->setBottomRightRadius(convertToLengthSize(state, rectValue.bottomRightRadius()));
        rect->setBottomLeftRadius(convertToLengthSize(state, rectValue.bottomLeftRadius()));

        basicShape = rect.release();
    } else {
        ASSERT_NOT_REACHED();
    }

    return basicShape.release();
}

FloatPoint floatPointForCenterCoordinate(const BasicShapeCenterCoordinate& centerX, const BasicShapeCenterCoordinate& centerY, FloatSize boxSize)
{
    float x = floatValueForLength(centerX.computedLength(), boxSize.width());
    float y = floatValueForLength(centerY.computedLength(), boxSize.height());
    return FloatPoint(x, y);
}

} // namespace blink
