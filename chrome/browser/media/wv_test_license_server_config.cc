// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/wv_test_license_server_config.h"

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/socket/tcp_server_socket.h"
#include "net/test/python_utils.h"

const uint16_t kMinPort = 17000;
const uint16_t kPortRangeSize = 1000;

// Widevine license server configuration files.
const base::FilePath::CharType kKeysFileName[] =
    FILE_PATH_LITERAL("keys.dat");
const base::FilePath::CharType kPoliciesFileName[] =
    FILE_PATH_LITERAL("policies.dat");
const base::FilePath::CharType kProfilesFileName[] =
    FILE_PATH_LITERAL("profiles.dat");

// License server configuration files directory name relative to root.
const base::FilePath::CharType kLicenseServerConfigDirName[] =
    FILE_PATH_LITERAL("config");

WVTestLicenseServerConfig::WVTestLicenseServerConfig() {
}

WVTestLicenseServerConfig::~WVTestLicenseServerConfig() {
}

bool WVTestLicenseServerConfig::GetServerCommandLine(
    base::CommandLine* command_line) {
  if (!GetPythonCommand(command_line)) {
    LOG(ERROR) << "Could not get Python runtime command.";
    return false;
  }

  // Add the Python protocol buffers files directory to Python path.
  base::FilePath pyproto_dir;
  if (!GetPyProtoPath(&pyproto_dir)) {
    DVLOG(0) << "Cannot find pyproto directory required by license server.";
    return false;
  }
  AppendToPythonPath(pyproto_dir);

  base::FilePath license_server_path;
  GetLicenseServerPath(&license_server_path);
  if (!base::PathExists(license_server_path)) {
    DVLOG(0) << "Missing license server file at "
             << license_server_path.value();
    return false;
  }

  base::FilePath server_root;
  GetLicenseServerRootPath(&server_root);
  base::FilePath config_path = server_root.Append(kLicenseServerConfigDirName);

  if (!base::PathExists(config_path.Append(kKeysFileName)) ||
      !base::PathExists(config_path.Append(kPoliciesFileName)) ||
      !base::PathExists(config_path.Append(kProfilesFileName))) {
    DVLOG(0) << "Missing license server configuration files.";
    return false;
  }

  if (!SelectServerPort())
    return false;

  // Needed to dynamically load .so libraries used by license server.
  // TODO(shadi): Remove need to set env variable once b/12932983 is fixed.
#if defined(OS_LINUX)
  scoped_ptr<base::Environment> env(base::Environment::Create());
  const char kLibraryPathEnvVarName[] = "LD_LIBRARY_PATH";
  std::string library_paths(license_server_path.DirName().value());
  std::string old_path;
  if (env->GetVar(kLibraryPathEnvVarName, &old_path))
    library_paths.append(":").append(old_path);
  env->SetVar(kLibraryPathEnvVarName, library_paths);
#endif  // defined(OS_LINUX)

  // Since it is a Python command line, we need to AppendArg instead of
  // AppendSwitch so that the arguments are passed to the Python server instead
  // of Python engine.
  command_line->AppendArgPath(license_server_path);
  command_line->AppendArg("-k");
  command_line->AppendArgPath(config_path.Append(kKeysFileName));
  command_line->AppendArg("-o");
  command_line->AppendArgPath(config_path.Append(kPoliciesFileName));
  command_line->AppendArg("-r");
  command_line->AppendArgPath(config_path.Append(kProfilesFileName));
  command_line->AppendArg(base::StringPrintf("--port=%u", port_));
  return true;
}

bool WVTestLicenseServerConfig::SelectServerPort() {
  // Try all ports within the range of kMinPort to (kMinPort + kPortRangeSize)
  // Instead of starting from kMinPort, use a random port within that range.
  uint16_t start_seed = base::RandInt(0, kPortRangeSize);
  uint16_t try_port = 0;
  for (uint16_t i = 0; i < kPortRangeSize; ++i) {
    try_port = kMinPort + (start_seed + i) % kPortRangeSize;
    net::NetLog::Source source;
    net::TCPServerSocket sock(NULL, source);
    if (sock.Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), try_port),
                    1) == net::OK) {
      port_ = try_port;
      return true;
    }
  }
  DVLOG(0) << "Could not find an open port in the range of " <<  kMinPort <<
             " to " << kMinPort + kPortRangeSize;
  return false;
}

bool WVTestLicenseServerConfig::IsPlatformSupported() {
#if defined(OS_LINUX) && defined(ARCH_CPU_X86_64)
  return true;
#else
  return false;
#endif  // defined(OS_LINUX)
}

std::string WVTestLicenseServerConfig::GetServerURL() {
  return base::StringPrintf("http://localhost:%u/license_server", port_);
}

void WVTestLicenseServerConfig::GetLicenseServerPath(base::FilePath *path) {
  base::FilePath server_root;
  GetLicenseServerRootPath(&server_root);
  // Platform-specific license server binary path relative to root.
  *path =
#if defined(OS_LINUX)
    server_root.Append(FILE_PATH_LITERAL("linux"))
               .Append(FILE_PATH_LITERAL("license_server.py"));
#else
    server_root.Append(FILE_PATH_LITERAL("unsupported_platform"));
#endif  // defined(OS_LINUX)
}

void WVTestLicenseServerConfig::GetLicenseServerRootPath(
    base::FilePath* path) {
  base::FilePath source_root;
  PathService::Get(base::DIR_SOURCE_ROOT, &source_root);
  *path = source_root.Append(FILE_PATH_LITERAL("third_party"))
                     .Append(FILE_PATH_LITERAL("widevine"))
                     .Append(FILE_PATH_LITERAL("test"))
                     .Append(FILE_PATH_LITERAL("license_server"));
}
