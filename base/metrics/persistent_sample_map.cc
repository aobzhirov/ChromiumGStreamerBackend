// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/persistent_sample_map.h"

#include "base/logging.h"
#include "base/stl_util.h"

namespace base {

typedef HistogramBase::Count Count;
typedef HistogramBase::Sample Sample;

namespace {

// An iterator for going through a PersistentSampleMap. The logic here is
// identical to that of SampleMapIterator but with different data structures.
// Changes here likely need to be duplicated there.
class PersistentSampleMapIterator : public SampleCountIterator {
 public:
  typedef std::map<HistogramBase::Sample, HistogramBase::Count*>
      SampleToCountMap;

  explicit PersistentSampleMapIterator(const SampleToCountMap& sample_counts);
  ~PersistentSampleMapIterator() override;

  // SampleCountIterator:
  bool Done() const override;
  void Next() override;
  void Get(HistogramBase::Sample* min,
           HistogramBase::Sample* max,
           HistogramBase::Count* count) const override;

 private:
  void SkipEmptyBuckets();

  SampleToCountMap::const_iterator iter_;
  const SampleToCountMap::const_iterator end_;
};

PersistentSampleMapIterator::PersistentSampleMapIterator(
    const SampleToCountMap& sample_counts)
    : iter_(sample_counts.begin()),
      end_(sample_counts.end()) {
  SkipEmptyBuckets();
}

PersistentSampleMapIterator::~PersistentSampleMapIterator() {}

bool PersistentSampleMapIterator::Done() const {
  return iter_ == end_;
}

void PersistentSampleMapIterator::Next() {
  DCHECK(!Done());
  ++iter_;
  SkipEmptyBuckets();
}

void PersistentSampleMapIterator::Get(Sample* min,
                                      Sample* max,
                                      Count* count) const {
  DCHECK(!Done());
  if (min)
    *min = iter_->first;
  if (max)
    *max = iter_->first + 1;
  if (count)
    *count = *iter_->second;
}

void PersistentSampleMapIterator::SkipEmptyBuckets() {
  while (!Done() && *iter_->second == 0) {
    ++iter_;
  }
}

// This structure holds an entry for a PersistentSampleMap within a persistent
// memory allocator. The "id" must be unique across all maps held by an
// allocator or they will get attached to the wrong sample map.
struct SampleRecord {
  uint64_t id;   // Unique identifier of owner.
  Sample value;  // The value for which this record holds a count.
  Count count;   // The count associated with the above value.
};

// The type-id used to identify sample records inside an allocator.
const uint32_t kTypeIdSampleRecord = 0x8FE6A69F + 1;  // SHA1(SampleRecord) v1

}  // namespace

PersistentSampleMap::PersistentSampleMap(
    uint64_t id,
    PersistentMemoryAllocator* allocator,
    Metadata* meta)
    : HistogramSamples(id, meta),
      allocator_(allocator) {
  // This is created once but will continue to return new iterables even when
  // it has previously reached the end.
  allocator->CreateIterator(&sample_iter_);

  // Load all existing samples during construction. It's no worse to do it
  // here than at some point in the future and could be better if construction
  // takes place on some background thread. New samples could be created at
  // any time by parallel threads; if so, they'll get loaded when needed.
  ImportSamples(kAllSamples);
}

PersistentSampleMap::~PersistentSampleMap() {}

void PersistentSampleMap::Accumulate(Sample value, Count count) {
  *GetOrCreateSampleCountStorage(value) += count;
  IncreaseSum(static_cast<int64_t>(count) * value);
  IncreaseRedundantCount(count);
}

Count PersistentSampleMap::GetCount(Sample value) const {
  // Have to override "const" to make sure all samples have been loaded before
  // being able to know what value to return.
  Count* count_pointer =
      const_cast<PersistentSampleMap*>(this)->GetSampleCountStorage(value);
  return count_pointer ? *count_pointer : 0;
}

Count PersistentSampleMap::TotalCount() const {
  // Have to override "const" in order to make sure all samples have been
  // loaded before trying to iterate over the map.
  const_cast<PersistentSampleMap*>(this)->ImportSamples(kAllSamples);

  Count count = 0;
  for (const auto& entry : sample_counts_) {
    count += *entry.second;
  }
  return count;
}

scoped_ptr<SampleCountIterator> PersistentSampleMap::Iterator() const {
  // Have to override "const" in order to make sure all samples have been
  // loaded before trying to iterate over the map.
  const_cast<PersistentSampleMap*>(this)->ImportSamples(kAllSamples);
  return make_scoped_ptr(new PersistentSampleMapIterator(sample_counts_));
}

bool PersistentSampleMap::AddSubtractImpl(SampleCountIterator* iter,
                                          Operator op) {
  Sample min;
  Sample max;
  Count count;
  for (; !iter->Done(); iter->Next()) {
    iter->Get(&min, &max, &count);
    if (min + 1 != max)
      return false;  // SparseHistogram only supports bucket with size 1.

    *GetOrCreateSampleCountStorage(min) +=
        (op == HistogramSamples::ADD) ? count : -count;
  }
  return true;
}

Count* PersistentSampleMap::GetSampleCountStorage(Sample value) {
  DCHECK_LE(0, value);

  // If |value| is already in the map, just return that.
  auto it = sample_counts_.find(value);
  if (it != sample_counts_.end())
    return it->second;

  // Import any new samples from persistent memory looking for the value.
  return ImportSamples(value);
}

Count* PersistentSampleMap::GetOrCreateSampleCountStorage(Sample value) {
  // Get any existing count storage.
  Count* count_pointer = GetSampleCountStorage(value);
  if (count_pointer)
    return count_pointer;

  // Create a new record in persistent memory for the value.
  PersistentMemoryAllocator::Reference ref =
      allocator_->Allocate(sizeof(SampleRecord), kTypeIdSampleRecord);
  SampleRecord* record =
      allocator_->GetAsObject<SampleRecord>(ref, kTypeIdSampleRecord);
  if (!record) {
    // If the allocator was unable to create a record then it is full or
    // corrupt. Instead, allocate the counter from the heap. This sample will
    // not be persistent, will not be shared, and will leak but it's better
    // than crashing.
    NOTREACHED() << "full=" << allocator_->IsFull()
                 << ", corrupt=" << allocator_->IsCorrupt();
    count_pointer = new Count(0);
    sample_counts_[value] = count_pointer;
    return count_pointer;
  }
  record->id = id();
  record->value = value;
  record->count = 0;  // Should already be zero but don't trust other processes.
  allocator_->MakeIterable(ref);

  // A race condition between two independent processes (i.e. two independent
  // histogram objects sharing the same sample data) could cause two of the
  // above records to be created. The allocator, however, forces a strict
  // ordering on iterable objects so use the import method to actually add the
  // just-created record. This ensures that all PersistentSampleMap objects
  // will always use the same record, whichever was first made iterable.
  // Thread-safety within a process where multiple threads use the same
  // histogram object is delegated to the controlling histogram object which,
  // for sparse histograms, is a lock object.
  count_pointer = ImportSamples(value);
  DCHECK(count_pointer);
  return count_pointer;
}

Count* PersistentSampleMap::ImportSamples(Sample until_value) {
  // TODO(bcwhite): This import operates in O(V+N) total time per sparse
  // histogram where V is the number of values for this object and N is
  // the number of other iterable objects in the allocator. This becomes
  // O(S*(SV+N)) or O(S^2*V + SN) overall where S is the number of sparse
  // histograms.
  //
  // This is actually okay when histograms are expected to exist for the
  // lifetime of the program, spreading the cost out, and S and V are
  // relatively small, as is the current case.
  //
  // However, it is not so good for objects that are created, detroyed, and
  // recreated on a periodic basis, such as when making a snapshot of
  // sparse histograms owned by another, ongoing process. In that case, the
  // entire cost is compressed into a single sequential operation... on the
  // UI thread no less.
  //
  // This will be addressed in a future CL.

  uint32_t type_id;
  PersistentMemoryAllocator::Reference ref;
  while ((ref = allocator_->GetNextIterable(&sample_iter_, &type_id)) != 0) {
    if (type_id == kTypeIdSampleRecord) {
      SampleRecord* record =
          allocator_->GetAsObject<SampleRecord>(ref, kTypeIdSampleRecord);
      if (!record)
        continue;

      // A sample record has been found but may not be for this histogram.
      if (record->id != id())
        continue;

      // Check if the record's value is already known.
      if (!ContainsKey(sample_counts_, record->value)) {
        // No: Add it to map of known values if the value is valid.
        if (record->value >= 0)
          sample_counts_[record->value] = &record->count;
      } else {
        // Yes: Ignore it; it's a duplicate caused by a race condition -- see
        // code & comment in GetOrCreateSampleCountStorage() for details.
        // Check that nothing ever operated on the duplicate record.
        DCHECK_EQ(0, record->count);
      }

      // Stop if it's the value being searched for.
      if (record->value == until_value)
        return &record->count;
    }
  }

  return nullptr;
}

}  // namespace base
