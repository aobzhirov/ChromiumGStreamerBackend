// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_FILE_SYSTEM_PROVIDER_FILEAPI_PROVIDER_ASYNC_FILE_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_FILE_SYSTEM_PROVIDER_FILEAPI_PROVIDER_ASYNC_FILE_UTIL_H_

#include <stdint.h>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "storage/browser/fileapi/async_file_util.h"

namespace chromeos {
namespace file_system_provider {

class FileSystemInterface;

// TODO(mtomasz): Remove this namespace.
namespace internal {

// The implementation of storage::AsyncFileUtil for provided file systems. It is
// created one per Chrome process. It is responsible for routing calls to the
// correct profile, and then to the correct profided file system.
//
// This class should be called AsyncFileUtil, without the Provided prefix. This
// is impossible, though because of GYP limitations. There must not be two files
// with the same name in a Chromium tree.
// See: https://code.google.com/p/gyp/issues/detail?id=384
//
// All of the methods should be called on the IO thread.
class ProviderAsyncFileUtil : public storage::AsyncFileUtil {
 public:
  ProviderAsyncFileUtil();
  ~ProviderAsyncFileUtil() override;

  // storage::AsyncFileUtil overrides.
  void CreateOrOpen(scoped_ptr<storage::FileSystemOperationContext> context,
                    const storage::FileSystemURL& url,
                    int file_flags,
                    const CreateOrOpenCallback& callback) override;
  void EnsureFileExists(scoped_ptr<storage::FileSystemOperationContext> context,
                        const storage::FileSystemURL& url,
                        const EnsureFileExistsCallback& callback) override;
  void CreateDirectory(scoped_ptr<storage::FileSystemOperationContext> context,
                       const storage::FileSystemURL& url,
                       bool exclusive,
                       bool recursive,
                       const StatusCallback& callback) override;
  void GetFileInfo(scoped_ptr<storage::FileSystemOperationContext> context,
                   const storage::FileSystemURL& url,
                   int fields,
                   const GetFileInfoCallback& callback) override;
  void ReadDirectory(scoped_ptr<storage::FileSystemOperationContext> context,
                     const storage::FileSystemURL& url,
                     const ReadDirectoryCallback& callback) override;
  void Touch(scoped_ptr<storage::FileSystemOperationContext> context,
             const storage::FileSystemURL& url,
             const base::Time& last_access_time,
             const base::Time& last_modified_time,
             const StatusCallback& callback) override;
  void Truncate(scoped_ptr<storage::FileSystemOperationContext> context,
                const storage::FileSystemURL& url,
                int64_t length,
                const StatusCallback& callback) override;
  void CopyFileLocal(scoped_ptr<storage::FileSystemOperationContext> context,
                     const storage::FileSystemURL& src_url,
                     const storage::FileSystemURL& dest_url,
                     CopyOrMoveOption option,
                     const CopyFileProgressCallback& progress_callback,
                     const StatusCallback& callback) override;
  void MoveFileLocal(scoped_ptr<storage::FileSystemOperationContext> context,
                     const storage::FileSystemURL& src_url,
                     const storage::FileSystemURL& dest_url,
                     CopyOrMoveOption option,
                     const StatusCallback& callback) override;
  void CopyInForeignFile(
      scoped_ptr<storage::FileSystemOperationContext> context,
      const base::FilePath& src_file_path,
      const storage::FileSystemURL& dest_url,
      const StatusCallback& callback) override;
  void DeleteFile(scoped_ptr<storage::FileSystemOperationContext> context,
                  const storage::FileSystemURL& url,
                  const StatusCallback& callback) override;
  void DeleteDirectory(scoped_ptr<storage::FileSystemOperationContext> context,
                       const storage::FileSystemURL& url,
                       const StatusCallback& callback) override;
  void DeleteRecursively(
      scoped_ptr<storage::FileSystemOperationContext> context,
      const storage::FileSystemURL& url,
      const StatusCallback& callback) override;
  void CreateSnapshotFile(
      scoped_ptr<storage::FileSystemOperationContext> context,
      const storage::FileSystemURL& url,
      const CreateSnapshotFileCallback& callback) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProviderAsyncFileUtil);
};

}  // namespace internal
}  // namespace file_system_provider
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_FILE_SYSTEM_PROVIDER_FILEAPI_PROVIDER_ASYNC_FILE_UTIL_H_
