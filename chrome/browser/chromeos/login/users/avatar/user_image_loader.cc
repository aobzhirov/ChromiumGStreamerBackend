// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/users/avatar/user_image_loader.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/task_runner_util.h"
#include "base/thread_task_runner_handle.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "components/user_manager/user_image/user_image.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/skbitmap_operations.h"

namespace chromeos {
namespace user_image_loader {
namespace {

// Contains attributes we need to know about each image we decode.
struct ImageInfo {
  ImageInfo(const base::FilePath& file_path,
            int pixels_per_side,
            ImageDecoder::ImageCodec image_codec,
            const LoadedCallback& loaded_cb)
      : file_path(file_path),
        pixels_per_side(pixels_per_side),
        image_codec(image_codec),
        loaded_cb(loaded_cb) {}
  ~ImageInfo() {}

  const base::FilePath file_path;
  const int pixels_per_side;
  const ImageDecoder::ImageCodec image_codec;
  const LoadedCallback loaded_cb;
};

// Crops |image| to the square format and downsizes the image to
// |target_size| in pixels. On success, returns true and stores the cropped
// image in |bitmap| and the bytes representation in |byytes|.
bool CropImage(const SkBitmap& image,
               int target_size,
               SkBitmap* bitmap,
               user_manager::UserImage::Bytes* bytes) {
  DCHECK_GT(target_size, 0);

  SkBitmap final_image;
  // Auto crop the image, taking the largest square in the center.
  int pixels_per_side = std::min(image.width(), image.height());
  int x = (image.width() - pixels_per_side) / 2;
  int y = (image.height() - pixels_per_side) / 2;
  SkBitmap cropped_image = SkBitmapOperations::CreateTiledBitmap(
      image, x, y, pixels_per_side, pixels_per_side);
  if (pixels_per_side > target_size) {
    // Also downsize the image to save space and memory.
    final_image = skia::ImageOperations::Resize(
        cropped_image, skia::ImageOperations::RESIZE_LANCZOS3, target_size,
        target_size);
  } else {
    final_image = cropped_image;
  }

  // Encode the cropped image to web-compatible bytes representation
  scoped_ptr<user_manager::UserImage::Bytes> encoded =
      user_manager::UserImage::Encode(final_image);
  if (!encoded)
    return false;

  bitmap->swap(final_image);
  bytes->swap(*encoded);
  return true;
}

// Handles the decoded image returned from ImageDecoder through the
// ImageRequest interface.
class UserImageRequest : public ImageDecoder::ImageRequest {
 public:
  UserImageRequest(
      const ImageInfo& image_info,
      const std::string& image_data,
      scoped_refptr<base::SequencedTaskRunner> background_task_runner)
      : image_info_(image_info),
        image_data_(image_data.begin(), image_data.end()),
        background_task_runner_(background_task_runner),
        weak_ptr_factory_(this) {}
  ~UserImageRequest() override {}

  // ImageDecoder::ImageRequest implementation.
  void OnImageDecoded(const SkBitmap& decoded_image) override;
  void OnDecodeImageFailed() override;

  // Called after the image is cropped (and downsized) as needed.
  void OnImageCropped(SkBitmap* bitmap,
                      user_manager::UserImage::Bytes* bytes,
                      bool succeeded);

  // Called after the image is finalized. |image_bytes_regenerated| is true
  // if |image_bytes| is regenerated from the cropped image.
  void OnImageFinalized(const SkBitmap& image,
                        const user_manager::UserImage::Bytes& image_bytes,
                        bool image_bytes_regenerated);

 private:
  const ImageInfo image_info_;
  const user_manager::UserImage::Bytes image_data_;
  scoped_refptr<base::SequencedTaskRunner> background_task_runner_;

  // This should be the last member.
  base::WeakPtrFactory<UserImageRequest> weak_ptr_factory_;
};

void UserImageRequest::OnImageDecoded(const SkBitmap& decoded_image) {
  int target_size = image_info_.pixels_per_side;
  if (target_size > 0) {
    // Cropping an image could be expensive, hence posting to the background
    // thread.
    SkBitmap* bitmap = new SkBitmap;
    user_manager::UserImage::Bytes* bytes = new user_manager::UserImage::Bytes;
    base::PostTaskAndReplyWithResult(
        background_task_runner_.get(), FROM_HERE,
        base::Bind(&CropImage, decoded_image, target_size, bitmap, bytes),
        base::Bind(&UserImageRequest::OnImageCropped,
                   weak_ptr_factory_.GetWeakPtr(), base::Owned(bitmap),
                   base::Owned(bytes)));
  } else {
    OnImageFinalized(decoded_image, image_data_,
                     false /* image_bytes_regenerated */);
  }
}

void UserImageRequest::OnImageCropped(SkBitmap* bitmap,
                                      user_manager::UserImage::Bytes* bytes,
                                      bool succeeded) {
  DCHECK_GT(image_info_.pixels_per_side, 0);

  if (!succeeded) {
    OnDecodeImageFailed();
    return;
  }
  OnImageFinalized(*bitmap, *bytes, true /* image_bytes_regenerated */);
}

void UserImageRequest::OnImageFinalized(
    const SkBitmap& image,
    const user_manager::UserImage::Bytes& image_bytes,
    bool image_bytes_regenerated) {
  SkBitmap final_image = image;
  // Make the SkBitmap immutable as we won't modify it. This is important
  // because otherwise it gets duplicated during painting, wasting memory.
  final_image.setImmutable();
  gfx::ImageSkia final_image_skia =
      gfx::ImageSkia::CreateFrom1xBitmap(final_image);
  final_image_skia.MakeThreadSafe();
  user_manager::UserImage user_image(final_image_skia, image_bytes);
  user_image.set_file_path(image_info_.file_path);
  if (image_info_.image_codec == ImageDecoder::ROBUST_JPEG_CODEC ||
      image_bytes_regenerated)
    user_image.MarkAsSafe();
  image_info_.loaded_cb.Run(user_image);
  delete this;
}

void UserImageRequest::OnDecodeImageFailed() {
  image_info_.loaded_cb.Run(user_manager::UserImage());
  delete this;
}

// Starts decoding the image with ImageDecoder for the image |data| if
// |data_is_ready| is true.
void DecodeImage(
    const ImageInfo& image_info,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner,
    const std::string* data,
    bool data_is_ready) {
  if (!data_is_ready) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(image_info.loaded_cb, user_manager::UserImage()));
    return;
  }

  UserImageRequest* image_request =
      new UserImageRequest(image_info, *data, background_task_runner);
  ImageDecoder::StartWithOptions(image_request, *data, image_info.image_codec,
                                 false);
}

}  // namespace

void StartWithFilePath(
    scoped_refptr<base::SequencedTaskRunner> background_task_runner,
    const base::FilePath& file_path,
    ImageDecoder::ImageCodec image_codec,
    int pixels_per_side,
    const LoadedCallback& loaded_cb) {
  std::string* data = new std::string;
  base::PostTaskAndReplyWithResult(
      background_task_runner.get(), FROM_HERE,
      base::Bind(&base::ReadFileToString, file_path, data),
      base::Bind(&DecodeImage,
                 ImageInfo(file_path, pixels_per_side, image_codec, loaded_cb),
                 background_task_runner, base::Owned(data)));
}

void StartWithData(
    scoped_refptr<base::SequencedTaskRunner> background_task_runner,
    scoped_ptr<std::string> data,
    ImageDecoder::ImageCodec image_codec,
    int pixels_per_side,
    const LoadedCallback& loaded_cb) {
  DecodeImage(
      ImageInfo(base::FilePath(), pixels_per_side, image_codec, loaded_cb),
      background_task_runner, data.get(), true /* data_is_ready */);
}

}  // namespace user_image_loader
}  // namespace chromeos
