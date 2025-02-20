// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DISPLAY_JSON_CONVERTER_H_
#define ASH_DISPLAY_JSON_CONVERTER_H_

#include "ash/ash_export.h"

namespace base {
class Value;
}

namespace ash {

class DisplayLayout;

ASH_EXPORT bool JsonToDisplayLayout(const base::Value& value,
                                    DisplayLayout* layout);

ASH_EXPORT bool DisplayLayoutToJson(const DisplayLayout& layout,
                                    base::Value* value);

}  // namespace ash

#endif  // ASH_DISPLAY_JSON_CONVERTER_H_
