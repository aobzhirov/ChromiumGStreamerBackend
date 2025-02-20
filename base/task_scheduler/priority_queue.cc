// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/priority_queue.h"

#include <utility>

#include "base/logging.h"

namespace base {
namespace internal {

PriorityQueue::SequenceAndSortKey::SequenceAndSortKey()
    : sort_key(TaskPriority::LOWEST, TimeTicks()) {}

PriorityQueue::SequenceAndSortKey::SequenceAndSortKey(
    scoped_refptr<Sequence> sequence,
    const SequenceSortKey& sort_key)
    : sequence(std::move(sequence)), sort_key(sort_key) {}

PriorityQueue::SequenceAndSortKey::~SequenceAndSortKey() = default;

PriorityQueue::Transaction::Transaction(PriorityQueue* outer_queue)
    : auto_lock_(new AutoSchedulerLock(outer_queue->container_lock_)),
      outer_queue_(outer_queue) {
  DCHECK(CalledOnValidThread());
}

PriorityQueue::Transaction::~Transaction() {
  DCHECK(CalledOnValidThread());

  // Run the sequence insertion callback once for each Sequence that was
  // inserted in the PriorityQueue during the lifetime of this Transaction.
  // Perform this outside the scope of PriorityQueue's lock to avoid imposing an
  // unnecessary lock dependency on |sequence_inserted_callback_|'s destination.
  auto_lock_.reset();
  for (size_t i = 0; i < num_pushed_sequences_; ++i)
    outer_queue_->sequence_inserted_callback_.Run();
}

void PriorityQueue::Transaction::Push(
    scoped_ptr<SequenceAndSortKey> sequence_and_sort_key) {
  DCHECK(CalledOnValidThread());
  DCHECK(!sequence_and_sort_key->is_null());

  outer_queue_->container_.push(std::move(sequence_and_sort_key));
  ++num_pushed_sequences_;
}

const PriorityQueue::SequenceAndSortKey& PriorityQueue::Transaction::Peek()
    const {
  DCHECK(CalledOnValidThread());

  // TODO(fdoray): Add an IsEmpty() method to Transaction and require Peek() to
  // be called on a non-empty PriorityQueue only.
  if (outer_queue_->container_.empty())
    return outer_queue_->empty_sequence_and_sort_key_;

  return *outer_queue_->container_.top();
}

void PriorityQueue::Transaction::Pop() {
  DCHECK(CalledOnValidThread());
  DCHECK(!outer_queue_->container_.empty());
  outer_queue_->container_.pop();
}

PriorityQueue::PriorityQueue(const Closure& sequence_inserted_callback)
    : sequence_inserted_callback_(sequence_inserted_callback) {
  DCHECK(!sequence_inserted_callback_.is_null());
}

PriorityQueue::PriorityQueue(const Closure& sequence_inserted_callback,
                             const PriorityQueue* predecessor_priority_queue)
    : container_lock_(&predecessor_priority_queue->container_lock_),
      sequence_inserted_callback_(sequence_inserted_callback) {
  DCHECK(!sequence_inserted_callback_.is_null());
  DCHECK(predecessor_priority_queue);
}

PriorityQueue::~PriorityQueue() = default;

scoped_ptr<PriorityQueue::Transaction> PriorityQueue::BeginTransaction() {
  return make_scoped_ptr(new Transaction(this));
}

}  // namespace internal
}  // namespace base
