// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "leak_detector_impl.h"

#include <inttypes.h>
#include <stddef.h>

#include <algorithm>
#include <new>

#include "base/hash.h"
#include "base/process/process_handle.h"
#include "components/metrics/leak_detector/call_stack_table.h"
#include "components/metrics/leak_detector/custom_allocator.h"
#include "components/metrics/leak_detector/ranked_set.h"

namespace metrics {
namespace leak_detector {

namespace {

// Look for leaks in the the top N entries in each tier, where N is this value.
const int kRankedSetSize = 16;

// Initial hash table size for |LeakDetectorImpl::address_map_|.
const int kAddressMapNumBuckets = 100003;

// Number of entries in the alloc size table. As sizes are aligned to 32-bits
// the max supported allocation size is (kNumSizeEntries * 4 - 1). Any larger
// sizes are ignored. This value is chosen high enough that such large sizes
// are rare if not nonexistent.
const int kNumSizeEntries = 2048;

using ValueType = LeakDetectorValueType;

// Functions to convert an allocation size to/from the array index used for
// |LeakDetectorImpl::size_entries_|.
size_t SizeToIndex(const size_t size) {
  int result = static_cast<int>(size / sizeof(uint32_t));
  if (result < kNumSizeEntries)
    return result;
  return 0;
}

size_t IndexToSize(size_t index) {
  return sizeof(uint32_t) * index;
}

}  // namespace

LeakDetectorImpl::LeakReport::LeakReport() : alloc_size_bytes_(0) {}

LeakDetectorImpl::LeakReport::~LeakReport() {}

bool LeakDetectorImpl::LeakReport::operator<(const LeakReport& other) const {
  if (alloc_size_bytes_ != other.alloc_size_bytes_)
    return alloc_size_bytes_ < other.alloc_size_bytes_;
  for (size_t i = 0; i < call_stack_.size() && i < other.call_stack_.size();
       ++i) {
    if (call_stack_[i] != other.call_stack_[i])
      return call_stack_[i] < other.call_stack_[i];
  }
  return call_stack_.size() < other.call_stack_.size();
}

LeakDetectorImpl::LeakDetectorImpl(uintptr_t mapping_addr,
                                   size_t mapping_size,
                                   int size_suspicion_threshold,
                                   int call_stack_suspicion_threshold)
    : num_allocs_(0),
      num_frees_(0),
      alloc_size_(0),
      free_size_(0),
      num_allocs_with_call_stack_(0),
      num_stack_tables_(0),
      address_map_(kAddressMapNumBuckets),
      size_leak_analyzer_(kRankedSetSize, size_suspicion_threshold),
      size_entries_(kNumSizeEntries),
      mapping_addr_(mapping_addr),
      mapping_size_(mapping_size),
      call_stack_suspicion_threshold_(call_stack_suspicion_threshold) {}

LeakDetectorImpl::~LeakDetectorImpl() {
  // Free any call stack tables.
  for (AllocSizeEntry& entry : size_entries_) {
    CallStackTable* table = entry.stack_table;
    if (!table)
      continue;
    table->~CallStackTable();
    CustomAllocator::Free(table, sizeof(CallStackTable));
  }
  size_entries_.clear();
}

bool LeakDetectorImpl::ShouldGetStackTraceForSize(size_t size) const {
  return size_entries_[SizeToIndex(size)].stack_table != nullptr;
}

void LeakDetectorImpl::RecordAlloc(const void* ptr,
                                   size_t size,
                                   int stack_depth,
                                   const void* const stack[]) {
  AllocInfo alloc_info;
  alloc_info.size = size;

  alloc_size_ += alloc_info.size;
  ++num_allocs_;

  AllocSizeEntry* entry = &size_entries_[SizeToIndex(size)];
  ++entry->num_allocs;

  if (entry->stack_table && stack_depth > 0) {
    alloc_info.call_stack =
        call_stack_manager_.GetCallStack(stack_depth, stack);
    entry->stack_table->Add(alloc_info.call_stack);

    ++num_allocs_with_call_stack_;
  }

  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  address_map_.insert(std::pair<uintptr_t, AllocInfo>(addr, alloc_info));
}

void LeakDetectorImpl::RecordFree(const void* ptr) {
  // Look up address.
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  auto iter = address_map_.find(addr);
  // TODO(sque): Catch and report double frees.
  if (iter == address_map_.end())
    return;

  const AllocInfo& alloc_info = iter->second;

  AllocSizeEntry* entry = &size_entries_[SizeToIndex(alloc_info.size)];
  ++entry->num_frees;

  const CallStack* call_stack = alloc_info.call_stack;
  if (call_stack) {
    if (entry->stack_table)
      entry->stack_table->Remove(call_stack);
  }
  ++num_frees_;
  free_size_ += alloc_info.size;

  address_map_.erase(iter);
}

void LeakDetectorImpl::TestForLeaks(InternalVector<LeakReport>* reports) {
  // Add net alloc counts for each size to a ranked list.
  RankedSet size_ranked_set(kRankedSetSize);
  for (size_t i = 0; i < size_entries_.size(); ++i) {
    const AllocSizeEntry& entry = size_entries_[i];
    ValueType size_value(IndexToSize(i));
    size_ranked_set.Add(size_value, entry.num_allocs - entry.num_frees);
  }
  size_leak_analyzer_.AddSample(std::move(size_ranked_set));

  // Get suspected leaks by size.
  for (const ValueType& size_value : size_leak_analyzer_.suspected_leaks()) {
    uint32_t size = size_value.size();
    AllocSizeEntry* entry = &size_entries_[SizeToIndex(size)];
    if (entry->stack_table)
      continue;
    entry->stack_table = new (CustomAllocator::Allocate(sizeof(CallStackTable)))
        CallStackTable(call_stack_suspicion_threshold_);
    ++num_stack_tables_;
  }

  // Check for leaks in each CallStackTable. It makes sense to this before
  // checking the size allocations, because that could potentially create new
  // CallStackTable. However, the overhead to check a new CallStackTable is
  // small since this function is run very rarely. So handle the leak checks of
  // Tier 2 here.
  reports->clear();
  for (size_t i = 0; i < size_entries_.size(); ++i) {
    const AllocSizeEntry& entry = size_entries_[i];
    CallStackTable* stack_table = entry.stack_table;
    if (!stack_table || stack_table->empty())
      continue;

    size_t size = IndexToSize(i);

    // Get suspected leaks by call stack.
    stack_table->TestForLeaks();
    const LeakAnalyzer& leak_analyzer = stack_table->leak_analyzer();
    for (const ValueType& call_stack_value : leak_analyzer.suspected_leaks()) {
      const CallStack* call_stack = call_stack_value.call_stack();

      // Return reports by storing in |*reports|.
      reports->resize(reports->size() + 1);
      LeakReport* report = &reports->back();
      report->alloc_size_bytes_ = size;
      report->call_stack_.resize(call_stack->depth);
      for (size_t j = 0; j < call_stack->depth; ++j) {
        report->call_stack_[j] = GetOffset(call_stack->stack[j]);
      }
    }
  }
}

size_t LeakDetectorImpl::AddressHash::operator()(uintptr_t addr) const {
  return base::Hash(reinterpret_cast<const char*>(&addr), sizeof(addr));
}

uintptr_t LeakDetectorImpl::GetOffset(const void* ptr) const {
  uintptr_t ptr_value = reinterpret_cast<uintptr_t>(ptr);
  if (ptr_value >= mapping_addr_ && ptr_value < mapping_addr_ + mapping_size_)
    return ptr_value - mapping_addr_;
  return ptr_value;
}

}  // namespace leak_detector
}  // namespace metrics
