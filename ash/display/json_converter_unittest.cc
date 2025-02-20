// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/json_converter.h"

#include "ash/display/display_layout.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {

TEST(JsonConverterTest, JsonFromToDisplayLayout) {
  DisplayLayout layout;
  layout.primary_id = 1;
  layout.mirrored = true;
  layout.default_unified = false;
  layout.placement_list.push_back(DisplayPlacement());
  layout.placement_list.push_back(DisplayPlacement());
  layout.placement_list[0].display_id = 2;
  layout.placement_list[0].parent_display_id = 1;
  layout.placement_list[0].position = DisplayPlacement::BOTTOM;

  layout.placement_list[1].display_id = 3;
  layout.placement_list[1].parent_display_id = 2;
  layout.placement_list[1].position = DisplayPlacement::LEFT;
  layout.placement_list[1].offset = 30;

  base::DictionaryValue value;
  DisplayLayoutToJson(layout, &value);

  const char data[] =
      "{\n"
      "  \"primary-id\": \"1\",\n"
      "  \"mirrored\": true,\n"
      "  \"default_unified\": false,\n"
      "  \"display_placement\": [{\n"
      "    \"display_id\": \"2\",\n"
      "    \"parent_display_id\": \"1\",\n"
      "    \"position\": \"bottom\",\n"
      "    \"offset\": 0\n"
      "  },{\n"
      "    \"display_id\": \"3\",\n"
      "    \"parent_display_id\": \"2\",\n"
      "    \"position\": \"left\",\n"
      "    \"offset\": 30\n"
      "  }]\n"
      "}";
  int error_code = 0, error_line, error_column;
  std::string error_msg;
  scoped_ptr<base::Value> read_value(base::JSONReader::ReadAndReturnError(
      data, 0, &error_code, &error_msg, &error_line, &error_column));
  ASSERT_EQ(0, error_code) << error_msg << " at " << error_line << ":"
                           << error_column;
  EXPECT_TRUE(value.Equals(read_value.get()));

  DisplayLayout read_layout;
  EXPECT_TRUE(JsonToDisplayLayout(*read_value, &read_layout));
  EXPECT_EQ(read_layout.mirrored, layout.mirrored);
  EXPECT_EQ(read_layout.primary_id, layout.primary_id);
  EXPECT_EQ(read_layout.default_unified, layout.default_unified);
  EXPECT_TRUE(read_layout.HasSamePlacementList(layout));
}

TEST(JsonConverterTest, OldJsonToDisplayLayout) {
  const char data[] =
      "{\n"
      "  \"primary-id\": \"1\",\n"
      "  \"mirrored\": true,\n"
      "  \"default_unified\": false,\n"
      "  \"position\": \"bottom\",\n"
      "  \"offset\": 20\n"
      "}";
  int error_code = 0, error_line, error_column;
  std::string error_msg;
  scoped_ptr<base::Value> read_value(base::JSONReader::ReadAndReturnError(
      data, 0, &error_code, &error_msg, &error_line, &error_column));
  ASSERT_EQ(0, error_code) << error_msg << " at " << error_line << ":"
                           << error_column;

  DisplayLayout read_layout;
  EXPECT_TRUE(JsonToDisplayLayout(*read_value, &read_layout));
  EXPECT_EQ(true, read_layout.mirrored);
  EXPECT_EQ(1, read_layout.primary_id);
  EXPECT_EQ(false, read_layout.default_unified);
  ASSERT_EQ(1u, read_layout.placement_list.size());
  EXPECT_EQ(DisplayPlacement::BOTTOM, read_layout.placement_list[0].position);
  EXPECT_EQ(20, read_layout.placement_list[0].offset);
}

}  // namespace ash
