// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/local/sync_file_system_backend.h"

#include <string>
#include <utility>

#include "base/logging.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/sync_file_system/local/local_file_change_tracker.h"
#include "chrome/browser/sync_file_system/local/local_file_sync_context.h"
#include "chrome/browser/sync_file_system/local/syncable_file_system_operation.h"
#include "chrome/browser/sync_file_system/sync_file_system_service.h"
#include "chrome/browser/sync_file_system/sync_file_system_service_factory.h"
#include "chrome/browser/sync_file_system/syncable_file_system_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "storage/browser/fileapi/file_stream_reader.h"
#include "storage/browser/fileapi/file_stream_writer.h"
#include "storage/browser/fileapi/file_system_context.h"
#include "storage/browser/fileapi/file_system_operation.h"
#include "storage/common/fileapi/file_system_util.h"

using content::BrowserThread;

namespace sync_file_system {

namespace {

bool CalledOnUIThread() {
  // Ensure that these methods are called on the UI thread, except for unittests
  // where a UI thread might not have been created.
  return BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         !BrowserThread::IsMessageLoopValid(BrowserThread::UI);
}

}  // namespace

SyncFileSystemBackend::ProfileHolder::ProfileHolder(Profile* profile)
    : profile_(profile) {
  DCHECK(CalledOnUIThread());
  registrar_.Add(this,
                 chrome::NOTIFICATION_PROFILE_DESTROYED,
                 content::Source<Profile>(profile_));
}

void SyncFileSystemBackend::ProfileHolder::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(CalledOnUIThread());
  DCHECK_EQ(chrome::NOTIFICATION_PROFILE_DESTROYED, type);
  DCHECK_EQ(profile_, content::Source<Profile>(source).ptr());
  profile_ = nullptr;
  registrar_.RemoveAll();
}

Profile* SyncFileSystemBackend::ProfileHolder::GetProfile() {
  DCHECK(CalledOnUIThread());
  return profile_;
}

SyncFileSystemBackend::SyncFileSystemBackend(Profile* profile)
    : context_(nullptr),
      skip_initialize_syncfs_service_for_testing_(false) {
  DCHECK(CalledOnUIThread());
  if (profile)
    profile_holder_.reset(new ProfileHolder(profile));

  // Register the service name here to enable to crack an URL on SyncFileSystem
  // even if SyncFileSystemService has not started yet.
  RegisterSyncableFileSystem();
}

SyncFileSystemBackend::~SyncFileSystemBackend() {
  if (change_tracker_) {
    GetDelegate()->file_task_runner()->DeleteSoon(
        FROM_HERE, change_tracker_.release());
  }

  if (profile_holder_ && !CalledOnUIThread()) {
    BrowserThread::DeleteSoon(
        BrowserThread::UI, FROM_HERE, profile_holder_.release());
  }
}

// static
SyncFileSystemBackend* SyncFileSystemBackend::CreateForTesting() {
  DCHECK(CalledOnUIThread());
  SyncFileSystemBackend* backend = new SyncFileSystemBackend(nullptr);
  backend->skip_initialize_syncfs_service_for_testing_ = true;
  return backend;
}

bool SyncFileSystemBackend::CanHandleType(storage::FileSystemType type) const {
  return type == storage::kFileSystemTypeSyncable ||
         type == storage::kFileSystemTypeSyncableForInternalSync;
}

void SyncFileSystemBackend::Initialize(storage::FileSystemContext* context) {
  DCHECK(context);
  DCHECK(!context_);
  context_ = context;

  storage::SandboxFileSystemBackendDelegate* delegate = GetDelegate();
  delegate->RegisterQuotaUpdateObserver(storage::kFileSystemTypeSyncable);
  delegate->RegisterQuotaUpdateObserver(
      storage::kFileSystemTypeSyncableForInternalSync);
}

void SyncFileSystemBackend::ResolveURL(const storage::FileSystemURL& url,
                                       storage::OpenFileSystemMode mode,
                                       const OpenFileSystemCallback& callback) {
  DCHECK(CanHandleType(url.type()));

  if (skip_initialize_syncfs_service_for_testing_) {
    GetDelegate()->OpenFileSystem(url.origin(),
                                  url.type(),
                                  mode,
                                  callback,
                                  GetSyncableFileSystemRootURI(url.origin()));
    return;
  }

  // It is safe to pass Unretained(this) since |context_| owns it.
  SyncStatusCallback initialize_callback =
      base::Bind(&SyncFileSystemBackend::DidInitializeSyncFileSystemService,
                 base::Unretained(this), base::RetainedRef(context_),
                 url.origin(), url.type(), mode, callback);
  InitializeSyncFileSystemService(url.origin(), initialize_callback);
}

storage::AsyncFileUtil* SyncFileSystemBackend::GetAsyncFileUtil(
    storage::FileSystemType type) {
  return GetDelegate()->file_util();
}

storage::WatcherManager* SyncFileSystemBackend::GetWatcherManager(
    storage::FileSystemType type) {
  return nullptr;
}

storage::CopyOrMoveFileValidatorFactory*
SyncFileSystemBackend::GetCopyOrMoveFileValidatorFactory(
    storage::FileSystemType type,
    base::File::Error* error_code) {
  DCHECK(error_code);
  *error_code = base::File::FILE_OK;
  return nullptr;
}

storage::FileSystemOperation* SyncFileSystemBackend::CreateFileSystemOperation(
    const storage::FileSystemURL& url,
    storage::FileSystemContext* context,
    base::File::Error* error_code) const {
  DCHECK(CanHandleType(url.type()));
  DCHECK(context);
  DCHECK(error_code);

  scoped_ptr<storage::FileSystemOperationContext> operation_context =
      GetDelegate()->CreateFileSystemOperationContext(url, context, error_code);
  if (!operation_context)
    return nullptr;

  if (url.type() == storage::kFileSystemTypeSyncableForInternalSync) {
    return storage::FileSystemOperation::Create(url, context,
                                                std::move(operation_context));
  }

  return new SyncableFileSystemOperation(url, context,
                                         std::move(operation_context));
}

bool SyncFileSystemBackend::SupportsStreaming(
    const storage::FileSystemURL& url) const {
  return false;
}

bool SyncFileSystemBackend::HasInplaceCopyImplementation(
    storage::FileSystemType type) const {
  return false;
}

scoped_ptr<storage::FileStreamReader>
SyncFileSystemBackend::CreateFileStreamReader(
    const storage::FileSystemURL& url,
    int64_t offset,
    int64_t max_bytes_to_read,
    const base::Time& expected_modification_time,
    storage::FileSystemContext* context) const {
  DCHECK(CanHandleType(url.type()));
  return GetDelegate()->CreateFileStreamReader(
      url, offset, expected_modification_time, context);
}

scoped_ptr<storage::FileStreamWriter>
SyncFileSystemBackend::CreateFileStreamWriter(
    const storage::FileSystemURL& url,
    int64_t offset,
    storage::FileSystemContext* context) const {
  DCHECK(CanHandleType(url.type()));
  return GetDelegate()->CreateFileStreamWriter(
      url, offset, context, storage::kFileSystemTypeSyncableForInternalSync);
}

storage::FileSystemQuotaUtil* SyncFileSystemBackend::GetQuotaUtil() {
  return GetDelegate();
}

const storage::UpdateObserverList* SyncFileSystemBackend::GetUpdateObservers(
    storage::FileSystemType type) const {
  return GetDelegate()->GetUpdateObservers(type);
}

const storage::ChangeObserverList* SyncFileSystemBackend::GetChangeObservers(
    storage::FileSystemType type) const {
  return GetDelegate()->GetChangeObservers(type);
}

const storage::AccessObserverList* SyncFileSystemBackend::GetAccessObservers(
    storage::FileSystemType type) const {
  return GetDelegate()->GetAccessObservers(type);
}

// static
SyncFileSystemBackend* SyncFileSystemBackend::GetBackend(
    const storage::FileSystemContext* file_system_context) {
  DCHECK(file_system_context);
  return static_cast<SyncFileSystemBackend*>(
      file_system_context->GetFileSystemBackend(
          storage::kFileSystemTypeSyncable));
}

void SyncFileSystemBackend::SetLocalFileChangeTracker(
    scoped_ptr<LocalFileChangeTracker> tracker) {
  DCHECK(!change_tracker_);
  DCHECK(tracker);
  change_tracker_ = std::move(tracker);

  storage::SandboxFileSystemBackendDelegate* delegate = GetDelegate();
  delegate->AddFileUpdateObserver(storage::kFileSystemTypeSyncable,
                                  change_tracker_.get(),
                                  delegate->file_task_runner());
  delegate->AddFileChangeObserver(storage::kFileSystemTypeSyncable,
                                  change_tracker_.get(),
                                  delegate->file_task_runner());
}

void SyncFileSystemBackend::set_sync_context(
    LocalFileSyncContext* sync_context) {
  DCHECK(!sync_context_.get());
  sync_context_ = sync_context;
}

storage::SandboxFileSystemBackendDelegate* SyncFileSystemBackend::GetDelegate()
    const {
  DCHECK(context_);
  DCHECK(context_->sandbox_delegate());
  return context_->sandbox_delegate();
}

void SyncFileSystemBackend::InitializeSyncFileSystemService(
    const GURL& origin_url,
    const SyncStatusCallback& callback) {
  // Repost to switch from IO thread to UI thread.
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    // It is safe to pass Unretained(this) (see comments in OpenFileSystem()).
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&SyncFileSystemBackend::InitializeSyncFileSystemService,
                   base::Unretained(this), origin_url, callback));
    return;
  }

  if (!profile_holder_->GetProfile()) {
    // Profile was destroyed.
    callback.Run(SYNC_FILE_ERROR_FAILED);
    return;
  }

  SyncFileSystemService* service = SyncFileSystemServiceFactory::GetForProfile(
          profile_holder_->GetProfile());
  DCHECK(service);
  service->InitializeForApp(context_, origin_url, callback);
}

void SyncFileSystemBackend::DidInitializeSyncFileSystemService(
    storage::FileSystemContext* context,
    const GURL& origin_url,
    storage::FileSystemType type,
    storage::OpenFileSystemMode mode,
    const OpenFileSystemCallback& callback,
    SyncStatusCode status) {
  // Repost to switch from UI thread to IO thread.
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    // It is safe to pass Unretained(this) since |context| owns it.
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&SyncFileSystemBackend::DidInitializeSyncFileSystemService,
                   base::Unretained(this), base::RetainedRef(context),
                   origin_url, type, mode, callback, status));
    return;
  }

  if (status != sync_file_system::SYNC_STATUS_OK) {
    callback.Run(GURL(), std::string(),
                 SyncStatusCodeToFileError(status));
    return;
  }

  callback.Run(GetSyncableFileSystemRootURI(origin_url),
               GetFileSystemName(origin_url, type),
               base::File::FILE_OK);
}

}  // namespace sync_file_system
