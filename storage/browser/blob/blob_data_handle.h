// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BROWSER_BLOB_BLOB_DATA_HANDLE_H_
#define STORAGE_BROWSER_BLOB_BLOB_DATA_HANDLE_H_

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "storage/browser/storage_browser_export.h"

namespace base {
class SequencedTaskRunner;
}

namespace storage {

class BlobDataSnapshot;
class BlobReader;
class BlobStorageContext;
class FileSystemContext;

// BlobDataHandle ensures that the underlying blob (keyed by the uuid) remains
// in the BlobStorageContext's collection while this object is alive. Anything
// that needs to keep a blob alive needs to store this handle.
// When the blob data itself is needed, clients must call the CreateSnapshot()
// method on the IO thread to create a snapshot of the blob data.  This snapshot
// is not intended to be persisted, and serves to ensure that the backing
// resources remain around for the duration of reading the blob.  This snapshot
// can be read on any thread, but it must be destructed on the IO thread.
// This object has delete semantics and may be deleted on any thread.
class STORAGE_EXPORT BlobDataHandle
    : public base::SupportsUserData::Data {
 public:
  BlobDataHandle(const BlobDataHandle& other);  // May be copied on any thread.
  ~BlobDataHandle() override;                   // May be deleted on any thread.

  // Returns if this blob is still constructing. If so, one can use the
  // RunOnConstructionComplete to wait.
  // Must be called on IO thread.
  bool IsBeingBuilt() const;

  // Returns if this blob is broken, and there is no data associated with it.
  // Must be called on IO thread.
  bool IsBroken() const;

  // The callback will be run on the IO thread when construction of the blob
  // is complete. If construction is already complete, then the task is run
  // immediately on the current message loop (i.e. IO thread).
  // Must be called on IO thread.  Returns if construction successful.
  // Calling this multiple times results in registering multiple
  // completion callbacks.
  void RunOnConstructionComplete(const base::Callback<void(bool)>& done);

  // A BlobReader is used to read the data from the blob.  This object is
  // intended to be transient and should not be stored for any extended period
  // of time.
  std::unique_ptr<BlobReader> CreateReader(
      FileSystemContext* file_system_context,
      base::SequencedTaskRunner* file_task_runner) const;

  // May be accessed on any thread.
  const std::string& uuid() const;
  // May be accessed on any thread.
  const std::string& content_type() const;
  // May be accessed on any thread.
  const std::string& content_disposition() const;

  // This call and the destruction of the returned snapshot must be called
  // on the IO thread. If the blob is broken, then we return a nullptr here.
  // Please do not call this, and use CreateReader instead. It appropriately
  // waits until the blob is built before having a size (see CalculateSize).
  // TODO(dmurph): Make this protected, where only the BlobReader can call it.
  std::unique_ptr<BlobDataSnapshot> CreateSnapshot() const;

 private:
  // Internal class whose destructor is guarenteed to be called on the IO
  // thread.
  class BlobDataHandleShared
      : public base::RefCountedThreadSafe<BlobDataHandleShared> {
   public:
    BlobDataHandleShared(const std::string& uuid,
                         const std::string& content_type,
                         const std::string& content_disposition,
                         BlobStorageContext* context);

    void RunOnConstructionComplete(const base::Callback<void(bool)>& done);

    std::unique_ptr<BlobDataSnapshot> CreateSnapshot() const;

   private:
    friend class base::DeleteHelper<BlobDataHandleShared>;
    friend class base::RefCountedThreadSafe<BlobDataHandleShared>;
    friend class BlobDataHandle;

    virtual ~BlobDataHandleShared();

    const std::string uuid_;
    const std::string content_type_;
    const std::string content_disposition_;
    base::WeakPtr<BlobStorageContext> context_;

    DISALLOW_COPY_AND_ASSIGN(BlobDataHandleShared);
  };

  friend class BlobStorageContext;
  BlobDataHandle(const std::string& uuid,
                 const std::string& content_type,
                 const std::string& content_disposition,
                 BlobStorageContext* context,
                 base::SequencedTaskRunner* io_task_runner);

  scoped_refptr<base::SequencedTaskRunner> io_task_runner_;
  scoped_refptr<BlobDataHandleShared> shared_;
};

}  // namespace storage

#endif  // STORAGE_BROWSER_BLOB_BLOB_DATA_HANDLE_H_
