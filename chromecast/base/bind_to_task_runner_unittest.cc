// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/base/bind_to_task_runner.h"

#include <utility>

#include "base/memory/free_deleter.h"
#include "base/message_loop/message_loop.h"
#include "base/synchronization/waitable_event.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromecast {

void BoundBoolSet(bool* var, bool val) {
  *var = val;
}

void BoundBoolSetFromScopedPtr(bool* var, scoped_ptr<bool> val) {
  *var = *val;
}

void BoundBoolSetFromScopedPtrFreeDeleter(
    bool* var,
    scoped_ptr<bool, base::FreeDeleter> val) {
  *var = *val;
}

void BoundBoolSetFromScopedArray(bool* var, scoped_ptr<bool[]> val) {
  *var = val[0];
}

void BoundBoolSetFromConstRef(bool* var, const bool& val) {
  *var = val;
}

void BoundIntegersSet(int* a_var, int* b_var, int a_val, int b_val) {
  *a_var = a_val;
  *b_var = b_val;
}

// Various tests that check that the bound function is only actually executed
// on the message loop, not during the original Run.
class BindToTaskRunnerTest : public ::testing::Test {
 protected:
  base::MessageLoop loop_;
};

TEST_F(BindToTaskRunnerTest, Closure) {
  // Test the closure is run inside the loop, not outside it.
  base::WaitableEvent waiter(false, false);
  base::Closure cb = BindToCurrentThread(
      base::Bind(&base::WaitableEvent::Signal, Unretained(&waiter)));
  cb.Run();
  EXPECT_FALSE(waiter.IsSignaled());
  loop_.RunUntilIdle();
  EXPECT_TRUE(waiter.IsSignaled());
}

TEST_F(BindToTaskRunnerTest, Bool) {
  bool bool_var = false;
  base::Callback<void(bool)> cb =
      BindToCurrentThread(base::Bind(&BoundBoolSet, &bool_var));
  cb.Run(true);
  EXPECT_FALSE(bool_var);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_var);
}

TEST_F(BindToTaskRunnerTest, BoundScopedPtrBool) {
  bool bool_val = false;
  scoped_ptr<bool> scoped_ptr_bool(new bool(true));
  base::Closure cb = BindToCurrentThread(base::Bind(
      &BoundBoolSetFromScopedPtr, &bool_val, base::Passed(&scoped_ptr_bool)));
  cb.Run();
  EXPECT_FALSE(bool_val);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_val);
}

TEST_F(BindToTaskRunnerTest, PassedScopedPtrBool) {
  bool bool_val = false;
  scoped_ptr<bool> scoped_ptr_bool(new bool(true));
  base::Callback<void(scoped_ptr<bool>)> cb =
      BindToCurrentThread(base::Bind(&BoundBoolSetFromScopedPtr, &bool_val));
  cb.Run(std::move(scoped_ptr_bool));
  EXPECT_FALSE(bool_val);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_val);
}

TEST_F(BindToTaskRunnerTest, BoundScopedArrayBool) {
  bool bool_val = false;
  scoped_ptr<bool[]> scoped_array_bool(new bool[1]);
  scoped_array_bool[0] = true;
  base::Closure cb =
      BindToCurrentThread(base::Bind(&BoundBoolSetFromScopedArray, &bool_val,
                                     base::Passed(&scoped_array_bool)));
  cb.Run();
  EXPECT_FALSE(bool_val);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_val);
}

TEST_F(BindToTaskRunnerTest, PassedScopedArrayBool) {
  bool bool_val = false;
  scoped_ptr<bool[]> scoped_array_bool(new bool[1]);
  scoped_array_bool[0] = true;
  base::Callback<void(scoped_ptr<bool[]>)> cb =
      BindToCurrentThread(base::Bind(&BoundBoolSetFromScopedArray, &bool_val));
  cb.Run(std::move(scoped_array_bool));
  EXPECT_FALSE(bool_val);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_val);
}

TEST_F(BindToTaskRunnerTest, BoundScopedPtrFreeDeleterBool) {
  bool bool_val = false;
  scoped_ptr<bool, base::FreeDeleter> scoped_ptr_free_deleter_bool(
      static_cast<bool*>(malloc(sizeof(bool))));
  *scoped_ptr_free_deleter_bool = true;
  base::Closure cb = BindToCurrentThread(
      base::Bind(&BoundBoolSetFromScopedPtrFreeDeleter, &bool_val,
                 base::Passed(&scoped_ptr_free_deleter_bool)));
  cb.Run();
  EXPECT_FALSE(bool_val);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_val);
}

TEST_F(BindToTaskRunnerTest, PassedScopedPtrFreeDeleterBool) {
  bool bool_val = false;
  scoped_ptr<bool, base::FreeDeleter> scoped_ptr_free_deleter_bool(
      static_cast<bool*>(malloc(sizeof(bool))));
  *scoped_ptr_free_deleter_bool = true;
  base::Callback<void(scoped_ptr<bool, base::FreeDeleter>)> cb =
      BindToCurrentThread(
          base::Bind(&BoundBoolSetFromScopedPtrFreeDeleter, &bool_val));
  cb.Run(std::move(scoped_ptr_free_deleter_bool));
  EXPECT_FALSE(bool_val);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_val);
}

TEST_F(BindToTaskRunnerTest, BoolConstRef) {
  bool bool_var = false;
  bool true_var = true;
  const bool& true_ref = true_var;
  base::Closure cb = BindToCurrentThread(
      base::Bind(&BoundBoolSetFromConstRef, &bool_var, true_ref));
  cb.Run();
  EXPECT_FALSE(bool_var);
  loop_.RunUntilIdle();
  EXPECT_TRUE(bool_var);
}

TEST_F(BindToTaskRunnerTest, Integers) {
  int a = 0;
  int b = 0;
  base::Callback<void(int, int)> cb =
      BindToCurrentThread(base::Bind(&BoundIntegersSet, &a, &b));
  cb.Run(1, -1);
  EXPECT_EQ(a, 0);
  EXPECT_EQ(b, 0);
  loop_.RunUntilIdle();
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, -1);
}

}  // namespace chromecast
