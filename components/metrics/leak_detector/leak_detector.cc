// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/leak_detector/leak_detector.h"

#include <stdint.h>

#include "base/allocator/allocator_extension.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/threading/thread_local.h"
#include "components/metrics/leak_detector/custom_allocator.h"
#include "components/metrics/leak_detector/leak_detector_impl.h"
#include "content/public/browser/browser_thread.h"

#if defined(OS_CHROMEOS)
#include <link.h>  // for dl_iterate_phdr
#else
#error "Getting binary mapping info is not supported on this platform."
#endif  // defined(OS_CHROMEOS)

namespace metrics {

using LeakReport = LeakDetector::LeakReport;
using InternalLeakReport = leak_detector::LeakDetectorImpl::LeakReport;
template <typename T>
using InternalVector = leak_detector::LeakDetectorImpl::InternalVector<T>;

namespace {

// Add the thread-local alloc size count to the shared alloc size count
// (LeakDetector::total_alloc_size_) whenever the local counter reaches
// |LeakDetector::analysis_interval_bytes_| divided by this value. Choose a
// high enough value that there is plenty of granularity, but low enough that a
// thread is not frequently updating the shared counter.
const int kTotalAllocSizeUpdateIntervalDivisor = 1024;

#if defined(OS_CHROMEOS)
// For storing the address range of the Chrome binary in memory.
struct MappingInfo {
  uintptr_t addr;
  size_t size;
};
#endif  // defined(OS_CHROMEOS)

// Local data to be used in the alloc/free hook functions to keep track of
// things across hook function calls.
struct HookData {
  // The total number of bytes nominally allocated from the allocator on the
  // current thread.
  size_t alloc_size;

  // Flag indicating that one of the alloc hooks have already been entered. Used
  // to handle recursive hook calls. Anything allocated when this flag is set
  // should also be freed when this flag is set.
  bool entered_hook;

  HookData() : alloc_size(0), entered_hook(false) {}
};

#if defined(OS_CHROMEOS)
// Callback for dl_iterate_phdr() to find the Chrome binary mapping.
int IterateLoadedObjects(struct dl_phdr_info* shared_object,
                         size_t /* size */,
                         void* data) {
  for (int i = 0; i < shared_object->dlpi_phnum; i++) {
    // Find the ELF segment header that contains the actual code of the Chrome
    // binary.
    const ElfW(Phdr)& segment_header = shared_object->dlpi_phdr[i];
    if (segment_header.p_type == SHT_PROGBITS && segment_header.p_offset == 0 &&
        data) {
      MappingInfo* mapping = reinterpret_cast<MappingInfo*>(data);

      // Make sure the fields in the ELF header and MappingInfo have the
      // same size.
      static_assert(sizeof(mapping->addr) == sizeof(shared_object->dlpi_addr),
                    "Integer size mismatch between MappingInfo::addr and "
                    "dl_phdr_info::dlpi_addr.");
      static_assert(sizeof(mapping->size) == sizeof(segment_header.p_offset),
                    "Integer size mismatch between MappingInfo::size and "
                    "ElfW(Phdr)::p_memsz.");

      mapping->addr = shared_object->dlpi_addr + segment_header.p_offset;
      mapping->size = segment_header.p_memsz;
      return 1;
    }
  }
  return 0;
}
#endif  // defined(OS_CHROMEOS)

// Populates |*tls_data| with a heap-allocated HookData object. Returns the
// address of the allocated object.
HookData* CreateTLSHookData(base::ThreadLocalPointer<HookData>* tls_data) {
  // Allocating a new object will result in a recursive hook function call, when
  // there is no TLS HookData available. This would turn into an infinite loop
  // as this function gets called repeatedly in a futile attempt to create the
  // HookData for the first time, ultimately causing a stack overflow.
  //
  // To get around this, fill |*tls_data| with a temporary HookData object
  // first, and then call the allocator.
  HookData temp_hook_data;
  temp_hook_data.entered_hook = true;
  tls_data->Set(&temp_hook_data);

  tls_data->Set(new HookData);
  return tls_data->Get();
}

// Convert a pointer to a hash value. Returns only the upper eight bits.
inline uint64_t PointerToHash(const void* ptr) {
  // The input data is the pointer address, not the location in memory pointed
  // to by the pointer.
  // The multiplier is taken from Farmhash code:
  //   https://github.com/google/farmhash/blob/master/src/farmhash.cc
  const uint64_t kMultiplier = 0x9ddfea08eb382d69ULL;
  return reinterpret_cast<uint64_t>(ptr) * kMultiplier;
}

// Converts a vector of leak reports generated by LeakDetectorImpl
// (InternalLeakReport) to a vector of leak reports suitable for sending to
// LeakDetector's observers (LeakReport).
void GetReportsForObservers(
    const InternalVector<InternalLeakReport>& leak_reports,
    std::vector<LeakReport>* reports_for_observers) {
  reports_for_observers->clear();
  reports_for_observers->reserve(leak_reports.size());
  for (const InternalLeakReport& report : leak_reports) {
    reports_for_observers->push_back(LeakReport());
    LeakReport* new_report = &reports_for_observers->back();

    new_report->alloc_size_bytes = report.alloc_size_bytes();
    if (!report.call_stack().empty()) {
      new_report->call_stack.resize(report.call_stack().size());
      memcpy(new_report->call_stack.data(), report.call_stack().data(),
             report.call_stack().size() * sizeof(report.call_stack()[0]));
    }
  }
}

// The only instance of LeakDetector that should be used.
base::LazyInstance<LeakDetector>::Leaky g_instance = LAZY_INSTANCE_INITIALIZER;

// Thread-specific data to be used by hook functions.
base::LazyInstance<base::ThreadLocalPointer<HookData>>::Leaky g_hook_data_tls;

}  // namespace

LeakDetector::LeakReport::LeakReport() {}

LeakDetector::LeakReport::~LeakReport() {}

// static
LeakDetector* LeakDetector::GetInstance() {
  return g_instance.Pointer();
}

void LeakDetector::Init(float sampling_rate,
                        size_t max_call_stack_unwind_depth,
                        uint64_t analysis_interval_bytes,
                        uint32_t size_suspicion_threshold,
                        uint32_t call_stack_suspicion_threshold) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(sampling_rate > 0) << "Sampling rate cannot be zero or negative.";

  sampling_factor_ = base::saturated_cast<uint64_t>(sampling_rate * UINT64_MAX);

  analysis_interval_bytes_ = analysis_interval_bytes;
  max_call_stack_unwind_depth_ = max_call_stack_unwind_depth;

  MappingInfo mapping = {0};
#if defined(OS_CHROMEOS)
  // Locate the Chrome binary mapping info.
  dl_iterate_phdr(IterateLoadedObjects, &mapping);
#endif  // defined(OS_CHROMEOS)

  // CustomAllocator can use the default allocator, as long as the hook
  // functions can handle recursive calls.
  leak_detector::CustomAllocator::Initialize();

  // The initialization should be done only once. Check for this by examining
  // whether |impl_| has already been initialized.
  CHECK(!impl_.get()) << "Cannot initialize LeakDetector more than once!";
  impl_.reset(new leak_detector::LeakDetectorImpl(
      mapping.addr, mapping.size, size_suspicion_threshold,
      call_stack_suspicion_threshold));

  // Register allocator hook functions.
  base::allocator::SetHooks(&AllocHook, &FreeHook);
}

void LeakDetector::AddObserver(Observer* observer) {
  base::AutoLock lock(observers_lock_);
  observers_.AddObserver(observer);
}

void LeakDetector::RemoveObserver(Observer* observer) {
  base::AutoLock lock(observers_lock_);
  observers_.RemoveObserver(observer);
}

LeakDetector::LeakDetector()
    : total_alloc_size_(0),
      last_analysis_alloc_size_(0),
      analysis_interval_bytes_(0),
      max_call_stack_unwind_depth_(0),
      sampling_factor_(0) {}

LeakDetector::~LeakDetector() {}

// static
void LeakDetector::AllocHook(const void* ptr, size_t size) {
  base::ThreadLocalPointer<HookData>& hook_data_ptr = g_hook_data_tls.Get();
  HookData* hook_data = hook_data_ptr.Get();
  if (!hook_data) {
    hook_data = CreateTLSHookData(&hook_data_ptr);
  }

  if (hook_data->entered_hook)
    return;

  hook_data->alloc_size += size;

  LeakDetector* detector = GetInstance();
  if (!detector->ShouldSample(ptr))
    return;

  hook_data->entered_hook = true;

  // Get stack trace if necessary.
  std::vector<void*> stack;
  int depth = 0;
  if (detector->impl_->ShouldGetStackTraceForSize(size)) {
    stack.resize(detector->max_call_stack_unwind_depth_);
    depth = base::allocator::GetCallStack(stack.data(), stack.size());
  }

  {
    base::AutoLock lock(detector->recording_lock_);
    detector->impl_->RecordAlloc(ptr, size, depth, stack.data());

    const auto& analysis_interval_bytes = detector->analysis_interval_bytes_;
    auto& total_alloc_size = detector->total_alloc_size_;
    // Update the shared counter, |detector->total_alloc_size_|, once the local
    // counter reaches a threshold that is a fraction of the analysis interval.
    // The fraction should be small enough (and hence the value of
    // kTotalAllocSizeUpdateIntervalDivisor should be large enough) that the
    // shared counter is updated with sufficient granularity. This way, even if
    // a few threads were slow to reach the threshold, the leak analysis would
    // not be delayed by too much.
    if (hook_data->alloc_size >=
        analysis_interval_bytes / kTotalAllocSizeUpdateIntervalDivisor) {
      total_alloc_size += hook_data->alloc_size;
      hook_data->alloc_size = 0;
    }

    // Check for leaks after |analysis_interval_bytes_| bytes have been
    // allocated since the last time that was done.
    if (total_alloc_size >
        detector->last_analysis_alloc_size_ + analysis_interval_bytes) {
      // Try to maintain regular intervals of size |analysis_interval_bytes_|.
      detector->last_analysis_alloc_size_ =
          total_alloc_size - total_alloc_size % analysis_interval_bytes;

      InternalVector<InternalLeakReport> leak_reports;
      detector->impl_->TestForLeaks(&leak_reports);

      // Pass leak reports to observers.
      std::vector<LeakReport> leak_reports_for_observers;
      GetReportsForObservers(leak_reports, &leak_reports_for_observers);
      detector->NotifyObservers(leak_reports_for_observers);
    }
  }

  {
    // The internal memory of |stack| should be freed before setting
    // |entered_hook| to false at the end of this function. Free it here by
    // moving the internal memory to a temporary variable that will go out of
    // scope.
    std::vector<void*> dummy_stack;
    dummy_stack.swap(stack);
  }

  hook_data->entered_hook = false;
}

// static
void LeakDetector::FreeHook(const void* ptr) {
  LeakDetector* detector = GetInstance();
  if (!detector->ShouldSample(ptr))
    return;

  base::ThreadLocalPointer<HookData>& hook_data_ptr = g_hook_data_tls.Get();
  HookData* hook_data = hook_data_ptr.Get();
  if (!hook_data) {
    hook_data = CreateTLSHookData(&hook_data_ptr);
  }

  if (hook_data->entered_hook)
    return;

  hook_data->entered_hook = true;

  {
    base::AutoLock lock(detector->recording_lock_);
    detector->impl_->RecordFree(ptr);
  }

  hook_data->entered_hook = false;
}

inline bool LeakDetector::ShouldSample(const void* ptr) const {
  return PointerToHash(ptr) < sampling_factor_;
}

void LeakDetector::NotifyObservers(const std::vector<LeakReport>& reports) {
  if (reports.empty())
    return;

  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&LeakDetector::NotifyObservers, base::Unretained(this),
                   reports));
    return;
  }

  for (const LeakReport& report : reports) {
    base::AutoLock lock(observers_lock_);
    FOR_EACH_OBSERVER(Observer, observers_, OnLeakFound(report));
  }
}

}  // namespace metrics
