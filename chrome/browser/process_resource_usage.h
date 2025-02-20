// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PROCESS_RESOURCE_USAGE_H_
#define CHROME_BROWSER_PROCESS_RESOURCE_USAGE_H_

#include <stddef.h>

#include <deque>

#include "base/callback.h"
#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "chrome/common/resource_usage_reporter.mojom.h"
#include "third_party/WebKit/public/web/WebCache.h"

// Provides resource usage information about a child process.
//
// This is a wrapper around the ResourceUsageReporter Mojo service that exposes
// information about resources used by a child process. Currently, this is only
// V8 memory and Blink resource cache usage, but could be expanded to include
// other resources.  This is intended for status viewers such as the task
// manager.
//
// To create:
// 1. Create a ResourceUsageReporterPtr and obtain an InterfaceRequest<> using
//    mojo::GetProxy.
// 2. Use the child process's service registry to connect to the service using
//    the InterfaceRequest<>. Note, ServiceRegistry is thread hostile and
//    must always be accessed from the same thread. However, InterfaceRequest<>
//    can be passed safely between threads, and therefore a task can be posted
//    to the ServiceRegistry thread to connect to the remote service.
// 3. Pass the ResourceUsageReporterPtr to the constructor.
//
// Example:
//   void Foo::ConnectToService(
//       mojo::InterfaceRequest<ResourceUsageReporter> req) {
//     content::ServiceRegistry* registry = host_->GetServiceRegistry();
//     registry->ConnectToRemoteService(std::move(req));
//   }
//
//   ...
//     ResourceUsageReporterPtr service;
//     mojo::InterfaceRequest<ResourceUsageReporter> request =
//         mojo::GetProxy(&service);
//     content::BrowserThread::PostTask(
//         content::BrowserThread::IO, FROM_HERE,
//         base::Bind(&Foo::ConnectToService, this, base::Passed(&request)));
//     resource_usage_.reset(new ProcessResourceUsage(std::move(service)));
//   ...
//
// Note: ProcessResourceUsage is thread-hostile and must live on a single
// thread.
class ProcessResourceUsage {
 public:
  // Must be called from the same thread that created |service|.
  explicit ProcessResourceUsage(ResourceUsageReporterPtr service);
  ~ProcessResourceUsage();

  // Refresh the resource usage information. |callback| is invoked when the
  // usage data is updated, or when the IPC connection is lost.
  void Refresh(const base::Closure& callback);

  // Get V8 memory usage information.
  bool ReportsV8MemoryStats() const;
  size_t GetV8MemoryAllocated() const;
  size_t GetV8MemoryUsed() const;

  // Get Blink resource cache information.
  blink::WebCache::ResourceTypeStats GetWebCoreCacheStats() const;

 private:
  // Mojo IPC callback.
  void OnRefreshDone(ResourceUsageDataPtr data);

  void RunPendingRefreshCallbacks();

  ResourceUsageReporterPtr service_;
  bool update_in_progress_;
  std::deque<base::Closure> refresh_callbacks_;

  ResourceUsageDataPtr stats_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(ProcessResourceUsage);
};

#endif  // CHROME_BROWSER_PROCESS_RESOURCE_USAGE_H_
