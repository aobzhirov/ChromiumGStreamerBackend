// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _CRT_SECURE_NO_WARNINGS

#include "base/process/memory.h"

#include <stddef.h>

#include <limits>

#include "base/allocator/allocator_check.h"
#include "base/compiler_specific.h"
#include "base/debug/alias.h"
#include "base/memory/aligned_memory.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_WIN)
#include <windows.h>
#endif
#if defined(OS_POSIX)
#include <errno.h>
#endif
#if defined(OS_MACOSX)
#include <malloc/malloc.h>
#include "base/mac/mac_util.h"
#include "base/process/memory_unittest_mac.h"
#endif
#if defined(OS_LINUX)
#include <malloc.h>
#include "base/test/malloc_wrapper.h"
#endif

#if defined(OS_WIN)

#if defined(_MSC_VER)
// ssize_t needed for OutOfMemoryTest.
#if defined(_WIN64)
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif
#endif

// HeapQueryInformation function pointer.
typedef BOOL (WINAPI* HeapQueryFn)  \
    (HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T, PSIZE_T);

const int kConstantInModule = 42;

#endif  // defined(OS_WIN)

#if defined(OS_MACOSX)

// For the following Mac tests:
// Note that base::EnableTerminationOnHeapCorruption() is called as part of
// test suite setup and does not need to be done again, else mach_override
// will fail.

TEST(ProcessMemoryTest, MacTerminateOnHeapCorruption) {
  // Assert that freeing an unallocated pointer will crash the process.
  char buf[9];
  asm("" : "=r" (buf));  // Prevent clang from being too smart.
#if ARCH_CPU_64_BITS
  // On 64 bit Macs, the malloc system automatically abort()s on heap corruption
  // but does not output anything.
  ASSERT_DEATH(free(buf), "");
#elif defined(ADDRESS_SANITIZER)
  // AddressSanitizer replaces malloc() and prints a different error message on
  // heap corruption.
  ASSERT_DEATH(free(buf), "attempting free on address which "
      "was not malloc\\(\\)-ed");
#else
  ADD_FAILURE() << "This test is not supported in this build configuration.";
#endif
}

#endif  // defined(OS_MACOSX)

TEST(MemoryTest, AllocatorShimWorking) {
  ASSERT_TRUE(base::allocator::IsAllocatorInitialized());
}

// Android doesn't implement set_new_handler, so we can't use the
// OutOfMemoryTest cases. OpenBSD does not support these tests either.
// Don't test these on ASan/TSan/MSan configurations: only test the real
// allocator.
// Windows only supports these tests with the allocator shim in place.
#if !defined(OS_ANDROID) && !defined(OS_OPENBSD) &&   \
    !(defined(OS_WIN) && !defined(ALLOCATOR_SHIM)) && \
    !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)

namespace {
const char *kOomRegex = "Out of memory";
}  // namespace

class OutOfMemoryTest : public testing::Test {
 public:
  OutOfMemoryTest()
    : value_(NULL),
    // Make test size as large as possible minus a few pages so
    // that alignment or other rounding doesn't make it wrap.
    test_size_(std::numeric_limits<std::size_t>::max() - 12 * 1024),
    // A test size that is > 2Gb and will cause the allocators to reject
    // the allocation due to security restrictions. See crbug.com/169327.
    insecure_test_size_(std::numeric_limits<int>::max()),
    signed_test_size_(std::numeric_limits<ssize_t>::max()) {
  }

 protected:
  void* value_;
  size_t test_size_;
  size_t insecure_test_size_;
  ssize_t signed_test_size_;
};

class OutOfMemoryDeathTest : public OutOfMemoryTest {
 public:
  void SetUpInDeathAssert() {
    // Must call EnableTerminationOnOutOfMemory() because that is called from
    // chrome's main function and therefore hasn't been called yet.
    // Since this call may result in another thread being created and death
    // tests shouldn't be started in a multithread environment, this call
    // should be done inside of the ASSERT_DEATH.
    base::EnableTerminationOnOutOfMemory();
  }
};

TEST_F(OutOfMemoryDeathTest, New) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = operator new(test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, NewArray) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = new char[test_size_];
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, Malloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = malloc(test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, Realloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = realloc(NULL, test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, Calloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = calloc(1024, test_size_ / 1024L);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, AlignedAlloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = base::AlignedAlloc(test_size_, 8);
    }, kOomRegex);
}

// POSIX does not define an aligned realloc function.
#if defined(OS_WIN)
TEST_F(OutOfMemoryDeathTest, AlignedRealloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = _aligned_realloc(NULL, test_size_, 8);
    }, kOomRegex);
}
#endif  // defined(OS_WIN)

// OS X has no 2Gb allocation limit.
// See https://crbug.com/169327.
#if !defined(OS_MACOSX)
TEST_F(OutOfMemoryDeathTest, SecurityNew) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = operator new(insecure_test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, SecurityNewArray) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = new char[insecure_test_size_];
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, SecurityMalloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = malloc(insecure_test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, SecurityRealloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = realloc(NULL, insecure_test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, SecurityCalloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = calloc(1024, insecure_test_size_ / 1024L);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, SecurityAlignedAlloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = base::AlignedAlloc(insecure_test_size_, 8);
    }, kOomRegex);
}

// POSIX does not define an aligned realloc function.
#if defined(OS_WIN)
TEST_F(OutOfMemoryDeathTest, SecurityAlignedRealloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = _aligned_realloc(NULL, insecure_test_size_, 8);
    }, kOomRegex);
}
#endif  // defined(OS_WIN)
#endif  // !defined(OS_MACOSX)

#if defined(OS_LINUX)

TEST_F(OutOfMemoryDeathTest, Valloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = valloc(test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, SecurityValloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = valloc(insecure_test_size_);
    }, kOomRegex);
}

#if PVALLOC_AVAILABLE == 1
TEST_F(OutOfMemoryDeathTest, Pvalloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = pvalloc(test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, SecurityPvalloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = pvalloc(insecure_test_size_);
    }, kOomRegex);
}
#endif  // PVALLOC_AVAILABLE == 1

TEST_F(OutOfMemoryDeathTest, Memalign) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = memalign(4, test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, ViaSharedLibraries) {
  // This tests that the run-time symbol resolution is overriding malloc for
  // shared libraries as well as for our code.
  ASSERT_DEATH({
    SetUpInDeathAssert();
    value_ = MallocWrapper(test_size_);
  }, kOomRegex);
}
#endif  // OS_LINUX

// Android doesn't implement posix_memalign().
#if defined(OS_POSIX) && !defined(OS_ANDROID)
TEST_F(OutOfMemoryDeathTest, Posix_memalign) {
  // Grab the return value of posix_memalign to silence a compiler warning
  // about unused return values. We don't actually care about the return
  // value, since we're asserting death.
  ASSERT_DEATH({
      SetUpInDeathAssert();
      EXPECT_EQ(ENOMEM, posix_memalign(&value_, 8, test_size_));
    }, kOomRegex);
}
#endif  // defined(OS_POSIX) && !defined(OS_ANDROID)

#if defined(OS_MACOSX)

// Purgeable zone tests

TEST_F(OutOfMemoryDeathTest, MallocPurgeable) {
  malloc_zone_t* zone = malloc_default_purgeable_zone();
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = malloc_zone_malloc(zone, test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, ReallocPurgeable) {
  malloc_zone_t* zone = malloc_default_purgeable_zone();
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = malloc_zone_realloc(zone, NULL, test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, CallocPurgeable) {
  malloc_zone_t* zone = malloc_default_purgeable_zone();
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = malloc_zone_calloc(zone, 1024, test_size_ / 1024L);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, VallocPurgeable) {
  malloc_zone_t* zone = malloc_default_purgeable_zone();
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = malloc_zone_valloc(zone, test_size_);
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, PosixMemalignPurgeable) {
  malloc_zone_t* zone = malloc_default_purgeable_zone();
  ASSERT_DEATH({
      SetUpInDeathAssert();
      value_ = malloc_zone_memalign(zone, 8, test_size_);
    }, kOomRegex);
}

// Since these allocation functions take a signed size, it's possible that
// calling them just once won't be enough to exhaust memory. In the 32-bit
// environment, it's likely that these allocation attempts will fail because
// not enough contiguous address space is available. In the 64-bit environment,
// it's likely that they'll fail because they would require a preposterous
// amount of (virtual) memory.

TEST_F(OutOfMemoryDeathTest, CFAllocatorSystemDefault) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      while ((value_ =
              base::AllocateViaCFAllocatorSystemDefault(signed_test_size_))) {}
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, CFAllocatorMalloc) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      while ((value_ =
              base::AllocateViaCFAllocatorMalloc(signed_test_size_))) {}
    }, kOomRegex);
}

TEST_F(OutOfMemoryDeathTest, CFAllocatorMallocZone) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      while ((value_ =
              base::AllocateViaCFAllocatorMallocZone(signed_test_size_))) {}
    }, kOomRegex);
}

#if !defined(ARCH_CPU_64_BITS)

// See process_util_unittest_mac.mm for an explanation of why this test isn't
// run in the 64-bit environment.

TEST_F(OutOfMemoryDeathTest, PsychoticallyBigObjCObject) {
  ASSERT_DEATH({
      SetUpInDeathAssert();
      while ((value_ = base::AllocatePsychoticallyBigObjCObject())) {}
    }, kOomRegex);
}

#endif  // !ARCH_CPU_64_BITS
#endif  // OS_MACOSX

class OutOfMemoryHandledTest : public OutOfMemoryTest {
 public:
  static const size_t kSafeMallocSize = 512;
  static const size_t kSafeCallocSize = 128;
  static const size_t kSafeCallocItems = 4;

  void SetUp() override {
    OutOfMemoryTest::SetUp();

    // We enable termination on OOM - just as Chrome does at early
    // initialization - and test that UncheckedMalloc and  UncheckedCalloc
    // properly by-pass this in order to allow the caller to handle OOM.
    base::EnableTerminationOnOutOfMemory();
  }
};

// TODO(b.kelemen): make UncheckedMalloc and UncheckedCalloc work
// on Windows as well.
// UncheckedMalloc() and UncheckedCalloc() work as regular malloc()/calloc()
// under sanitizer tools.
#if !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
TEST_F(OutOfMemoryHandledTest, UncheckedMalloc) {
#if defined(OS_MACOSX) && ARCH_CPU_32_BITS
  // The Mavericks malloc library changed in a way which breaks the tricks used
  // to implement EnableTerminationOnOutOfMemory() with UncheckedMalloc() under
  // 32-bit.  The 64-bit malloc library works as desired without tricks.
  if (base::mac::IsOSMavericksOrLater())
    return;
#endif
  EXPECT_TRUE(base::UncheckedMalloc(kSafeMallocSize, &value_));
  EXPECT_TRUE(value_ != NULL);
  free(value_);

  EXPECT_FALSE(base::UncheckedMalloc(test_size_, &value_));
  EXPECT_TRUE(value_ == NULL);
}

TEST_F(OutOfMemoryHandledTest, UncheckedCalloc) {
#if defined(OS_MACOSX) && ARCH_CPU_32_BITS
  // The Mavericks malloc library changed in a way which breaks the tricks used
  // to implement EnableTerminationOnOutOfMemory() with UncheckedCalloc() under
  // 32-bit.  The 64-bit malloc library works as desired without tricks.
  if (base::mac::IsOSMavericksOrLater())
    return;
#endif
  EXPECT_TRUE(base::UncheckedCalloc(1, kSafeMallocSize, &value_));
  EXPECT_TRUE(value_ != NULL);
  const char* bytes = static_cast<const char*>(value_);
  for (size_t i = 0; i < kSafeMallocSize; ++i)
    EXPECT_EQ(0, bytes[i]);
  free(value_);

  EXPECT_TRUE(
      base::UncheckedCalloc(kSafeCallocItems, kSafeCallocSize, &value_));
  EXPECT_TRUE(value_ != NULL);
  bytes = static_cast<const char*>(value_);
  for (size_t i = 0; i < (kSafeCallocItems * kSafeCallocSize); ++i)
    EXPECT_EQ(0, bytes[i]);
  free(value_);

  EXPECT_FALSE(base::UncheckedCalloc(1, test_size_, &value_));
  EXPECT_TRUE(value_ == NULL);
}
#endif  // !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
#endif  // !defined(OS_ANDROID) && !defined(OS_OPENBSD) && !(defined(OS_WIN) &&
        // !defined(ALLOCATOR_SHIM)) && !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
