/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/css/CSSMatrix.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/CSSPropertyNames.h"
#include "core/CSSValueKeywords.h"
#include "core/css/CSSToLengthConversionData.h"
#include "core/css/StylePropertySet.h"
#include "core/css/parser/CSSParser.h"
#include "core/css/resolver/TransformBuilder.h"
#include "core/dom/ExceptionCode.h"
#include "core/frame/UseCounter.h"
#include "core/style/ComputedStyle.h"
#include "core/style/StyleInheritedData.h"
#include "wtf/MathExtras.h"

namespace blink {

RawPtr<CSSMatrix> CSSMatrix::create(ExecutionContext* executionContext, const String& s, ExceptionState& exceptionState)
{
    UseCounter::count(executionContext, UseCounter::WebKitCSSMatrix);
    return new CSSMatrix(s, exceptionState);
}

CSSMatrix::CSSMatrix(const TransformationMatrix& m)
    : m_matrix(TransformationMatrix::create(m))
{
}

CSSMatrix::CSSMatrix(const String& s, ExceptionState& exceptionState)
    : m_matrix(TransformationMatrix::create())
{
    setMatrixValue(s, exceptionState);
}

static inline PassRefPtr<ComputedStyle> createInitialStyle()
{
    RefPtr<ComputedStyle> initialStyle = ComputedStyle::create();
    initialStyle->font().update(nullptr);
    return initialStyle;
}

void CSSMatrix::setMatrixValue(const String& string, ExceptionState& exceptionState)
{
    if (string.isEmpty())
        return;

    if (RawPtr<CSSValue> value = CSSParser::parseSingleValue(CSSPropertyTransform, string)) {
        // Check for a "none" transform. In these cases we can use the default identity matrix.
        if (value->isPrimitiveValue() && (toCSSPrimitiveValue(value.get()))->getValueID() == CSSValueNone)
            return;

        DEFINE_STATIC_REF(ComputedStyle, initialStyle, createInitialStyle());
        TransformOperations operations;
        TransformBuilder::createTransformOperations(*value, CSSToLengthConversionData(initialStyle, initialStyle, nullptr, 1.0f), operations);

        // Convert transform operations to a TransformationMatrix. This can fail
        // if a param has a percentage ('%')
        if (operations.dependsOnBoxSize())
            exceptionState.throwDOMException(SyntaxError, "The transformation depends on the box size, which is not supported.");
        m_matrix = TransformationMatrix::create();
        operations.apply(FloatSize(0, 0), *m_matrix);
    } else { // There is something there but parsing failed.
        exceptionState.throwDOMException(SyntaxError, "Failed to parse '" + string + "'.");
    }
}

// Perform a concatenation of the matrices (this * secondMatrix)
RawPtr<CSSMatrix> CSSMatrix::multiply(CSSMatrix* secondMatrix) const
{
    if (!secondMatrix)
        return nullptr;

    return CSSMatrix::create(TransformationMatrix(*m_matrix).multiply(*secondMatrix->m_matrix));
}

RawPtr<CSSMatrix> CSSMatrix::inverse(ExceptionState& exceptionState) const
{
    if (!m_matrix->isInvertible()) {
        exceptionState.throwDOMException(NotSupportedError, "The matrix is not invertable.");
        return nullptr;
    }

    return CSSMatrix::create(m_matrix->inverse());
}

RawPtr<CSSMatrix> CSSMatrix::translate(double x, double y, double z) const
{
    if (std::isnan(x))
        x = 0;
    if (std::isnan(y))
        y = 0;
    if (std::isnan(z))
        z = 0;
    return CSSMatrix::create(TransformationMatrix(*m_matrix).translate3d(x, y, z));
}

RawPtr<CSSMatrix> CSSMatrix::scale(double scaleX, double scaleY, double scaleZ) const
{
    if (std::isnan(scaleX))
        scaleX = 1;
    if (std::isnan(scaleY))
        scaleY = scaleX;
    if (std::isnan(scaleZ))
        scaleZ = 1;
    return CSSMatrix::create(TransformationMatrix(*m_matrix).scale3d(scaleX, scaleY, scaleZ));
}

RawPtr<CSSMatrix> CSSMatrix::rotate(double rotX, double rotY, double rotZ) const
{
    if (std::isnan(rotX))
        rotX = 0;

    if (std::isnan(rotY) && std::isnan(rotZ)) {
        rotZ = rotX;
        rotX = 0;
        rotY = 0;
    }

    if (std::isnan(rotY))
        rotY = 0;
    if (std::isnan(rotZ))
        rotZ = 0;
    return CSSMatrix::create(TransformationMatrix(*m_matrix).rotate3d(rotX, rotY, rotZ));
}

RawPtr<CSSMatrix> CSSMatrix::rotateAxisAngle(double x, double y, double z, double angle) const
{
    if (std::isnan(x))
        x = 0;
    if (std::isnan(y))
        y = 0;
    if (std::isnan(z))
        z = 0;
    if (std::isnan(angle))
        angle = 0;
    if (!x && !y && !z)
        z = 1;
    return CSSMatrix::create(TransformationMatrix(*m_matrix).rotate3d(x, y, z, angle));
}

RawPtr<CSSMatrix> CSSMatrix::skewX(double angle) const
{
    if (std::isnan(angle))
        angle = 0;
    return CSSMatrix::create(TransformationMatrix(*m_matrix).skewX(angle));
}

RawPtr<CSSMatrix> CSSMatrix::skewY(double angle) const
{
    if (std::isnan(angle))
        angle = 0;
    return CSSMatrix::create(TransformationMatrix(*m_matrix).skewY(angle));
}

String CSSMatrix::toString() const
{
    // FIXME - Need to ensure valid CSS floating point values (https://bugs.webkit.org/show_bug.cgi?id=20674)
    if (m_matrix->isAffine())
        return String::format("matrix(%f, %f, %f, %f, %f, %f)", m_matrix->a(), m_matrix->b(), m_matrix->c(), m_matrix->d(), m_matrix->e(), m_matrix->f());
    return String::format("matrix3d(%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f)",
    m_matrix->m11(), m_matrix->m12(), m_matrix->m13(), m_matrix->m14(),
    m_matrix->m21(), m_matrix->m22(), m_matrix->m23(), m_matrix->m24(),
    m_matrix->m31(), m_matrix->m32(), m_matrix->m33(), m_matrix->m34(),
    m_matrix->m41(), m_matrix->m42(), m_matrix->m43(), m_matrix->m44());
}

} // namespace blink
