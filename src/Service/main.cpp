// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <string>

#include "OrbitBase/Logging.h"
#include "OrbitService.h"
#include "OrbitVersion/OrbitVersion.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/flags/usage_config.h"

ABSL_FLAG(uint64_t, grpc_port, 44765, "gRPC server port");

ABSL_FLAG(bool, devmode, false, "Enable developer mode");

namespace {
std::atomic<bool> exit_requested;

void SigintHandler(int signum) {
  if (signum == SIGINT) {
    exit_requested = true;
  }
}

void InstallSigintHandler() {
  struct sigaction act {};
  act.sa_handler = SigintHandler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  act.sa_restorer = nullptr;
  sigaction(SIGINT, &act, nullptr);
}

std::string GetLogFilePath() {
  std::filesystem::path var_log{"/var/log"};
  std::filesystem::create_directory(var_log);
  const std::string log_file_path = var_log / "OrbitService.log";
  return log_file_path;
}

}  // namespace

int main(int argc, char** argv) {
  orbit_base::InitLogFile(GetLogFilePath());

  absl::SetProgramUsageMessage("Orbit CPU Profiler Service");
  absl::SetFlagsUsageConfig(absl::FlagsUsageConfig{{}, {}, {}, &orbit_version::GetBuildReport, {}});
  absl::ParseCommandLine(argc, argv);

  InstallSigintHandler();

  uint16_t grpc_port = absl::GetFlag(FLAGS_grpc_port);
  bool dev_mode = absl::GetFlag(FLAGS_devmode);

  exit_requested = false;
  orbit_service::OrbitService service{grpc_port, dev_mode};
  return service.Run(&exit_requested);
}
