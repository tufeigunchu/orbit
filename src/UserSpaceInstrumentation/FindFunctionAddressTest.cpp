// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/strings/match.h>
#include <gtest/gtest.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include "FindFunctionAddress.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/Result.h"
#include "UserSpaceInstrumentation/Attach.h"

namespace orbit_user_space_instrumentation {

TEST(FindFunctionAddressTest, FindFunctionAddress) {
  pid_t pid = fork();
  CHECK(pid != -1);
  if (pid == 0) {
    prctl(PR_SET_PDEATHSIG, SIGTERM);

    volatile uint64_t counter = 0;
    while (true) {
      // Endless loops without side effects are UB and recent versions of clang optimize it away.
      ++counter;
    }
  }

  // Stop the child process using our tooling.
  CHECK(AttachAndStopProcess(pid).has_value());

  auto function_address_or_error = FindFunctionAddress(pid, "libc.so.6", "printf");
  ASSERT_TRUE(function_address_or_error.has_value()) << function_address_or_error.error().message();

  function_address_or_error = FindFunctionAddress(pid, "libc.so.6", "NOT_A_SYMBOL");
  ASSERT_TRUE(function_address_or_error.has_error());
  EXPECT_TRUE(absl::StrContains(function_address_or_error.error().message(),
                                "Unable to locate function symbol"));

  function_address_or_error = FindFunctionAddress(pid, "NOT_A_LIB-", "printf");
  ASSERT_TRUE(function_address_or_error.has_error());
  EXPECT_TRUE(absl::StrContains(function_address_or_error.error().message(),
                                "There is no module \"NOT_A_LIB-\" in process"));

  function_address_or_error = FindFunctionAddress(-1, "libc.so.6", "printf");
  ASSERT_TRUE(function_address_or_error.has_error());
  EXPECT_TRUE(
      absl::StrContains(function_address_or_error.error().message(), "Unable to open file"));

  // Detach and end child.
  CHECK(!DetachAndContinueProcess(pid).has_error());
  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);
}

}  // namespace orbit_user_space_instrumentation