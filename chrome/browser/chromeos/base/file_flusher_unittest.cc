// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/base/file_flusher.h"

#include <map>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

void WriteStringToFile(const base::FilePath path, const std::string& data) {
  ASSERT_TRUE(base::CreateDirectory(path.DirName()))
      << "Failed to create directory " << path.DirName().value();

  int size = data.size();
  ASSERT_TRUE(base::WriteFile(path, data.c_str(), size) == size)
      << "Failed to write " << path.value();
}

}  // namespace

// Provide basic sanity test of the FileFlusher. Note it only tests that
// flush is called for the expected files but not testing the underlying
// file system for actually persisting the data.
class FileFlusherTest : public testing::Test {
 public:
  FileFlusherTest() {}
  ~FileFlusherTest() override {}

  // testing::Test
  void SetUp() override {
    // Create test files under a temp dir.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    const size_t kNumDirs = 2;
    const size_t kNumFiles = 3;
    for (size_t i = 1; i <= kNumDirs; ++i) {
      for (size_t j = 1; j <= kNumFiles; ++j) {
        const std::string path = base::StringPrintf("dir%zu/file%zu", i, j);
        const std::string content = base::StringPrintf("content %zu %zu", i, j);
        WriteStringToFile(temp_dir_.path().AppendASCII(path), content);
      }
    }
  }

  void TearDown() override {
    content::BrowserThread::GetBlockingPool()->FlushForTesting();
    base::RunLoop().RunUntilIdle();
  }

  scoped_ptr<FileFlusher> CreateFileFlusher() {
    scoped_ptr<FileFlusher> flusher(new FileFlusher);
    flusher->set_on_flush_callback_for_test(
        base::Bind(&FileFlusherTest::OnFlush, base::Unretained(this)));
    return flusher;
  }

  base::FilePath GetTestFilePath(const std::string& path_string) const {
    const base::FilePath path = base::FilePath::FromUTF8Unsafe(path_string);
    if (path.IsAbsolute())
      return path;

    return temp_dir_.path().Append(path);
  }

  void OnFlush(const base::FilePath& path) { ++flush_counts_[path]; }

  int GetFlushCount(const std::string& path_string) const {
    const base::FilePath path(GetTestFilePath(path_string));
    const auto& it = flush_counts_.find(path);
    return it == flush_counts_.end() ? 0 : it->second;
  }

 private:
  content::TestBrowserThreadBundle thread_bundle_;
  base::ScopedTempDir temp_dir_;
  std::map<base::FilePath, int> flush_counts_;

  DISALLOW_COPY_AND_ASSIGN(FileFlusherTest);
};

TEST_F(FileFlusherTest, Flush) {
  scoped_ptr<FileFlusher> flusher(CreateFileFlusher());
  base::RunLoop run_loop;
  flusher->RequestFlush(GetTestFilePath("dir1"), std::vector<base::FilePath>(),
                        base::Closure());
  flusher->RequestFlush(GetTestFilePath("dir2"), std::vector<base::FilePath>(),
                        run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_EQ(1, GetFlushCount("dir1/file1"));
  EXPECT_EQ(1, GetFlushCount("dir1/file2"));
  EXPECT_EQ(1, GetFlushCount("dir1/file3"));

  EXPECT_EQ(1, GetFlushCount("dir2/file1"));
  EXPECT_EQ(1, GetFlushCount("dir2/file2"));
  EXPECT_EQ(1, GetFlushCount("dir2/file3"));
}

TEST_F(FileFlusherTest, Exclude) {
  scoped_ptr<FileFlusher> flusher(CreateFileFlusher());

  std::vector<base::FilePath> excludes;
  // Relative exclude
  excludes.push_back(base::FilePath::FromUTF8Unsafe("file1"));
  // Absolute exclude
  excludes.push_back(GetTestFilePath("dir1/file2"));
  // Invalid exclude will be ignored.
  excludes.push_back(base::FilePath::FromUTF8Unsafe("Bad file"));

  base::RunLoop run_loop;
  flusher->RequestFlush(GetTestFilePath("dir1"), excludes,
                        run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_EQ(0, GetFlushCount("dir1/file1"));
  EXPECT_EQ(0, GetFlushCount("dir1/file2"));
  EXPECT_EQ(1, GetFlushCount("dir1/file3"));

  EXPECT_EQ(0, GetFlushCount("dir1/Bad file"));
}

TEST_F(FileFlusherTest, DuplicateRequests) {
  scoped_ptr<FileFlusher> flusher(CreateFileFlusher());
  base::RunLoop run_loop;
  flusher->RequestFlush(GetTestFilePath("dir1"), std::vector<base::FilePath>(),
                        base::Closure());
  flusher->RequestFlush(GetTestFilePath("dir1"), std::vector<base::FilePath>(),
                        run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_EQ(1, GetFlushCount("dir1/file1"));
  EXPECT_EQ(1, GetFlushCount("dir1/file2"));
  EXPECT_EQ(1, GetFlushCount("dir1/file3"));
}

}  // namespace chromeos
