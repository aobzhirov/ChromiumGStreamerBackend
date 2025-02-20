// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_CRASH_LINUX_DUMP_INFO_H_
#define CHROMECAST_CRASH_LINUX_DUMP_INFO_H_

#include <ctime>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "chromecast/crash/linux/minidump_params.h"

namespace base {
class Value;
}

namespace chromecast {

// Class that encapsulates the construction and parsing of dump entries
// in the log file.
class DumpInfo {
 public:
  // Validate the input as a valid JSON representation of DumpInfo, then
  // populate the relevant fields.
  explicit DumpInfo(const base::Value* entry);

  // Attempt to construct a DumpInfo object that has the following info:
  //
  // -crashed_process_dump: the full path of the dump written
  // -crashed_process_logfile: the full path of the logfile written
  // -dump_time: the time of the dump written
  // -params: a structure containing other useful crash information
  DumpInfo(const std::string& crashed_process_dump,
           const std::string& crashed_process_logfile,
           const time_t& dump_time,
           const MinidumpParams& params);

  ~DumpInfo();

  const std::string& crashed_process_dump() const {
    return crashed_process_dump_;
  }
  const std::string& logfile() const { return logfile_; }
  const time_t& dump_time() const { return dump_time_; }

  // Return a deep copy of the entry's JSON representation.
  // The format is:
  // {
  //   "name": <name>,
  //   "dump_time": <dump_time (kDumpTimeFormat)>,
  //   "dump": <dump>,
  //   "uptime": <uptime>,
  //   "logfile": <logfile>,
  //   "suffix": <suffix>,
  //   "prev_app_name": <prev_app_name>,
  //   "cur_app_name": <current_app_name>,
  //   "last_app_name": <last_app_name>,
  //   "release_version": <release_version>,
  //   "build_number": <build_number>
  //   "reason": <reason>
  // }
  scoped_ptr<base::Value> GetAsValue() const;
  const MinidumpParams& params() const { return params_; }
  bool valid() const { return valid_; }

 private:
  // Checks if parsed JSON in |value| is valid, if so populates the object's
  // fields from |value|.
  bool ParseEntry(const base::Value* value);
  bool SetDumpTimeFromString(const std::string& timestr);

  std::string crashed_process_dump_;
  std::string logfile_;
  time_t dump_time_;
  MinidumpParams params_;
  bool valid_;

  DISALLOW_COPY_AND_ASSIGN(DumpInfo);
};

}  // namespace chromecast

#endif  // CHROMECAST_CRASH_LINUX_DUMP_INFO_H_
