// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/page/scrolling/ScrollState.h"

#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

RawPtr<ScrollState> CreateScrollState(double deltaX, double deltaY, bool beginning, bool ending)
{
    OwnPtr<ScrollStateData> scrollStateData = adoptPtr(new ScrollStateData());
    scrollStateData->delta_x = deltaX;
    scrollStateData->delta_y = deltaY;
    scrollStateData->is_beginning = beginning;
    scrollStateData->is_ending = ending;
    return ScrollState::create(scrollStateData.release());
}

class ScrollStateTest : public testing::Test {
};

TEST_F(ScrollStateTest, ConsumeDeltaNative)
{
    const float deltaX = 12.3;
    const float deltaY = 3.9;

    const float deltaXToConsume = 1.2;
    const float deltaYToConsume = 2.3;

    RawPtr<ScrollState> scrollState = CreateScrollState(deltaX, deltaY, false, false);
    EXPECT_FLOAT_EQ(deltaX, scrollState->deltaX());
    EXPECT_FLOAT_EQ(deltaY, scrollState->deltaY());
    EXPECT_FALSE(scrollState->deltaConsumedForScrollSequence());
    EXPECT_FALSE(scrollState->fullyConsumed());

    scrollState->consumeDeltaNative(0, 0);
    EXPECT_FLOAT_EQ(deltaX, scrollState->deltaX());
    EXPECT_FLOAT_EQ(deltaY, scrollState->deltaY());
    EXPECT_FALSE(scrollState->deltaConsumedForScrollSequence());
    EXPECT_FALSE(scrollState->fullyConsumed());

    scrollState->consumeDeltaNative(deltaXToConsume, 0);
    EXPECT_FLOAT_EQ(deltaX - deltaXToConsume, scrollState->deltaX());
    EXPECT_FLOAT_EQ(deltaY, scrollState->deltaY());
    EXPECT_TRUE(scrollState->deltaConsumedForScrollSequence());
    EXPECT_FALSE(scrollState->fullyConsumed());

    scrollState->consumeDeltaNative(0, deltaYToConsume);
    EXPECT_FLOAT_EQ(deltaX - deltaXToConsume, scrollState->deltaX());
    EXPECT_FLOAT_EQ(deltaY - deltaYToConsume, scrollState->deltaY());
    EXPECT_TRUE(scrollState->deltaConsumedForScrollSequence());
    EXPECT_FALSE(scrollState->fullyConsumed());

    scrollState->consumeDeltaNative(scrollState->deltaX(), scrollState->deltaY());
    EXPECT_TRUE(scrollState->deltaConsumedForScrollSequence());
    EXPECT_TRUE(scrollState->fullyConsumed());
}

TEST_F(ScrollStateTest, CurrentNativeScrollingElement)
{
    RawPtr<ScrollState> scrollState = CreateScrollState(0, 0, false, false);
    RawPtr<Element> element = Element::create(
        QualifiedName::null(), Document::create().get());
    scrollState->setCurrentNativeScrollingElement(element.get());

    EXPECT_EQ(element, scrollState->currentNativeScrollingElement());
}

TEST_F(ScrollStateTest, FullyConsumed)
{
    RawPtr<ScrollState> scrollStateBegin = CreateScrollState(0, 0, true, false);
    RawPtr<ScrollState> scrollState = CreateScrollState(0, 0, false, false);
    RawPtr<ScrollState> scrollStateEnd = CreateScrollState(0, 0, false, true);
    EXPECT_FALSE(scrollStateBegin->fullyConsumed());
    EXPECT_TRUE(scrollState->fullyConsumed());
    EXPECT_FALSE(scrollStateEnd->fullyConsumed());
}

} // namespace

} // namespace blink
