// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/download/download_manager_service.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/test/mock_download_item.h"
#include "content/public/test/mock_download_manager.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/origin.h"

using ::testing::_;

namespace content {
class BrowserContext;
class ByteStreamReader;
class DownloadManagerDelegate;
struct DownloadCreateInfo;
}

class DownloadManagerServiceTest : public testing::Test {
 public:
  DownloadManagerServiceTest()
      : service_(
            new DownloadManagerService(base::android::AttachCurrentThread(),
                                       nullptr,
                                       &manager_)),
        finished_(false),
        success_(false) {
    ON_CALL(manager_, GetDownloadByGuid(_))
        .WillByDefault(
            ::testing::Invoke(this,
                              &DownloadManagerServiceTest::GetDownloadByGuid));
  }

  void OnResumptionDone(bool success) {
    finished_ = true;
    success_ = success;
  }

  void StartDownload(const std::string& download_guid) {
    JNIEnv* env = base::android::AttachCurrentThread();
    service_->set_resume_callback_for_testing(base::Bind(
        &DownloadManagerServiceTest::OnResumptionDone, base::Unretained(this)));
    service_->ResumeDownload(
        env, nullptr,
        JavaParamRef<jstring>(
            env,
            base::android::ConvertUTF8ToJavaString(env, download_guid).obj()));
    EXPECT_FALSE(success_);
    service_->OnHistoryQueryComplete();
    while (!finished_)
      message_loop_.RunUntilIdle();
  }

  void CreateDownloadItem(bool can_resume) {
    download_.reset(new content::MockDownloadItem());
    ON_CALL(*download_, CanResume())
        .WillByDefault(::testing::Return(can_resume));
  }

 protected:
  content::DownloadItem* GetDownloadByGuid(const std::string&) {
    return download_.get();
  }

  base::MessageLoop message_loop_;
  scoped_ptr<content::MockDownloadItem> download_;
  content::MockDownloadManager manager_;
  DownloadManagerService* service_;
  bool finished_;
  bool success_;

  DISALLOW_COPY_AND_ASSIGN(DownloadManagerServiceTest);
};

// Test that resumption succeeds if the download item is found and can be
// resumed.
TEST_F(DownloadManagerServiceTest, ResumptionWithResumableItem) {
  CreateDownloadItem(true);
  StartDownload("0000");
  EXPECT_TRUE(success_);
}

// Test that resumption fails if the target download item is not resumable.
TEST_F(DownloadManagerServiceTest, ResumptionWithNonResumableItem) {
  CreateDownloadItem(false);
  StartDownload("0000");
  EXPECT_FALSE(success_);
}
