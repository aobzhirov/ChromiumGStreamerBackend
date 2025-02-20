// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BROWSER_BLOB_BLOB_READER_H_
#define STORAGE_BROWSER_BLOB_BLOB_READER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_callback.h"
#include "storage/browser/storage_browser_export.h"

class GURL;

namespace base {
class FilePath;
class SequencedTaskRunner;
class TaskRunner;
class Time;
}

namespace net {
class DrainableIOBuffer;
class IOBuffer;
}

namespace storage {
class BlobDataItem;
class BlobDataHandle;
class BlobDataSnapshot;
class FileStreamReader;
class FileSystemContext;

// The blob reader is used to read a blob.  This can only be used in the browser
// process, and we need to be on the IO thread.
//  * There can only be one read happening at a time per reader.
//  * If a status of Status::NET_ERROR is returned, that means there was an
//    error and the net_error() variable contains the error code.
// Use a BlobDataHandle to create an instance.
class STORAGE_EXPORT BlobReader {
 public:
  class STORAGE_EXPORT FileStreamReaderProvider {
   public:
    virtual ~FileStreamReaderProvider();

    virtual std::unique_ptr<FileStreamReader> CreateForLocalFile(
        base::TaskRunner* task_runner,
        const base::FilePath& file_path,
        int64_t initial_offset,
        const base::Time& expected_modification_time) = 0;

    virtual std::unique_ptr<FileStreamReader> CreateFileStreamReader(
        const GURL& filesystem_url,
        int64_t offset,
        int64_t max_bytes_to_read,
        const base::Time& expected_modification_time) = 0;
  };
  enum class Status { NET_ERROR, IO_PENDING, DONE };
  virtual ~BlobReader();

  // This calculates the total size of the blob, and initializes the reading
  // cursor.
  //  * This should only be called once per reader.
  //  * Status::Done means that the total_size() value is populated and you can
  //    continue to SetReadRange or Read.
  //  * The 'done' callback is only called if Status::IO_PENDING is returned.
  //    The callback value contains the error code or net::OK. Please use the
  //    total_size() value to query the blob size, as it's uint64_t.
  Status CalculateSize(const net::CompletionCallback& done);

  // Used to set the read position.
  // * This should be called after CalculateSize and before Read.
  // * Range can only be set once.
  Status SetReadRange(uint64_t position, uint64_t length);

  // Reads a portion of the data.
  // * CalculateSize (and optionally SetReadRange) must be called beforehand.
  // * bytes_read is populated only if Status::DONE is returned. Otherwise the
  //   bytes read (or error code) is populated in the 'done' callback.
  // * The done callback is only called if Status::IO_PENDING is returned.
  // * This method can be called multiple times. A bytes_read value (either from
  //   the callback for Status::IO_PENDING or the bytes_read value for
  //   Status::DONE) of 0 means we're finished reading.
  Status Read(net::IOBuffer* buffer,
              size_t dest_size,
              int* bytes_read,
              net::CompletionCallback done);

  // Kills reading and invalidates all callbacks. The reader cannot be used
  // after this call.
  void Kill();

  // Returns if all of the blob's items are in memory.
  bool IsInMemory() const;

  // Returns the remaining bytes to be read in the blob. This is populated
  // after CalculateSize, and is modified by SetReadRange.
  uint64_t remaining_bytes() const { return remaining_bytes_; }

  // Returns the net error code if there was an error. Defaults to net::OK.
  int net_error() const { return net_error_; }

  // Returns the total size of the blob. This is populated after CalculateSize
  // is called.
  uint64_t total_size() const {
    DCHECK(total_size_calculated_);
    return total_size_;
  }

 protected:
  friend class BlobDataHandle;
  friend class BlobReaderTest;
  FRIEND_TEST_ALL_PREFIXES(BlobReaderTest, HandleBeforeAsyncCancel);
  FRIEND_TEST_ALL_PREFIXES(BlobReaderTest, ReadFromIncompleteBlob);

  BlobReader(const BlobDataHandle* blob_handle,
             std::unique_ptr<FileStreamReaderProvider> file_stream_provider,
             base::SequencedTaskRunner* file_task_runner);

  bool total_size_calculated() const { return total_size_calculated_; }

 private:
  Status ReportError(int net_error);
  void InvalidateCallbacksAndDone(int net_error, net::CompletionCallback done);

  void AsyncCalculateSize(const net::CompletionCallback& done,
                          bool async_succeeded);
  Status CalculateSizeImpl(const net::CompletionCallback& done);
  bool AddItemLength(size_t index, uint64_t length);
  bool ResolveFileItemLength(const BlobDataItem& item,
                             int64_t total_length,
                             uint64_t* output_length);
  void DidGetFileItemLength(size_t index, int64_t result);
  void DidCountSize();

  // For reading the blob.
  // Returns if we're done, PENDING_IO if we're waiting on async.
  Status ReadLoop(int* bytes_read);
  // Called from asynchronously called methods to continue the read loop.
  void ContinueAsyncReadLoop();
  // PENDING_IO means we're waiting on async.
  Status ReadItem();
  void AdvanceItem();
  void AdvanceBytesRead(int result);
  void ReadBytesItem(const BlobDataItem& item, int bytes_to_read);
  BlobReader::Status ReadFileItem(FileStreamReader* reader, int bytes_to_read);
  void DidReadFile(int result);
  void DeleteCurrentFileReader();
  Status ReadDiskCacheEntryItem(const BlobDataItem& item, int bytes_to_read);
  void DidReadDiskCacheEntry(int result);
  void DidReadItem(int result);
  int ComputeBytesToRead() const;
  int BytesReadCompleted();

  // Returns a FileStreamReader for a blob item at |index|.
  // If the item at |index| is not of file this returns NULL.
  FileStreamReader* GetOrCreateFileReaderAtIndex(size_t index);
  // If the reader is null, then this basically performs a delete operation.
  void SetFileReaderAtIndex(size_t index,
                            std::unique_ptr<FileStreamReader> reader);
  // Creates a FileStreamReader for the item with additional_offset.
  std::unique_ptr<FileStreamReader> CreateFileStreamReader(
      const BlobDataItem& item,
      uint64_t additional_offset);

  std::unique_ptr<BlobDataHandle> blob_handle_;
  std::unique_ptr<BlobDataSnapshot> blob_data_;
  std::unique_ptr<FileStreamReaderProvider> file_stream_provider_;
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;

  int net_error_;
  bool item_list_populated_ = false;
  std::vector<uint64_t> item_length_list_;

  scoped_refptr<net::DrainableIOBuffer> read_buf_;

  bool total_size_calculated_ = false;
  uint64_t total_size_ = 0;
  uint64_t remaining_bytes_ = 0;
  size_t pending_get_file_info_count_ = 0;
  std::map<size_t, FileStreamReader*> index_to_reader_;
  size_t current_item_index_ = 0;
  uint64_t current_item_offset_ = 0;

  bool io_pending_ = false;

  net::CompletionCallback size_callback_;
  net::CompletionCallback read_callback_;

  base::WeakPtrFactory<BlobReader> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(BlobReader);
};

}  // namespace storage
#endif  // STORAGE_BROWSER_BLOB_BLOB_READER_H_
