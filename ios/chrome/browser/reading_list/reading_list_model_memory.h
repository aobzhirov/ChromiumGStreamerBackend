// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_READING_LIST_READING_LIST_MODEL_MEMORY_H_
#define IOS_CHROME_BROWSER_READING_LIST_READING_LIST_MODEL_MEMORY_H_

#include "base/memory/scoped_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "ios/chrome/browser/reading_list/reading_list_entry.h"
#include "ios/chrome/browser/reading_list/reading_list_model.h"

class ReadingListModelStorage;

// Concrete implementation of a reading list model using in memory lists.
class ReadingListModelMemory : public ReadingListModel, public KeyedService {
 public:
  // Initialize a ReadingListModelMemory to load and save data in
  // |persistence_layer|.
  ReadingListModelMemory(
      std::unique_ptr<ReadingListModelStorage> storage_layer);

  // Initialize a ReadingListModelMemory without persistence. Data will not be
  // persistent across sessions.
  ReadingListModelMemory();

  ~ReadingListModelMemory() override;
  void Shutdown() override;

  bool loaded() const override;

  size_t unread_size() const override;
  size_t read_size() const override;

  bool HasUnseenEntries() const override;
  void ResetUnseenEntries() override;

  // Returns a specific entry.
  const ReadingListEntry& GetUnreadEntryAtIndex(size_t index) const override;
  const ReadingListEntry& GetReadEntryAtIndex(size_t index) const override;

  void RemoveEntryByUrl(const GURL& url) override;

  const ReadingListEntry& AddEntry(const GURL& url,
                                   const std::string& title) override;

  void MarkReadByURL(const GURL& url) override;

 protected:
  void EndBatchUpdates() override;

 private:
  std::vector<ReadingListEntry> unread_;
  std::vector<ReadingListEntry> read_;
  std::unique_ptr<ReadingListModelStorage> storageLayer_;
  bool hasUnseen_;
  bool loaded_;
};

#endif  // IOS_CHROME_BROWSER_READING_LIST_READING_LIST_MODEL_MEMORY_H_
