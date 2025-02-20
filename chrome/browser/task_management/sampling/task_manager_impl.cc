// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/task_management/sampling/task_manager_impl.h"

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/stl_util.h"
#include "build/build_config.h"
#include "chrome/browser/task_management/providers/browser_process_task_provider.h"
#include "chrome/browser/task_management/providers/child_process_task_provider.h"
#include "chrome/browser/task_management/providers/web_contents/web_contents_task_provider.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/gpu_data_manager.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/task_management/providers/arc/arc_process_task_provider.h"
#include "components/arc/arc_bridge_service.h"
#endif  // defined(OS_CHROMEOS)

namespace task_management {

namespace {

scoped_refptr<base::SequencedTaskRunner> GetBlockingPoolRunner() {
  base::SequencedWorkerPool* blocking_pool =
      content::BrowserThread::GetBlockingPool();
  return blocking_pool->GetSequencedTaskRunner(
      blocking_pool->GetSequenceToken());
}

base::LazyInstance<TaskManagerImpl> lazy_task_manager_instance =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

TaskManagerImpl::TaskManagerImpl()
    : on_background_data_ready_callback_(base::Bind(
          &TaskManagerImpl::OnTaskGroupBackgroundCalculationsDone,
          base::Unretained(this))),
      blocking_pool_runner_(GetBlockingPoolRunner()),
      is_running_(false) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  task_providers_.push_back(new BrowserProcessTaskProvider());
  task_providers_.push_back(new ChildProcessTaskProvider());
  task_providers_.push_back(new WebContentsTaskProvider());
#if defined(OS_CHROMEOS)
  if (arc::ArcBridgeService::GetEnabled(
          base::CommandLine::ForCurrentProcess())) {
    task_providers_.push_back(new ArcProcessTaskProvider());
  }
#endif  // defined(OS_CHROMEOS)

  content::GpuDataManager::GetInstance()->AddObserver(this);
}

TaskManagerImpl::~TaskManagerImpl() {
  content::GpuDataManager::GetInstance()->RemoveObserver(this);

  STLDeleteValues(&task_groups_by_proc_id_);
}

// static
TaskManagerImpl* TaskManagerImpl::GetInstance() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  return lazy_task_manager_instance.Pointer();
}

void TaskManagerImpl::ActivateTask(TaskId task_id) {
  GetTaskByTaskId(task_id)->Activate();
}

void TaskManagerImpl::KillTask(TaskId task_id) {
  GetTaskByTaskId(task_id)->Kill();
}

double TaskManagerImpl::GetCpuUsage(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->cpu_usage();
}

int64_t TaskManagerImpl::GetPhysicalMemoryUsage(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->physical_bytes();
}

int64_t TaskManagerImpl::GetPrivateMemoryUsage(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->private_bytes();
}

int64_t TaskManagerImpl::GetSharedMemoryUsage(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->shared_bytes();
}

int64_t TaskManagerImpl::GetGpuMemoryUsage(TaskId task_id,
                                           bool* has_duplicates) const {
  const TaskGroup* task_group = GetTaskGroupByTaskId(task_id);
  if (has_duplicates)
    *has_duplicates = task_group->gpu_memory_has_duplicates();
  return task_group->gpu_memory();
}

int TaskManagerImpl::GetIdleWakeupsPerSecond(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->idle_wakeups_per_second();
}

int TaskManagerImpl::GetNaClDebugStubPort(TaskId task_id) const {
#if !defined(DISABLE_NACL)
  return GetTaskGroupByTaskId(task_id)->nacl_debug_stub_port();
#else
  return -2;
#endif  // !defined(DISABLE_NACL)
}

void TaskManagerImpl::GetGDIHandles(TaskId task_id,
                                    int64_t* current,
                                    int64_t* peak) const {
#if defined(OS_WIN)
  const TaskGroup* task_group = GetTaskGroupByTaskId(task_id);
  *current = task_group->gdi_current_handles();
  *peak = task_group->gdi_peak_handles();
#else
  *current = -1;
  *peak = -1;
#endif  // defined(OS_WIN)
}

void TaskManagerImpl::GetUSERHandles(TaskId task_id,
                                     int64_t* current,
                                     int64_t* peak) const {
#if defined(OS_WIN)
  const TaskGroup* task_group = GetTaskGroupByTaskId(task_id);
  *current = task_group->user_current_handles();
  *peak = task_group->user_peak_handles();
#else
  *current = -1;
  *peak = -1;
#endif  // defined(OS_WIN)
}

int TaskManagerImpl::GetOpenFdCount(TaskId task_id) const {
#if defined(OS_LINUX)
  return GetTaskGroupByTaskId(task_id)->open_fd_count();
#else
  return -1;
#endif  // defined(OS_LINUX)
}

bool TaskManagerImpl::IsTaskOnBackgroundedProcess(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->is_backgrounded();
}

const base::string16& TaskManagerImpl::GetTitle(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->title();
}

const std::string& TaskManagerImpl::GetTaskNameForRappor(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->rappor_sample_name();
}

base::string16 TaskManagerImpl::GetProfileName(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->GetProfileName();
}

const gfx::ImageSkia& TaskManagerImpl::GetIcon(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->icon();
}

const base::ProcessHandle& TaskManagerImpl::GetProcessHandle(
    TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->process_handle();
}

const base::ProcessId& TaskManagerImpl::GetProcessId(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->process_id();
}

Task::Type TaskManagerImpl::GetType(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->GetType();
}

int TaskManagerImpl::GetTabId(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->GetTabId();
}

int TaskManagerImpl::GetChildProcessUniqueId(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->GetChildProcessUniqueID();
}

void TaskManagerImpl::GetTerminationStatus(TaskId task_id,
                                           base::TerminationStatus* out_status,
                                           int* out_error_code) const {
  GetTaskByTaskId(task_id)->GetTerminationStatus(out_status, out_error_code);
}

int64_t TaskManagerImpl::GetNetworkUsage(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->network_usage();
}

int64_t TaskManagerImpl::GetProcessTotalNetworkUsage(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->per_process_network_usage();
}

int64_t TaskManagerImpl::GetSqliteMemoryUsed(TaskId task_id) const {
  return GetTaskByTaskId(task_id)->GetSqliteMemoryUsed();
}

bool TaskManagerImpl::GetV8Memory(TaskId task_id,
                                  int64_t* allocated,
                                  int64_t* used) const {
  const Task* task = GetTaskByTaskId(task_id);
  if (!task->ReportsV8Memory())
    return false;

  *allocated = task->GetV8MemoryAllocated();
  *used = task->GetV8MemoryUsed();

  return true;
}

bool TaskManagerImpl::GetWebCacheStats(
    TaskId task_id,
    blink::WebCache::ResourceTypeStats* stats) const {
  const Task* task = GetTaskByTaskId(task_id);
  if (!task->ReportsWebCacheStats())
    return false;

  *stats = task->GetWebCacheStats();

  return true;
}

const TaskIdList& TaskManagerImpl::GetTaskIdsList() const {
  DCHECK(is_running_) << "Task manager is not running. You must observe the "
      "task manager for it to start running";

  if (sorted_task_ids_.empty()) {
    sorted_task_ids_.reserve(task_groups_by_task_id_.size());

    // Ensure browser process group of task IDs are at the beginning of the
    // list.
    auto it = task_groups_by_proc_id_.find(base::GetCurrentProcId());
    DCHECK(it != task_groups_by_proc_id_.end());
    const TaskGroup* browser_group = it->second;
    browser_group->AppendSortedTaskIds(&sorted_task_ids_);

    for (const auto& groups_pair : task_groups_by_proc_id_) {
      if (groups_pair.second == browser_group)
        continue;

      groups_pair.second->AppendSortedTaskIds(&sorted_task_ids_);
    }
  }

  return sorted_task_ids_;
}

TaskIdList TaskManagerImpl::GetIdsOfTasksSharingSameProcess(
    TaskId task_id) const {
  DCHECK(is_running_) << "Task manager is not running. You must observe the "
      "task manager for it to start running";

  TaskIdList result;
  GetTaskGroupByTaskId(task_id)->AppendSortedTaskIds(&result);
  return result;
}

size_t TaskManagerImpl::GetNumberOfTasksOnSameProcess(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->num_tasks();
}

void TaskManagerImpl::TaskAdded(Task* task) {
  DCHECK(task);

  TaskGroup* task_group = nullptr;
  const base::ProcessId proc_id = task->process_id();
  const TaskId task_id = task->task_id();

  auto itr = task_groups_by_proc_id_.find(proc_id);
  if (itr == task_groups_by_proc_id_.end()) {
    task_group = new TaskGroup(task->process_handle(),
                               proc_id,
                               on_background_data_ready_callback_,
                               blocking_pool_runner_);
    task_groups_by_proc_id_[proc_id] = task_group;
  } else {
    task_group = itr->second;
  }

  task_group->AddTask(task);

  task_groups_by_task_id_[task_id] = task_group;

  // Invalidate the cached sorted IDs by clearing the list.
  sorted_task_ids_.clear();

  NotifyObserversOnTaskAdded(task_id);
}

void TaskManagerImpl::TaskRemoved(Task* task) {
  DCHECK(task);

  const base::ProcessId proc_id = task->process_id();
  const TaskId task_id = task->task_id();

  DCHECK(ContainsKey(task_groups_by_proc_id_, proc_id));
  DCHECK(ContainsKey(task_groups_by_task_id_, task_id));

  NotifyObserversOnTaskToBeRemoved(task_id);

  TaskGroup* task_group = task_groups_by_proc_id_[proc_id];
  task_group->RemoveTask(task);

  task_groups_by_task_id_.erase(task_id);

  // Invalidate the cached sorted IDs by clearing the list.
  sorted_task_ids_.clear();

  if (task_group->empty()) {
    task_groups_by_proc_id_.erase(proc_id);
    delete task_group;
  }
}

void TaskManagerImpl::TaskUnresponsive(Task* task) {
  DCHECK(task);
  NotifyObserversOnTaskUnresponsive(task->task_id());
}

void TaskManagerImpl::OnVideoMemoryUsageStatsUpdate(
    const gpu::VideoMemoryUsageStats& gpu_memory_stats) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  gpu_memory_stats_ = gpu_memory_stats;
}

// static
void TaskManagerImpl::OnMultipleBytesReadUI(
    std::vector<BytesReadParam>* params) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(params);

  for (BytesReadParam& param : *params) {
    if (!GetInstance()->UpdateTasksWithBytesRead(param)) {
      // We can't match a task to the notification.  That might mean the
      // tab that started a download was closed, or the request may have had
      // no originating task associated with it in the first place.
      // We attribute orphaned/unaccounted activity to the Browser process.
      DCHECK(param.origin_pid || (param.child_id != -1));

      param.origin_pid = 0;
      param.child_id = param.route_id = -1;

      GetInstance()->UpdateTasksWithBytesRead(param);
    }
  }
}

void TaskManagerImpl::Refresh() {
  if (IsResourceRefreshEnabled(REFRESH_TYPE_GPU_MEMORY)) {
    content::GpuDataManager::GetInstance()->
        RequestVideoMemoryUsageStatsUpdate();
  }

  for (auto& groups_itr : task_groups_by_proc_id_) {
    groups_itr.second->Refresh(gpu_memory_stats_,
                               GetCurrentRefreshTime(),
                               enabled_resources_flags());
  }

  NotifyObserversOnRefresh(GetTaskIdsList());
}

void TaskManagerImpl::StartUpdating() {
  if (is_running_)
    return;

  is_running_ = true;

  for (auto& provider : task_providers_)
    provider->SetObserver(this);

  io_thread_helper_manager_.reset(new IoThreadHelperManager);
}

void TaskManagerImpl::StopUpdating() {
  if (!is_running_)
    return;

  is_running_ = false;

  io_thread_helper_manager_.reset();

  for (auto& provider : task_providers_)
    provider->ClearObserver();

  STLDeleteValues(&task_groups_by_proc_id_);
  task_groups_by_task_id_.clear();
  sorted_task_ids_.clear();
}

bool TaskManagerImpl::UpdateTasksWithBytesRead(const BytesReadParam& param) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  for (const auto& task_provider : task_providers_) {
    Task* task = task_provider->GetTaskOfUrlRequest(param.origin_pid,
                                                    param.child_id,
                                                    param.route_id);
    if (task) {
      task->OnNetworkBytesRead(param.byte_count);
      return true;
    }
  }

  // Couldn't match the bytes to any existing task.
  return false;
}

TaskGroup* TaskManagerImpl::GetTaskGroupByTaskId(TaskId task_id) const {
  auto it = task_groups_by_task_id_.find(task_id);
  DCHECK(it != task_groups_by_task_id_.end());
  return it->second;
}

Task* TaskManagerImpl::GetTaskByTaskId(TaskId task_id) const {
  return GetTaskGroupByTaskId(task_id)->GetTaskById(task_id);
}

void TaskManagerImpl::OnTaskGroupBackgroundCalculationsDone() {
  // TODO(afakhry): There should be a better way for doing this!
  bool are_all_processes_data_ready = true;
  for (const auto& groups_itr : task_groups_by_proc_id_) {
    are_all_processes_data_ready &=
        groups_itr.second->AreBackgroundCalculationsDone();
  }
  if (are_all_processes_data_ready) {
    NotifyObserversOnRefreshWithBackgroundCalculations(GetTaskIdsList());
    for (const auto& groups_itr : task_groups_by_proc_id_)
      groups_itr.second->ClearCurrentBackgroundCalculationsFlags();
  }
}

}  // namespace task_management
