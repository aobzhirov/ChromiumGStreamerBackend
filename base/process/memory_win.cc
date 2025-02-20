// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/memory.h"

#include <new.h>
#include <psapi.h>
#include <stddef.h>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"

// malloc_unchecked is required to implement UncheckedMalloc properly.
// It's provided by allocator_shim_win.cc but since that's not always present,
// we provide a default that falls back to regular malloc.
typedef void* (*MallocFn)(size_t);
extern "C" void* (*const malloc_unchecked)(size_t);
extern "C" void* (*const malloc_default)(size_t) = &malloc;

#if defined(_M_IX86)
#pragma comment(linker, "/alternatename:_malloc_unchecked=_malloc_default")
#elif defined(_M_X64) || defined(_M_ARM)
#pragma comment(linker, "/alternatename:malloc_unchecked=malloc_default")
#else
#error Unsupported platform
#endif

namespace base {

namespace {

#pragma warning(push)
#pragma warning(disable: 4702)

int OnNoMemory(size_t size) {
  // Kill the process. This is important for security since most of code
  // does not check the result of memory allocation.
  LOG(FATAL) << "Out of memory, size = " << size;

  // Safety check, make sure process exits here.
  _exit(1);
  return 0;
}

#pragma warning(pop)

// HeapSetInformation function pointer.
typedef BOOL (WINAPI* HeapSetFn)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T);

}  // namespace

void EnableTerminationOnHeapCorruption() {
  // Ignore the result code. Supported on XP SP3 and Vista.
  HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
}

void EnableTerminationOnOutOfMemory() {
  _set_new_handler(&OnNoMemory);
  _set_new_mode(1);
}

// Implemented using a weak symbol.
bool UncheckedMalloc(size_t size, void** result) {
  *result = malloc_unchecked(size);
  return *result != NULL;
}

}  // namespace base
