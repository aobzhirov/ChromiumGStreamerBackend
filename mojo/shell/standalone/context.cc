// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/standalone/context.h"

#include <stddef.h>
#include <stdint.h>

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/process/process_info.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "components/tracing/tracing_switches.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/services/catalog/factory.h"
#include "mojo/services/catalog/store.h"
#include "mojo/services/tracing/public/cpp/switches.h"
#include "mojo/services/tracing/public/cpp/trace_provider_impl.h"
#include "mojo/services/tracing/public/cpp/tracing_impl.h"
#include "mojo/services/tracing/public/interfaces/tracing.mojom.h"
#include "mojo/shell/connect_params.h"
#include "mojo/shell/public/cpp/names.h"
#include "mojo/shell/runner/host/in_process_native_runner.h"
#include "mojo/shell/runner/host/out_of_process_native_runner.h"
#include "mojo/shell/standalone/tracer.h"
#include "mojo/shell/switches.h"
#include "mojo/util/filename_util.h"

#if defined(OS_MACOSX)
#include "mojo/shell/runner/host/mach_broker.h"
#endif

namespace mojo {
namespace shell {
namespace {

// Used to ensure we only init once.
class Setup {
 public:
  Setup() { edk::Init(); }

  ~Setup() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(Setup);
};

class TracingInterfaceProvider : public shell::mojom::InterfaceProvider {
 public:
  TracingInterfaceProvider(Tracer* tracer,
                           shell::mojom::InterfaceProviderRequest request)
      : tracer_(tracer), binding_(this, std::move(request)) {}
  ~TracingInterfaceProvider() override {}

  // shell::mojom::InterfaceProvider:
  void GetInterface(const mojo::String& interface_name,
                    ScopedMessagePipeHandle client_handle) override {
    if (tracer_ && interface_name == tracing::TraceProvider::Name_) {
      tracer_->ConnectToProvider(
          MakeRequest<tracing::TraceProvider>(std::move(client_handle)));
    }
  }

 private:
  Tracer* tracer_;
  StrongBinding<shell::mojom::InterfaceProvider> binding_;

  DISALLOW_COPY_AND_ASSIGN(TracingInterfaceProvider);
};

const size_t kMaxBlockingPoolThreads = 3;

scoped_ptr<base::Thread> CreateIOThread(const char* name) {
  scoped_ptr<base::Thread> thread(new base::Thread(name));
  base::Thread::Options options;
  options.message_loop_type = base::MessageLoop::TYPE_IO;
  thread->StartWithOptions(options);
  return thread;
}

void OnInstanceQuit(const std::string& name, const Identity& identity) {
  if (name == identity.name())
    base::MessageLoop::current()->QuitWhenIdle();
}

}  // namespace

Context::InitParams::InitParams() {}
Context::InitParams::~InitParams() {}

Context::Context()
    : io_thread_(CreateIOThread("io_thread")),
      main_entry_time_(base::Time::Now()) {}

Context::~Context() {
  DCHECK(!base::MessageLoop::current());
  blocking_pool_->Shutdown();
}

// static
void Context::EnsureEmbedderIsInitialized() {
  static base::LazyInstance<Setup>::Leaky setup = LAZY_INSTANCE_INITIALIZER;
  setup.Get();
}

void Context::Init(scoped_ptr<InitParams> init_params) {
  TRACE_EVENT0("mojo_shell", "Context::Init");
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  bool trace_startup = command_line.HasSwitch(::switches::kTraceStartup);
  if (trace_startup) {
    tracer_.Start(
        command_line.GetSwitchValueASCII(::switches::kTraceStartup),
        command_line.GetSwitchValueASCII(::switches::kTraceStartupDuration),
        "mojo_runner.trace");
  }

  if (!init_params || init_params->init_edk)
    EnsureEmbedderIsInitialized();

  shell_runner_ = base::MessageLoop::current()->task_runner();
  blocking_pool_ =
      new base::SequencedWorkerPool(kMaxBlockingPoolThreads, "blocking_pool");

  init_edk_ = !init_params || init_params->init_edk;
  if (init_edk_) {
    edk::InitIPCSupport(this, io_thread_->task_runner().get());
#if defined(OS_MACOSX)
    edk::SetMachPortProvider(MachBroker::GetInstance()->port_provider());
#endif
  }

  scoped_ptr<NativeRunnerFactory> runner_factory;
  if (command_line.HasSwitch(switches::kSingleProcess)) {
#if defined(COMPONENT_BUILD)
    LOG(ERROR) << "Running Mojo in single process component build, which isn't "
               << "supported because statics in apps interact. Use static build"
               << " or don't pass --single-process.";
#endif
    runner_factory.reset(
        new InProcessNativeRunnerFactory(blocking_pool_.get()));
  } else {
    NativeRunnerDelegate* native_runner_delegate = init_params ?
        init_params->native_runner_delegate : nullptr;
    runner_factory.reset(new OutOfProcessNativeRunnerFactory(
        blocking_pool_.get(), native_runner_delegate));
  }
  scoped_ptr<catalog::Store> store;
  if (init_params)
    store = std::move(init_params->catalog_store);
  catalog_.reset(new catalog::Factory(blocking_pool_.get(), std::move(store)));
  shell_.reset(new Shell(std::move(runner_factory),
                         catalog_->TakeShellClient()));

  shell::mojom::InterfaceProviderPtr tracing_remote_interfaces;
  shell::mojom::InterfaceProviderPtr tracing_local_interfaces;
  new TracingInterfaceProvider(&tracer_, GetProxy(&tracing_local_interfaces));

  scoped_ptr<ConnectParams> params(new ConnectParams);
  params->set_source(CreateShellIdentity());
  params->set_target(Identity("mojo:tracing", mojom::kRootUserID));
  params->set_remote_interfaces(GetProxy(&tracing_remote_interfaces));
  params->set_local_interfaces(std::move(tracing_local_interfaces));
  shell_->Connect(std::move(params));

  if (command_line.HasSwitch(tracing::kTraceStartup)) {
    tracing::TraceCollectorPtr coordinator;
    auto coordinator_request = GetProxy(&coordinator);
    tracing_remote_interfaces->GetInterface(
        tracing::TraceCollector::Name_, coordinator_request.PassMessagePipe());
    tracer_.StartCollectingFromTracingService(std::move(coordinator));
  }

  // Record the shell startup metrics used for performance testing.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          tracing::kEnableStatsCollectionBindings)) {
    tracing::StartupPerformanceDataCollectorPtr collector;
    tracing_remote_interfaces->GetInterface(
        tracing::StartupPerformanceDataCollector::Name_,
        GetProxy(&collector).PassMessagePipe());
#if defined(OS_MACOSX) || defined(OS_WIN) || defined(OS_LINUX)
    // CurrentProcessInfo::CreationTime is only defined on some platforms.
    const base::Time creation_time = base::CurrentProcessInfo::CreationTime();
    collector->SetShellProcessCreationTime(creation_time.ToInternalValue());
#endif
    collector->SetShellMainEntryPointTime(main_entry_time_.ToInternalValue());
  }
}

void Context::Shutdown() {
  // Actions triggered by Shell's destructor may require a current message loop,
  // so we should destruct it explicitly now as ~Context() occurs post message
  // loop shutdown.
  shell_.reset();

  DCHECK_EQ(base::MessageLoop::current()->task_runner(), shell_runner_);

  // If we didn't initialize the edk we should not shut it down.
  if (!init_edk_)
    return;

  TRACE_EVENT0("mojo_shell", "Context::Shutdown");
  // Post a task in case OnShutdownComplete is called synchronously.
  base::MessageLoop::current()->PostTask(FROM_HERE,
                                         base::Bind(edk::ShutdownIPCSupport));
  // We'll quit when we get OnShutdownComplete().
  base::MessageLoop::current()->Run();
}

void Context::OnShutdownComplete() {
  DCHECK_EQ(base::MessageLoop::current()->task_runner(), shell_runner_);
  base::MessageLoop::current()->QuitWhenIdle();
}

void Context::RunCommandLineApplication() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  base::CommandLine::StringVector args = command_line->GetArgs();
  for (size_t i = 0; i < args.size(); ++i) {
#if defined(OS_WIN)
    std::string possible_app = base::WideToUTF8(args[i]);
#else
    std::string possible_app = args[i];
#endif
    if (GetNameType(possible_app) == "mojo") {
      Run(possible_app);
      break;
    }
  }
}

void Context::Run(const std::string& name) {
  shell_->SetInstanceQuitCallback(base::Bind(&OnInstanceQuit, name));

  shell::mojom::InterfaceProviderPtr remote_interfaces;
  shell::mojom::InterfaceProviderPtr local_interfaces;

  scoped_ptr<ConnectParams> params(new ConnectParams);
  params->set_source(CreateShellIdentity());
  params->set_target(Identity(name, mojom::kRootUserID));
  params->set_remote_interfaces(GetProxy(&remote_interfaces));
  params->set_local_interfaces(std::move(local_interfaces));
  shell_->Connect(std::move(params));
}

}  // namespace shell
}  // namespace mojo
