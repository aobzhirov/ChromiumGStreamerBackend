// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/value_store/lazy_leveldb.h"

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/iterator.h"
#include "third_party/leveldatabase/src/include/leveldb/write_batch.h"

using base::StringPiece;
using content::BrowserThread;

namespace {

const char kInvalidJson[] = "Invalid JSON";
const char kRestoredDuringOpen[] = "Database corruption repaired during open";

// UMA values used when recovering from a corrupted leveldb.
// Do not change/delete these values as you will break reporting for older
// copies of Chrome. Only add new values to the end.
enum LevelDBDatabaseCorruptionRecoveryValue {
  LEVELDB_DB_RESTORE_DELETE_SUCCESS = 0,
  LEVELDB_DB_RESTORE_DELETE_FAILURE,
  LEVELDB_DB_RESTORE_REPAIR_SUCCESS,
  LEVELDB_DB_RESTORE_MAX
};

// UMA values used when recovering from a corrupted leveldb.
// Do not change/delete these values as you will break reporting for older
// copies of Chrome. Only add new values to the end.
enum LevelDBValueCorruptionRecoveryValue {
  LEVELDB_VALUE_RESTORE_DELETE_SUCCESS,
  LEVELDB_VALUE_RESTORE_DELETE_FAILURE,
  LEVELDB_VALUE_RESTORE_MAX
};

ValueStore::StatusCode LevelDbToValueStoreStatusCode(
    const leveldb::Status& status) {
  if (status.ok())
    return ValueStore::OK;
  if (status.IsCorruption())
    return ValueStore::CORRUPTION;
  return ValueStore::OTHER_ERROR;
}

}  // namespace

LazyLevelDb::LazyLevelDb(const std::string& uma_client_name,
                         const base::FilePath& path)
    : db_path_(path) {
  open_options_.create_if_missing = true;
  open_options_.paranoid_checks = true;
  open_options_.reuse_logs = leveldb_env::kDefaultLogReuseOptionValue;

  read_options_.verify_checksums = true;

  // Used in lieu of UMA_HISTOGRAM_ENUMERATION because the histogram name is
  // not a constant.
  open_histogram_ = base::LinearHistogram::FactoryGet(
      "Extensions.Database.Open." + uma_client_name, 1,
      leveldb_env::LEVELDB_STATUS_MAX, leveldb_env::LEVELDB_STATUS_MAX + 1,
      base::Histogram::kUmaTargetedHistogramFlag);
  db_restore_histogram_ = base::LinearHistogram::FactoryGet(
      "Extensions.Database.Database.Restore." + uma_client_name, 1,
      LEVELDB_DB_RESTORE_MAX, LEVELDB_DB_RESTORE_MAX + 1,
      base::Histogram::kUmaTargetedHistogramFlag);
  value_restore_histogram_ = base::LinearHistogram::FactoryGet(
      "Extensions.Database.Value.Restore." + uma_client_name, 1,
      LEVELDB_VALUE_RESTORE_MAX, LEVELDB_VALUE_RESTORE_MAX + 1,
      base::Histogram::kUmaTargetedHistogramFlag);
}

LazyLevelDb::~LazyLevelDb() {
  if (db_ && !BrowserThread::CurrentlyOn(BrowserThread::FILE))
    BrowserThread::DeleteSoon(BrowserThread::FILE, FROM_HERE, db_.release());
}

ValueStore::Status LazyLevelDb::Read(const std::string& key,
                                     scoped_ptr<base::Value>* value) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  DCHECK(value);

  std::string value_as_json;
  leveldb::Status s = db_->Get(read_options_, key, &value_as_json);

  if (s.IsNotFound()) {
    // Despite there being no value, it was still a success. Check this first
    // because ok() is false on IsNotFound.
    return ValueStore::Status();
  }

  if (!s.ok())
    return ToValueStoreError(s);

  scoped_ptr<base::Value> val = base::JSONReader().ReadToValue(value_as_json);
  if (!val)
    return ValueStore::Status(ValueStore::CORRUPTION, FixCorruption(&key),
                              kInvalidJson);

  *value = std::move(val);
  return ValueStore::Status();
}

leveldb::Status LazyLevelDb::Delete(const std::string& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  DCHECK(db_.get());

  leveldb::WriteBatch batch;
  batch.Delete(key);

  return db_->Write(leveldb::WriteOptions(), &batch);
}

ValueStore::BackingStoreRestoreStatus LazyLevelDb::LogRestoreStatus(
    ValueStore::BackingStoreRestoreStatus restore_status) const {
  switch (restore_status) {
    case ValueStore::RESTORE_NONE:
      NOTREACHED();
      break;
    case ValueStore::DB_RESTORE_DELETE_SUCCESS:
      db_restore_histogram_->Add(LEVELDB_DB_RESTORE_DELETE_SUCCESS);
      break;
    case ValueStore::DB_RESTORE_DELETE_FAILURE:
      db_restore_histogram_->Add(LEVELDB_DB_RESTORE_DELETE_FAILURE);
      break;
    case ValueStore::DB_RESTORE_REPAIR_SUCCESS:
      db_restore_histogram_->Add(LEVELDB_DB_RESTORE_REPAIR_SUCCESS);
      break;
    case ValueStore::VALUE_RESTORE_DELETE_SUCCESS:
      value_restore_histogram_->Add(LEVELDB_VALUE_RESTORE_DELETE_SUCCESS);
      break;
    case ValueStore::VALUE_RESTORE_DELETE_FAILURE:
      value_restore_histogram_->Add(LEVELDB_VALUE_RESTORE_DELETE_FAILURE);
      break;
  }
  return restore_status;
}

ValueStore::BackingStoreRestoreStatus LazyLevelDb::FixCorruption(
    const std::string* key) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  leveldb::Status s;
  if (key && db_) {
    s = Delete(*key);
    // Deleting involves writing to the log, so it's possible to have a
    // perfectly OK database but still have a delete fail.
    if (s.ok())
      return LogRestoreStatus(ValueStore::VALUE_RESTORE_DELETE_SUCCESS);
    else if (s.IsIOError())
      return LogRestoreStatus(ValueStore::VALUE_RESTORE_DELETE_FAILURE);
    // Any other kind of failure triggers a db repair.
  }

  // Make sure database is closed.
  db_.reset();

  // First try the less lossy repair.
  ValueStore::BackingStoreRestoreStatus restore_status =
      ValueStore::RESTORE_NONE;

  leveldb::Options repair_options;
  repair_options.create_if_missing = true;
  repair_options.paranoid_checks = true;

  // RepairDB can drop an unbounded number of leveldb tables (key/value sets).
  s = leveldb::RepairDB(db_path_.AsUTF8Unsafe(), repair_options);

  leveldb::DB* db = nullptr;
  if (s.ok()) {
    restore_status = ValueStore::DB_RESTORE_REPAIR_SUCCESS;
    s = leveldb::DB::Open(open_options_, db_path_.AsUTF8Unsafe(), &db);
  }

  if (!s.ok()) {
    if (DeleteDbFile()) {
      restore_status = ValueStore::DB_RESTORE_DELETE_SUCCESS;
      s = leveldb::DB::Open(open_options_, db_path_.AsUTF8Unsafe(), &db);
    } else {
      restore_status = ValueStore::DB_RESTORE_DELETE_FAILURE;
    }
  }

  if (s.ok())
    db_.reset(db);
  else
    db_unrecoverable_ = true;

  if (s.ok() && key) {
    s = Delete(*key);
    if (s.ok()) {
      restore_status = ValueStore::VALUE_RESTORE_DELETE_SUCCESS;
    } else if (s.IsIOError()) {
      restore_status = ValueStore::VALUE_RESTORE_DELETE_FAILURE;
    } else {
      db_.reset(db);
      if (!DeleteDbFile())
        db_unrecoverable_ = true;
      restore_status = ValueStore::DB_RESTORE_DELETE_FAILURE;
    }
  }

  // Only log for the final and most extreme form of database restoration.
  LogRestoreStatus(restore_status);

  return restore_status;
}

ValueStore::Status LazyLevelDb::EnsureDbIsOpen() {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  if (db_)
    return ValueStore::Status();

  if (db_unrecoverable_) {
    return ValueStore::Status(ValueStore::CORRUPTION,
                              ValueStore::DB_RESTORE_DELETE_FAILURE,
                              "Database corrupted");
  }

  leveldb::DB* db = nullptr;
  leveldb::Status ldb_status =
      leveldb::DB::Open(open_options_, db_path_.AsUTF8Unsafe(), &db);
  open_histogram_->Add(leveldb_env::GetLevelDBStatusUMAValue(ldb_status));
  ValueStore::Status status = ToValueStoreError(ldb_status);
  if (ldb_status.ok()) {
    db_.reset(db);
  } else if (ldb_status.IsCorruption()) {
    status.restore_status = FixCorruption(nullptr);
    if (status.restore_status != ValueStore::DB_RESTORE_DELETE_FAILURE) {
      status.code = ValueStore::OK;
      status.message = kRestoredDuringOpen;
    }
  }

  return status;
}

ValueStore::Status LazyLevelDb::ToValueStoreError(
    const leveldb::Status& status) {
  CHECK(!status.IsNotFound());  // not an error

  std::string message = status.ToString();
  // The message may contain |db_path_|, which may be considered sensitive
  // data, and those strings are passed to the extension, so strip it out.
  base::ReplaceSubstringsAfterOffset(&message, 0u, db_path_.AsUTF8Unsafe(),
                                     "...");

  return ValueStore::Status(LevelDbToValueStoreStatusCode(status), message);
}

bool LazyLevelDb::DeleteDbFile() {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  db_.reset();  // release any lock on the directory
  if (!base::DeleteFile(db_path_, true /* recursive */)) {
    LOG(WARNING) << "Failed to delete leveldb database at " << db_path_.value();
    return false;
  }
  return true;
}

scoped_ptr<leveldb::Iterator> LazyLevelDb::CreateIterator(
    const leveldb::ReadOptions& read_options) {
  if (!EnsureDbIsOpen().ok())
    return nullptr;
  return make_scoped_ptr(db_->NewIterator(read_options));
}
