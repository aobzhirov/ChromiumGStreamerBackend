// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_BASE_CHROME_TEST_LAUNCHER_H_
#define CHROME_TEST_BASE_CHROME_TEST_LAUNCHER_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/test/test_launcher.h"

// Allows a test suite to override the TestSuite class used. By default it is an
// instance of ChromeTestSuite.
class ChromeTestSuiteRunner {
 public:
  ChromeTestSuiteRunner();
  virtual ~ChromeTestSuiteRunner();

  virtual int RunTestSuite(int argc, char** argv);

 private:
  DISALLOW_COPY_AND_ASSIGN(ChromeTestSuiteRunner);
};

class ChromeTestLauncherDelegate : public content::TestLauncherDelegate {
 public:
  // Does not take ownership of ChromeTestSuiteRunner.
  explicit ChromeTestLauncherDelegate(ChromeTestSuiteRunner* runner);
  ~ChromeTestLauncherDelegate() override;

 protected:
  // content::TestLauncherDelegate:
  int RunTestSuite(int argc, char** argv) override;
  bool AdjustChildProcessCommandLine(
      base::CommandLine* command_line,
      const base::FilePath& temp_data_dir) override;
  content::ContentMainDelegate* CreateContentMainDelegate() override;
  void AdjustDefaultParallelJobs(int* default_jobs) override;

 private:
  ChromeTestSuiteRunner* runner_;

  DISALLOW_COPY_AND_ASSIGN(ChromeTestLauncherDelegate);
};

// Launches Chrome browser tests. |default_jobs| is number of test jobs to be
// run in parallel, unless overridden from the command line. Returns exit code.
// Does not take ownership of ChromeTestLauncherDelegate.
int LaunchChromeTests(int default_jobs,
                      content::TestLauncherDelegate* delegate,
                      int argc,
                      char** argv);

#endif  // CHROME_TEST_BASE_CHROME_TEST_LAUNCHER_H_
