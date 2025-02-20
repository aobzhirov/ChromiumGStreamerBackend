// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_backend/metadata_db_migration_util.h"

#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_backend_constants.h"
#include "chrome/browser/sync_file_system/syncable_file_system_util.h"
#include "storage/browser/fileapi/external_mount_points.h"
#include "storage/common/fileapi/file_system_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/leveldatabase/src/include/leveldb/write_batch.h"
#include "url/gurl.h"

#define FPL FILE_PATH_LITERAL

namespace sync_file_system {
namespace drive_backend {

namespace {

void VerifyKeyAndValue(const std::string& key,
                       const std::string& expect_val,
                       leveldb::DB* db) {
  scoped_ptr<leveldb::Iterator> itr(db->NewIterator(leveldb::ReadOptions()));

  itr->Seek(key);
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ(expect_val, itr->value().ToString());
}

void VerifyNotExist(const std::string& key, leveldb::DB* db) {
  scoped_ptr<leveldb::Iterator> itr(db->NewIterator(leveldb::ReadOptions()));

  itr->Seek(key);
  EXPECT_TRUE(!itr->Valid() ||
              !base::StartsWith(itr->key().ToString(), key,
                                base::CompareCase::SENSITIVE));
}

}  // namespace

TEST(DriveMetadataDBMigrationUtilTest, RollbackFromV4ToV3) {
  // Rollback from version 4 to version 3.
  // Please see metadata_database_index.cc for version 3 format, and
  // metadata_database_index_on_disk.cc for version 4 format.

  const char kDatabaseVersionKey[] = "VERSION";
  const char kServiceMetadataKey[] = "SERVICE";
  const char kFileMetadataKeyPrefix[] = "FILE: ";
  const char kFileTrackerKeyPrefix[] = "TRACKER: ";

  // Key prefixes used in version 4.
  const char kAppRootIDByAppIDKeyPrefix[] = "APP_ROOT: ";
  const char kActiveTrackerIDByFileIDKeyPrefix[] = "ACTIVE_FILE: ";
  const char kTrackerIDByFileIDKeyPrefix[] = "TRACKER_FILE: ";
  const char kMultiTrackerByFileIDKeyPrefix[] = "MULTI_FILE: ";
  const char kActiveTrackerIDByParentAndTitleKeyPrefix[] = "ACTIVE_PATH: ";
  const char kTrackerIDByParentAndTitleKeyPrefix[] = "TRACKER_PATH: ";
  const char kMultiBackingParentAndTitleKeyPrefix[] = "MULTI_PATH: ";
  const char kDirtyIDKeyPrefix[] = "DIRTY: ";
  const char kDemotedDirtyIDKeyPrefix[] = "DEMOTED_DIRTY: ";

  // Set up environment.
  leveldb::DB* db_ptr = nullptr;
  base::ScopedTempDir base_dir;
  ASSERT_TRUE(base_dir.CreateUniqueTempDir());
  {
    leveldb::Options options;
    options.create_if_missing = true;
    std::string db_dir =
        storage::FilePathToString(base_dir.path().Append(kDatabaseName));
    leveldb::Status status = leveldb::DB::Open(options, db_dir, &db_ptr);
    ASSERT_TRUE(status.ok());
  }
  scoped_ptr<leveldb::DB> db(db_ptr);

  // Setup the database with the schema version 4, without IDs.
  leveldb::WriteBatch batch;
  batch.Put(kDatabaseVersionKey, "4");
  batch.Put(kServiceMetadataKey, "service_metadata");
  batch.Put(kFileMetadataKeyPrefix, "file_metadata");
  batch.Put(kFileTrackerKeyPrefix, "file_tracker");
  batch.Put(kAppRootIDByAppIDKeyPrefix, "app_root_id");
  batch.Put(kActiveTrackerIDByFileIDKeyPrefix, "active_id_by_file");
  batch.Put(kTrackerIDByFileIDKeyPrefix, "tracker_id_by_file");
  batch.Put(kMultiTrackerByFileIDKeyPrefix, "multi_tracker_by_file");
  batch.Put(kActiveTrackerIDByParentAndTitleKeyPrefix, "active_id_by_path");
  batch.Put(kTrackerIDByParentAndTitleKeyPrefix, "tracker_id_by_path");
  batch.Put(kMultiBackingParentAndTitleKeyPrefix, "multi_tracker_by_path");
  batch.Put(kDirtyIDKeyPrefix, "dirty");
  batch.Put(kDemotedDirtyIDKeyPrefix, "demoted_dirty");

  leveldb::Status status = db->Write(leveldb::WriteOptions(), &batch);
  EXPECT_EQ(SYNC_STATUS_OK, LevelDBStatusToSyncStatusCode(status));

  // Migrate the database.
  MigrateDatabaseFromV4ToV3(db.get());

  // Verify DB schema verison
  VerifyKeyAndValue(kDatabaseVersionKey, "3", db.get());

  // Verify remained entries.
  VerifyKeyAndValue(kServiceMetadataKey, "service_metadata", db.get());
  VerifyKeyAndValue(kFileMetadataKeyPrefix, "file_metadata", db.get());
  VerifyKeyAndValue(kFileTrackerKeyPrefix, "file_tracker", db.get());

  // Verify
  VerifyNotExist(kAppRootIDByAppIDKeyPrefix, db.get());
  VerifyNotExist(kActiveTrackerIDByFileIDKeyPrefix, db.get());
  VerifyNotExist(kTrackerIDByFileIDKeyPrefix, db.get());
  VerifyNotExist(kMultiTrackerByFileIDKeyPrefix, db.get());
  VerifyNotExist(kActiveTrackerIDByParentAndTitleKeyPrefix, db.get());
  VerifyNotExist(kTrackerIDByParentAndTitleKeyPrefix, db.get());
  VerifyNotExist(kMultiBackingParentAndTitleKeyPrefix, db.get());
  VerifyNotExist(kDirtyIDKeyPrefix, db.get());
  VerifyNotExist(kDemotedDirtyIDKeyPrefix, db.get());
}

}  // namespace drive_backend
}  // namespace sync_file_system
