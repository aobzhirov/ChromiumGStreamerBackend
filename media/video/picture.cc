// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "media/video/picture.h"

namespace media {

PictureBuffer::PictureBuffer(int32_t id,
                             gfx::Size size,
                             const TextureIds& texture_ids)
    : id_(id), size_(size), texture_ids_(texture_ids) {}

PictureBuffer::PictureBuffer(int32_t id,
                             gfx::Size size,
                             const TextureIds& texture_ids,
                             const TextureIds& internal_texture_ids)
    : id_(id),
      size_(size),
      texture_ids_(texture_ids),
      internal_texture_ids_(internal_texture_ids) {
  DCHECK_EQ(texture_ids.size(), internal_texture_ids.size());
}

PictureBuffer::PictureBuffer(int32_t id,
                             gfx::Size size,
                             const TextureIds& texture_ids,
                             const std::vector<gpu::Mailbox>& texture_mailboxes)
    : id_(id),
      size_(size),
      texture_ids_(texture_ids),
      texture_mailboxes_(texture_mailboxes) {
  DCHECK_EQ(texture_ids.size(), texture_mailboxes.size());
}

PictureBuffer::~PictureBuffer() {}

Picture::Picture(int32_t picture_buffer_id,
                 int32_t bitstream_buffer_id,
                 const gfx::Rect& visible_rect,
                 bool allow_overlay)
    : picture_buffer_id_(picture_buffer_id),
      bitstream_buffer_id_(bitstream_buffer_id),
      visible_rect_(visible_rect),
      allow_overlay_(allow_overlay),
      size_changed_(false) {}

}  // namespace media
