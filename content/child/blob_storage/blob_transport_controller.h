// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_CHILD_BLOB_STORAGE_BLOB_TRANSPORT_CONTROLLER_H_
#define CONTENT_CHILD_BLOB_STORAGE_BLOB_TRANSPORT_CONTROLLER_H_

#include <stddef.h>

#include <map>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/shared_memory_handle.h"
#include "content/common/content_export.h"
#include "ipc/ipc_platform_file.h"
#include "storage/common/blob_storage/blob_storage_constants.h"

namespace base {
template <typename T>
struct DefaultLazyInstanceTraits;
class SingleThreadTaskRunner;
}

namespace storage {
class DataElement;
struct BlobItemBytesRequest;
struct BlobItemBytesResponse;
}

namespace IPC {
class Sender;
}

namespace content {

class BlobConsolidation;

// This class is used to manage all the asynchronous transporation of blobs from
// the Renderer to the Browser process, where it's handling the Renderer side.
// The function of this class is to:
// * Be a lazy singleton,
// * hold all of the blob data that is being transported to the Browser process,
// * create the blob item 'descriptions' for the browser,
// * include shortcut data in the descriptions,
// * generate responses to blob memory requests, and
// * send IPC responses.
// Must be used on the IO thread.
class CONTENT_EXPORT BlobTransportController {
 public:
  static BlobTransportController* GetInstance();

  // This kicks off a blob transfer to the browser thread, which involves
  // sending an IPC message and storing the blob consolidation object.
  // If we have no pending blobs, we also call ChildProcess::AddRefProcess to
  // keep our process around while we transfer. This will be decremented when
  // we finish our last pending transfer (when our map is empty).
  void InitiateBlobTransfer(
      const std::string& uuid,
      scoped_ptr<BlobConsolidation> consolidation,
      IPC::Sender* sender,
      scoped_refptr<base::SingleThreadTaskRunner> main_runner);

  // This responds to the request using the sender.
  void OnMemoryRequest(
      const std::string& uuid,
      const std::vector<storage::BlobItemBytesRequest>& requests,
      std::vector<base::SharedMemoryHandle>* memory_handles,
      const std::vector<IPC::PlatformFileForTransit>& file_handles,
      IPC::Sender* sender);

  void OnCancel(const std::string& uuid,
                storage::IPCBlobCreationCancelCode code);

  void OnDone(const std::string& uuid);


  bool IsTransporting(const std::string& uuid) {
    return blob_storage_.find(uuid) != blob_storage_.end();
  }

  ~BlobTransportController();

 private:
  friend class BlobTransportControllerTest;
  FRIEND_TEST_ALL_PREFIXES(BlobTransportControllerTest, Descriptions);
  FRIEND_TEST_ALL_PREFIXES(BlobTransportControllerTest, Responses);
  FRIEND_TEST_ALL_PREFIXES(BlobTransportControllerTest, SharedMemory);
  FRIEND_TEST_ALL_PREFIXES(BlobTransportControllerTest, ResponsesErrors);

  // Clears all internal state. If our map wasn't previously empty, then we call
  // ChildProcess::ReleaseProcess to release our previous reference.
  void ClearForTesting();

  enum class ResponsesStatus {
    BLOB_NOT_FOUND,
    SHARED_MEMORY_MAP_FAILED,
    SUCCESS
  };
  friend struct base::DefaultLazyInstanceTraits<BlobTransportController>;

  BlobTransportController();

  void GetDescriptions(BlobConsolidation* consolidation,
                       size_t max_data_population,
                       std::vector<storage::DataElement>* out);

  ResponsesStatus GetResponses(
      const std::string& uuid,
      const std::vector<storage::BlobItemBytesRequest>& requests,
      std::vector<base::SharedMemoryHandle>* memory_handles,
      const std::vector<IPC::PlatformFileForTransit>& file_handles,
      std::vector<storage::BlobItemBytesResponse>* output);

  // Deletes the consolidation, and if we removed the last consolidation from
  // our map, we call ChildProcess::ReleaseProcess to release our previous
  // reference.
  void ReleaseBlobConsolidation(const std::string& uuid);

  scoped_refptr<base::SingleThreadTaskRunner> main_thread_runner_;
  std::map<std::string, scoped_ptr<BlobConsolidation>> blob_storage_;

  DISALLOW_COPY_AND_ASSIGN(BlobTransportController);
};

}  // namespace content
#endif  // CONTENT_CHILD_BLOB_STORAGE_BLOB_TRANSPORT_CONTROLLER_H_
