/*
 * Copyright (C) Research In Motion Limited 2011-2012. All rights reserved.
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
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

#ifndef SVGAnimatedTypeAnimator_h
#define SVGAnimatedTypeAnimator_h

#include "core/svg/properties/SVGPropertyInfo.h"
#include "platform/heap/Handle.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/RefPtr.h"
#include "wtf/Vector.h"
#include "wtf/text/WTFString.h"

namespace blink {

class SVGAnimatedPropertyBase;
class SVGPropertyBase;
class SVGElement;
class SVGAnimationElement;

// The size of SVGElementInstances is 1 unless there is a <use> instance of the element.
using SVGElementInstances = HeapVector<Member<SVGElement>, 1u>;

class SVGAnimatedTypeAnimator final {
    DISALLOW_NEW();
public:
    SVGAnimatedTypeAnimator(SVGAnimationElement*);

    void clear();
    void reset(SVGElement* contextElement);

    RawPtr<SVGPropertyBase> constructFromString(const String&);

    RawPtr<SVGPropertyBase> startAnimValAnimation(const SVGElementInstances&);
    void stopAnimValAnimation(const SVGElementInstances&);
    RawPtr<SVGPropertyBase> resetAnimValToBaseVal(const SVGElementInstances&);

    void calculateAnimatedValue(float percentage, unsigned repeatCount, SVGPropertyBase*, SVGPropertyBase*, SVGPropertyBase*, SVGPropertyBase*);
    float calculateDistance(const String& fromString, const String& toString);

    void calculateFromAndToValues(Member<SVGPropertyBase>& from, Member<SVGPropertyBase>& to, const String& fromString, const String& toString);
    void calculateFromAndByValues(Member<SVGPropertyBase>& from, Member<SVGPropertyBase>& to, const String& fromString, const String& byString);

    void setContextElement(SVGElement* contextElement) { m_contextElement = contextElement; }
    AnimatedPropertyType type() const { return m_type; }

    bool isAnimatingSVGDom() const { return m_animatedProperty; }
    bool isAnimatingCSSProperty() const { return !m_animatedProperty; }

    DECLARE_TRACE();

private:
    friend class ParsePropertyFromString;
    RawPtr<SVGPropertyBase> createPropertyForAnimation(const String&);
    RawPtr<SVGPropertyBase> resetAnimation(const SVGElementInstances&);

    Member<SVGAnimationElement> m_animationElement;
    Member<SVGElement> m_contextElement;
    Member<SVGAnimatedPropertyBase> m_animatedProperty;
    AnimatedPropertyType m_type;
};

} // namespace blink

#endif // SVGAnimatedTypeAnimator_h
