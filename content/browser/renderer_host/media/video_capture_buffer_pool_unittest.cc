// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit test for VideoCaptureBufferPool.

#include "content/browser/renderer_host/media/video_capture_buffer_pool.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "build/build_config.h"
#include "cc/test/test_context_provider.h"
#include "cc/test/test_web_graphics_context_3d.h"
#include "content/browser/compositor/buffer_queue.h"
#include "content/browser/gpu/browser_gpu_memory_buffer_manager.h"
#include "content/browser/renderer_host/media/video_capture_controller.h"
#include "media/base/video_frame.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

struct PixelFormatAndStorage {
  media::VideoPixelFormat pixel_format;
  media::VideoPixelStorage pixel_storage;
};

static const PixelFormatAndStorage kCapturePixelFormatAndStorages[] = {
    {media::PIXEL_FORMAT_I420, media::PIXEL_STORAGE_CPU},
    {media::PIXEL_FORMAT_ARGB, media::PIXEL_STORAGE_CPU},
#if !defined(OS_ANDROID)
    {media::PIXEL_FORMAT_I420,
     media::PIXEL_STORAGE_GPUMEMORYBUFFER},
#endif
};

static const int kTestBufferPoolSize = 3;

class VideoCaptureBufferPoolTest
    : public testing::TestWithParam<PixelFormatAndStorage> {
 protected:
  // A GpuMemoryBuffer Mock to provide a trivial RGBA buffer as Map() backing.
  // We need to allocate on ctor and deallocate on dtor so that consecutive
  // Map()-Unmap() cycles yield the same underlying data pointer.
  class MockGpuMemoryBuffer : public gfx::GpuMemoryBuffer {
   public:
    explicit MockGpuMemoryBuffer(const gfx::Size& size)
        : size_(size),
          data_(new uint8_t[size_.GetArea() * 4]),
          mapped_(false) {}
    ~MockGpuMemoryBuffer() override { delete[] data_; }

    bool Map() override {
      EXPECT_FALSE(mapped_);
      mapped_ = true;
      return true;
    }
    void* memory(size_t plane) override {
      EXPECT_TRUE(mapped_);
      EXPECT_EQ(0u, plane);
      return static_cast<void*>(data_);
    }
    void Unmap() override {
      EXPECT_TRUE(mapped_);
      mapped_ = false;
    }
    gfx::Size GetSize() const override { return size_; }
    gfx::BufferFormat GetFormat() const override {
      return gfx::BufferFormat::BGRA_8888;
    }
    int stride(size_t plane) const override {
      EXPECT_EQ(0u, plane);
      return size_.width() * 4;
    }
    gfx::GpuMemoryBufferId GetId() const override {
      return gfx::GpuMemoryBufferId(0);
    }
    gfx::GpuMemoryBufferHandle GetHandle() const override {
      return gfx::GpuMemoryBufferHandle();
    }
    ClientBuffer AsClientBuffer() override { return nullptr; }

   private:
    const gfx::Size size_;
    uint8_t* const data_;
    bool mapped_;
  };

#if !defined(OS_ANDROID)
  // The next two classes are needed to replicate the GpuMemoryBuffer allocation
  // on Browser side.
  class StubBrowserGpuMemoryBufferManager
      : public BrowserGpuMemoryBufferManager {
   public:
    StubBrowserGpuMemoryBufferManager() : BrowserGpuMemoryBufferManager(1, 1) {}

    scoped_ptr<gfx::GpuMemoryBuffer> AllocateGpuMemoryBuffer(
        const gfx::Size& size,
        gfx::BufferFormat format,
        gfx::BufferUsage usage) override {
      return make_scoped_ptr(new MockGpuMemoryBuffer(size));
    }
  };
  class MockBufferQueue : public BufferQueue {
   public:
    MockBufferQueue(scoped_refptr<cc::ContextProvider> context_provider,
                    BrowserGpuMemoryBufferManager* gpu_memory_buffer_manager,
                    unsigned int target,
                    unsigned int internalformat)
        : BufferQueue(context_provider,
                      target,
                      internalformat,
                      nullptr,
                      gpu_memory_buffer_manager,
                      1) {}
    MOCK_METHOD4(CopyBufferDamage,
                 void(int, int, const gfx::Rect&, const gfx::Rect&));
  };
#endif

  // This is a generic Buffer tracker
  class Buffer {
   public:
    Buffer(const scoped_refptr<VideoCaptureBufferPool> pool,
           scoped_ptr<VideoCaptureBufferPool::BufferHandle> buffer_handle,
           int id)
        : id_(id), pool_(pool), buffer_handle_(std::move(buffer_handle)) {}
    ~Buffer() { pool_->RelinquishProducerReservation(id()); }
    int id() const { return id_; }
    size_t mapped_size() { return buffer_handle_->mapped_size(); }
    void* data() { return buffer_handle_->data(0); }

   private:
    const int id_;
    const scoped_refptr<VideoCaptureBufferPool> pool_;
    const scoped_ptr<VideoCaptureBufferPool::BufferHandle> buffer_handle_;
  };

  VideoCaptureBufferPoolTest()
      : expected_dropped_id_(0),
        pool_(new VideoCaptureBufferPool(kTestBufferPoolSize)) {}

#if !defined(OS_ANDROID)
  void SetUp() override {
    scoped_refptr<cc::TestContextProvider> context_provider =
        cc::TestContextProvider::Create(cc::TestWebGraphicsContext3D::Create());
    context_provider->BindToCurrentThread();
    gpu_memory_buffer_manager_.reset(new StubBrowserGpuMemoryBufferManager);
    output_surface_.reset(new MockBufferQueue(context_provider,
                                              gpu_memory_buffer_manager_.get(),
                                              GL_TEXTURE_2D, GL_RGBA));
    output_surface_->Initialize();
  }
#endif

  void ExpectDroppedId(int expected_dropped_id) {
    expected_dropped_id_ = expected_dropped_id;
  }

  scoped_ptr<Buffer> ReserveBuffer(const gfx::Size& dimensions,
                                   PixelFormatAndStorage format_and_storage) {
    // To verify that ReserveBuffer always sets |buffer_id_to_drop|,
    // initialize it to something different than the expected value.
    int buffer_id_to_drop = ~expected_dropped_id_;
    DVLOG(1) << media::VideoCaptureFormat::PixelStorageToString(
                    format_and_storage.pixel_storage) << " "
             << media::VideoPixelFormatToString(format_and_storage.pixel_format)
             << " " << dimensions.ToString();
    const int buffer_id = pool_->ReserveForProducer(
        dimensions, format_and_storage.pixel_format,
        format_and_storage.pixel_storage, &buffer_id_to_drop);
    if (buffer_id == VideoCaptureBufferPool::kInvalidId)
      return scoped_ptr<Buffer>();
    EXPECT_EQ(expected_dropped_id_, buffer_id_to_drop);

    scoped_ptr<VideoCaptureBufferPool::BufferHandle> buffer_handle =
        pool_->GetBufferHandle(buffer_id);
    return scoped_ptr<Buffer>(
        new Buffer(pool_, std::move(buffer_handle), buffer_id));
  }

  scoped_ptr<Buffer> ResurrectLastBuffer(
      const gfx::Size& dimensions,
      PixelFormatAndStorage format_and_storage) {
    const int buffer_id = pool_->ResurrectLastForProducer(
        dimensions, format_and_storage.pixel_format,
        format_and_storage.pixel_storage);
    if (buffer_id == VideoCaptureBufferPool::kInvalidId)
      return scoped_ptr<Buffer>();
    return scoped_ptr<Buffer>(
        new Buffer(pool_, pool_->GetBufferHandle(buffer_id), buffer_id));
  }

  base::MessageLoop loop_;
  int expected_dropped_id_;
  scoped_refptr<VideoCaptureBufferPool> pool_;

 private:
#if !defined(OS_ANDROID)
  scoped_ptr<StubBrowserGpuMemoryBufferManager> gpu_memory_buffer_manager_;
  scoped_ptr<MockBufferQueue> output_surface_;
#endif

  DISALLOW_COPY_AND_ASSIGN(VideoCaptureBufferPoolTest);
};

TEST_P(VideoCaptureBufferPoolTest, BufferPool) {
  const gfx::Size size_lo = gfx::Size(10, 10);
  const gfx::Size size_hi = gfx::Size(21, 33);
  const media::VideoCaptureFormat format_lo(
      size_lo, 0.0, GetParam().pixel_format, GetParam().pixel_storage);
  const media::VideoCaptureFormat format_hi(
      size_hi, 0.0, GetParam().pixel_format, GetParam().pixel_storage);

  // Reallocation won't happen for the first part of the test.
  ExpectDroppedId(VideoCaptureBufferPool::kInvalidId);

  // The buffer pool should have zero utilization before any buffers have been
  // reserved.
  ASSERT_EQ(0.0, pool_->GetBufferPoolUtilization());

  scoped_ptr<Buffer> buffer1 = ReserveBuffer(size_lo, GetParam());
  ASSERT_NE(nullptr, buffer1.get());
  ASSERT_EQ(1.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  scoped_ptr<Buffer> buffer2 = ReserveBuffer(size_lo, GetParam());
  ASSERT_NE(nullptr, buffer2.get());
  ASSERT_EQ(2.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  scoped_ptr<Buffer> buffer3 = ReserveBuffer(size_lo, GetParam());
  ASSERT_NE(nullptr, buffer3.get());
  ASSERT_EQ(3.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());

  // GMB backed frames have their platform and format specific allocations.
  if (GetParam().pixel_storage != media::PIXEL_STORAGE_GPUMEMORYBUFFER) {
    ASSERT_LE(format_lo.ImageAllocationSize(), buffer1->mapped_size());
    ASSERT_LE(format_lo.ImageAllocationSize(), buffer2->mapped_size());
    ASSERT_LE(format_lo.ImageAllocationSize(), buffer3->mapped_size());
  }

  ASSERT_NE(nullptr, buffer1->data());
  ASSERT_NE(nullptr, buffer2->data());
  ASSERT_NE(nullptr, buffer3->data());

  // Touch the memory.
  if (buffer1->data() != nullptr)
    memset(buffer1->data(), 0x11, buffer1->mapped_size());
  if (buffer2->data() != nullptr)
    memset(buffer2->data(), 0x44, buffer2->mapped_size());
  if (buffer3->data() != nullptr)
    memset(buffer3->data(), 0x77, buffer3->mapped_size());

  // Fourth buffer should fail.  Buffer pool utilization should be at 100%.
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  // Release 1st buffer and retry; this should succeed.
  buffer1.reset();
  ASSERT_EQ(2.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  scoped_ptr<Buffer> buffer4 = ReserveBuffer(size_lo, GetParam());
  ASSERT_NE(nullptr, buffer4.get());
  ASSERT_EQ(3.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());

  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());
  ASSERT_FALSE(ReserveBuffer(size_hi, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  // Validate the IDs
  int buffer_id2 = buffer2->id();
  ASSERT_EQ(1, buffer_id2);
  const int buffer_id3 = buffer3->id();
  ASSERT_EQ(2, buffer_id3);
  const int buffer_id4 = buffer4->id();
  ASSERT_EQ(0, buffer_id4);
  void* const memory_pointer3 = buffer3->data();

  // Deliver a buffer.
  pool_->HoldForConsumers(buffer_id3, 2);

  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  buffer3.reset();  // Old producer releases buffer. Should be a noop.
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());
  ASSERT_FALSE(ReserveBuffer(size_hi, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  buffer2.reset();  // Active producer releases buffer. Should free a buffer.

  buffer1 = ReserveBuffer(size_lo, GetParam());
  ASSERT_NE(nullptr, buffer1.get());
  ASSERT_EQ(3.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  // First consumer finishes.
  pool_->RelinquishConsumerHold(buffer_id3, 1);
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  // Second consumer finishes. This should free that buffer.
  pool_->RelinquishConsumerHold(buffer_id3, 1);
  buffer3 = ReserveBuffer(size_lo, GetParam());
  ASSERT_NE(nullptr, buffer3.get());
  ASSERT_EQ(buffer_id3, buffer3->id()) << "Buffer ID should be reused.";
  ASSERT_EQ(memory_pointer3, buffer3->data());
  ASSERT_EQ(3.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  // Now deliver & consume buffer1, but don't release the buffer.
  int buffer_id1 = buffer1->id();
  ASSERT_EQ(1, buffer_id1);
  pool_->HoldForConsumers(buffer_id1, 5);
  pool_->RelinquishConsumerHold(buffer_id1, 5);

  // Even though the consumer is done with the buffer at |buffer_id1|, it cannot
  // be re-allocated to the producer, because |buffer1| still references it. But
  // when |buffer1| goes away, we should be able to re-reserve the buffer (and
  // the ID ought to be the same).
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());
  buffer1.reset();  // Should free the buffer.
  ASSERT_EQ(2.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  buffer2 = ReserveBuffer(size_lo, GetParam());
  ASSERT_NE(nullptr, buffer2.get());
  ASSERT_EQ(buffer_id1, buffer2->id());
  buffer_id2 = buffer_id1;
  ASSERT_EQ(3.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  // Now try reallocation with different resolutions. We expect reallocation
  // to occur only when the old buffer is too small.
  buffer2.reset();
  ExpectDroppedId(buffer_id2);
  ASSERT_EQ(2.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  buffer2 = ReserveBuffer(size_hi, GetParam());
  ASSERT_NE(nullptr, buffer2.get());
  if (GetParam().pixel_storage != media::PIXEL_STORAGE_GPUMEMORYBUFFER)
    ASSERT_LE(format_hi.ImageAllocationSize(), buffer2->mapped_size());
  ASSERT_EQ(3, buffer2->id());
  ASSERT_EQ(3.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  void* const memory_pointer_hi = buffer2->data();
  buffer2.reset();  // Frees it.
  ExpectDroppedId(VideoCaptureBufferPool::kInvalidId);
  ASSERT_EQ(2.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  buffer2 = ReserveBuffer(size_lo, GetParam());
  void* const memory_pointer_lo = buffer2->data();
  ASSERT_EQ(memory_pointer_hi, memory_pointer_lo)
      << "Decrease in resolution should not reallocate buffer";
  ASSERT_NE(nullptr, buffer2.get());
  ASSERT_EQ(3, buffer2->id());
  if (GetParam().pixel_storage != media::PIXEL_STORAGE_GPUMEMORYBUFFER)
    ASSERT_LE(format_lo.ImageAllocationSize(), buffer2->mapped_size());
  ASSERT_EQ(3.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  ASSERT_FALSE(ReserveBuffer(size_lo, GetParam())) << "Pool should be empty";
  ASSERT_EQ(1.0, pool_->GetBufferPoolUtilization());

  // Tear down the pool_, writing into the buffers. The buffer should preserve
  // the lifetime of the underlying memory.
  buffer3.reset();
  ASSERT_EQ(2.0 / kTestBufferPoolSize, pool_->GetBufferPoolUtilization());
  pool_ = NULL;

  // Touch the memory.
  if (buffer2->data() != nullptr)
    memset(buffer2->data(), 0x22, buffer2->mapped_size());
  if (buffer4->data() != nullptr)
    memset(buffer4->data(), 0x55, buffer4->mapped_size());
  buffer2.reset();

  if (buffer4->data() != nullptr)
    memset(buffer4->data(), 0x77, buffer4->mapped_size());
  buffer4.reset();
}

// Tests that a previously-released buffer can be immediately resurrected under
// normal conditions.
TEST_P(VideoCaptureBufferPoolTest, ResurrectsLastBuffer) {
  ExpectDroppedId(VideoCaptureBufferPool::kInvalidId);

  // At the start, there should be nothing to resurrect.
  scoped_ptr<Buffer> resurrected =
      ResurrectLastBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_EQ(nullptr, resurrected.get());

  // Reserve a 10x10 buffer and fill it with 0xab values.
  scoped_ptr<Buffer> original = ReserveBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_NE(nullptr, original.get());
  const size_t original_mapped_size = original->mapped_size();
  memset(original->data(), 0xab, original_mapped_size);

  // Try to resurrect a buffer BEFORE releasing |original|.  This should fail.
  resurrected = ResurrectLastBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_EQ(nullptr, resurrected.get());

  // Release |original| and then try to resurrect it.  Confirm the content of
  // the resurrected buffer is a fill of 0xab values.
  original.reset();
  resurrected = ResurrectLastBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_NE(nullptr, resurrected.get());
  ASSERT_EQ(original_mapped_size, resurrected->mapped_size());
  uint8_t* resurrected_memory = reinterpret_cast<uint8_t*>(resurrected->data());
  for (size_t i = 0; i < original_mapped_size; ++i)
    EXPECT_EQ(0xab, resurrected_memory[i]) << "Mismatch at byte offset: " << i;

  // Now, fill the resurrected buffer with 0xbc values and release it.
  memset(resurrected_memory, 0xbc, original_mapped_size);
  resurrected.reset();

  // Finally, resurrect the buffer again, and confirm it contains a fill of 0xbc
  // values.
  resurrected = ResurrectLastBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_NE(nullptr, resurrected.get());
  ASSERT_EQ(original_mapped_size, resurrected->mapped_size());
  resurrected_memory = reinterpret_cast<uint8_t*>(resurrected->data());
  for (size_t i = 0; i < original_mapped_size; ++i)
    EXPECT_EQ(0xbc, resurrected_memory[i]) << "Mismatch at byte offset: " << i;
}

// Tests that a buffer cannot be resurrected if its properties do not match.
TEST_P(VideoCaptureBufferPoolTest, DoesNotResurrectIfPropertiesNotMatched) {
  ExpectDroppedId(VideoCaptureBufferPool::kInvalidId);

  // Reserve a 10x10 buffer, fill it with 0xcd values, and release it.
  scoped_ptr<Buffer> original = ReserveBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_NE(nullptr, original.get());
  const size_t original_mapped_size = original->mapped_size();
  memset(original->data(), 0xcd, original_mapped_size);
  original.reset();

  // Expect that the buffer cannot be resurrected if the dimensions do not
  // match.
  scoped_ptr<Buffer> resurrected =
      ResurrectLastBuffer(gfx::Size(8, 8), GetParam());
  ASSERT_EQ(nullptr, resurrected.get());

  // Expect that the buffer cannot be resurrected if the pixel format does not
  // match.
  PixelFormatAndStorage altered_format_or_storage = GetParam();
  altered_format_or_storage.pixel_format =
      (altered_format_or_storage.pixel_format == media::PIXEL_FORMAT_I420
           ? media::PIXEL_FORMAT_ARGB
           : media::PIXEL_FORMAT_I420);
  resurrected =
      ResurrectLastBuffer(gfx::Size(10, 10), altered_format_or_storage);
  ASSERT_EQ(nullptr, resurrected.get());

  // Expect that the buffer cannot be resurrected if the pixel storage does not
  // match.
  altered_format_or_storage = GetParam();
  altered_format_or_storage.pixel_storage =
      (altered_format_or_storage.pixel_storage == media::PIXEL_STORAGE_CPU)
          ? media::PIXEL_STORAGE_GPUMEMORYBUFFER
          : media::PIXEL_STORAGE_CPU;
  resurrected =
      ResurrectLastBuffer(gfx::Size(10, 10), altered_format_or_storage);
  ASSERT_EQ(nullptr, resurrected.get());

  // Finally, check that the buffer CAN be resurrected if all properties match.
  resurrected = ResurrectLastBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_NE(nullptr, resurrected.get());
  ASSERT_EQ(original_mapped_size, resurrected->mapped_size());
  uint8_t* resurrected_memory = reinterpret_cast<uint8_t*>(resurrected->data());
  for (size_t i = 0; i < original_mapped_size; ++i)
    EXPECT_EQ(0xcd, resurrected_memory[i]) << "Mismatch at byte offset: " << i;
}

// Tests that the buffers are managed by the pool such that the last-released
// buffer is kept around as long as possible (for successful resurrection).
TEST_P(VideoCaptureBufferPoolTest, AvoidsClobberingForResurrectingLastBuffer) {
  ExpectDroppedId(VideoCaptureBufferPool::kInvalidId);

  // Reserve a 10x10 buffer, fill it with 0xde values, and release it.
  scoped_ptr<Buffer> original = ReserveBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_NE(nullptr, original.get());
  const size_t original_mapped_size = original->mapped_size();
  memset(original->data(), 0xde, original_mapped_size);
  original.reset();

  // Reserve all but one of the pool's buffers.
  std::vector<scoped_ptr<Buffer>> held_buffers;
  for (int i = 0; i < kTestBufferPoolSize - 1; ++i) {
    held_buffers.push_back(ReserveBuffer(gfx::Size(10, 10), GetParam()));
    ASSERT_NE(nullptr, held_buffers.back().get());
  }

  // Now, attempt to resurrect the original buffer.  This should succeed.
  scoped_ptr<Buffer> resurrected =
      ResurrectLastBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_NE(nullptr, resurrected.get());
  ASSERT_EQ(original_mapped_size, resurrected->mapped_size());
  uint8_t* resurrected_memory = reinterpret_cast<uint8_t*>(resurrected->data());
  for (size_t i = 0; i < original_mapped_size; ++i)
    EXPECT_EQ(0xde, resurrected_memory[i]) << "Mismatch at byte offset: " << i;
  resurrected.reset();

  // Reserve the final buffer in the pool, and then confirm resurrection does
  // not succeed.
  held_buffers.push_back(ReserveBuffer(gfx::Size(10, 10), GetParam()));
  ASSERT_NE(nullptr, held_buffers.back().get());
  resurrected = ResurrectLastBuffer(gfx::Size(10, 10), GetParam());
  ASSERT_EQ(nullptr, resurrected.get());
}

INSTANTIATE_TEST_CASE_P(,
                        VideoCaptureBufferPoolTest,
                        testing::ValuesIn(kCapturePixelFormatAndStorages));

} // namespace content
