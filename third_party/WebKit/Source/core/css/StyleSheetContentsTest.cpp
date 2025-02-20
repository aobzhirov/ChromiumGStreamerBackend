// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/StyleSheetContents.h"

#include "core/css/CSSTestHelper.h"
#include "core/css/parser/CSSParser.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

TEST(StyleSheetContentsTest, InsertMediaRule)
{
    CSSParserContext context(HTMLStandardMode, nullptr);

    RawPtr<StyleSheetContents> styleSheet = StyleSheetContents::create(context);
    styleSheet->parseString("@namespace ns url(test);");
    EXPECT_EQ(1U, styleSheet->ruleCount());

    styleSheet->setMutable();
    styleSheet->wrapperInsertRule(CSSParser::parseRule(context, styleSheet.get(), "@media all { div { color: pink } }"), 0);
    EXPECT_EQ(1U, styleSheet->ruleCount());
    EXPECT_FALSE(styleSheet->hasMediaQueries());

    styleSheet->wrapperInsertRule(CSSParser::parseRule(context, styleSheet.get(), "@media all { div { color: green } }"), 1);
    EXPECT_EQ(2U, styleSheet->ruleCount());
    EXPECT_TRUE(styleSheet->hasMediaQueries());
}

} // namespace blink
