// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/debug/debugger.h"
#include "base/process/launch.h"
#include "base/sys_info.h"
#include "chrome/test/base/chrome_test_launcher.h"
#include "chrome/test/base/chrome_test_suite.h"
#include "chrome/test/base/mojo_test_connector.h"
#include "content/public/common/mojo_shell_connection.h"
#include "content/public/test/test_launcher.h"
#include "mojo/shell/public/cpp/connector.h"
#include "mojo/shell/public/cpp/shell_client.h"
#include "mojo/shell/public/cpp/shell_connection.h"
#include "mojo/shell/runner/common/switches.h"
#include "mojo/shell/runner/host/child_process.h"
#include "mojo/shell/runner/init.h"

namespace {

void ConnectToDefaultApps(mojo::Connector* connector) {
  connector->Connect("mojo:mash_session");
}

class MashTestSuite : public ChromeTestSuite {
 public:
  MashTestSuite(int argc, char** argv) : ChromeTestSuite(argc, argv) {}

  void SetMojoTestConnector(scoped_ptr<MojoTestConnector> connector) {
    mojo_test_connector_ = std::move(connector);
  }
  MojoTestConnector* mojo_test_connector() {
    return mojo_test_connector_.get();
  }

 private:
  // ChromeTestSuite:
  void Shutdown() override {
    mojo_test_connector_.reset();
    ChromeTestSuite::Shutdown();
  }

  scoped_ptr<MojoTestConnector> mojo_test_connector_;

  DISALLOW_COPY_AND_ASSIGN(MashTestSuite);
};

// Used to setup the command line for passing a mojo channel to tests.
class MashTestLauncherDelegate : public ChromeTestLauncherDelegate {
 public:
  MashTestLauncherDelegate() : ChromeTestLauncherDelegate(nullptr) {}
  ~MashTestLauncherDelegate() override {}

  MojoTestConnector* GetMojoTestConnectorForSingleProcess() {
    // This is only called for single process tests, in which case we need
    // the TestSuite to own the MojoTestConnector.
    DCHECK(base::CommandLine::ForCurrentProcess()->HasSwitch(
        content::kSingleProcessTestsFlag));
    DCHECK(test_suite_);
    test_suite_->SetMojoTestConnector(make_scoped_ptr(new MojoTestConnector));
    return test_suite_->mojo_test_connector();
  }

 private:
  // ChromeTestLauncherDelegate:
  int RunTestSuite(int argc, char** argv) override {
    test_suite_.reset(new MashTestSuite(argc, argv));
    const int result = test_suite_->Run();
    test_suite_.reset();
    return result;
  }
  scoped_ptr<content::TestState> PreRunTest(
      base::CommandLine* command_line,
      base::TestLauncher::LaunchOptions* test_launch_options) override {
    if (!mojo_test_connector_) {
      mojo_test_connector_.reset(new MojoTestConnector);
      shell_client_.reset(new mojo::ShellClient);
      shell_connection_.reset(new mojo::ShellConnection(
          shell_client_.get(), mojo_test_connector_->Init()));
      ConnectToDefaultApps(shell_connection_->connector());
    }
    return mojo_test_connector_->PrepareForTest(command_line,
                                                test_launch_options);
  }
  void OnDoneRunningTests() override {
    // We have to shutdown this state here, while an AtExitManager is still
    // valid.
    shell_connection_.reset();
    shell_client_.reset();
    mojo_test_connector_.reset();
  }

  scoped_ptr<MashTestSuite> test_suite_;
  scoped_ptr<MojoTestConnector> mojo_test_connector_;
  scoped_ptr<mojo::ShellClient> shell_client_;
  scoped_ptr<mojo::ShellConnection> shell_connection_;

  DISALLOW_COPY_AND_ASSIGN(MashTestLauncherDelegate);
};

void CreateMojoShellConnection(MashTestLauncherDelegate* delegate) {
  const bool is_external_shell = true;
  content::MojoShellConnection::Create(
      delegate->GetMojoTestConnectorForSingleProcess()->Init(),
      is_external_shell);
  ConnectToDefaultApps(content::MojoShellConnection::Get()->GetConnector());
}

}  // namespace

bool RunMashBrowserTests(int argc, char** argv, int* exit_code) {
  base::CommandLine::Init(argc, argv);
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch("run-in-mash"))
    return false;

  if (command_line.HasSwitch(switches::kChildProcess) &&
      !command_line.HasSwitch(MojoTestConnector::kTestSwitch)) {
    base::AtExitManager at_exit;
    mojo::shell::InitializeLogging();
    mojo::shell::WaitForDebuggerIfNecessary();
#if !defined(OFFICIAL_BUILD) && defined(OS_WIN)
    base::RouteStdioToConsole(false);
#endif
    *exit_code = mojo::shell::ChildProcessMain();
    return true;
  }

  int default_jobs = std::max(1, base::SysInfo::NumberOfProcessors() / 2);
  MashTestLauncherDelegate delegate;
  // --single_process and no primoridal pipe token indicate we were run directly
  // from the command line. In this case we have to start up MojoShellConnection
  // as though we were embedded.
  content::MojoShellConnection::Factory shell_connection_factory;
  if (command_line.HasSwitch(content::kSingleProcessTestsFlag) &&
      !command_line.HasSwitch(switches::kPrimordialPipeToken)) {
    shell_connection_factory =
        base::Bind(&CreateMojoShellConnection, &delegate);
    content::MojoShellConnection::SetFactoryForTest(&shell_connection_factory);
  }
  *exit_code = LaunchChromeTests(default_jobs, &delegate, argc, argv);
  return true;
}
