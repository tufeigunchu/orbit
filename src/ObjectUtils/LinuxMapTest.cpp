// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/strings/str_format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "GrpcProtos/module.pb.h"
#include "ObjectUtils/LinuxMap.h"
#include "OrbitBase/Result.h"
#include "Test/Path.h"
#include "TestUtils/TestUtils.h"

using orbit_test_utils::HasNoError;

namespace orbit_object_utils {

TEST(LinuxMap, CreateModuleHelloWorld) {
  using orbit_grpc_protos::ModuleInfo;

  const std::filesystem::path hello_world_path = orbit_test::GetTestdataDir() / "hello_world_elf";

  constexpr uint64_t kStartAddress = 23;
  constexpr uint64_t kEndAddress = 8004;
  auto result = CreateModule(hello_world_path, kStartAddress, kEndAddress);
  ASSERT_THAT(result, HasNoError());

  EXPECT_EQ(result.value().name(), "hello_world_elf");
  EXPECT_EQ(result.value().file_path(), hello_world_path);
  EXPECT_EQ(result.value().file_size(), 16616);
  EXPECT_EQ(result.value().address_start(), kStartAddress);
  EXPECT_EQ(result.value().address_end(), kEndAddress);
  EXPECT_EQ(result.value().build_id(), "d12d54bc5b72ccce54a408bdeda65e2530740ac8");
  EXPECT_EQ(result.value().load_bias(), 0x0);
  EXPECT_EQ(result.value().object_file_type(), ModuleInfo::kElfFile);
}

TEST(LinuxMap, CreateModuleOnDev) {
  using orbit_grpc_protos::ModuleInfo;

  const std::filesystem::path dev_zero_path = "/dev/zero";

  constexpr uint64_t kStartAddress = 23;
  constexpr uint64_t kEndAddress = 8004;
  auto result = CreateModule(dev_zero_path, kStartAddress, kEndAddress);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ(result.error().message(),
            "The module \"/dev/zero\" is a character or block device (is in /dev/)");
}

TEST(LinuxMap, CreateCoffModule) {
  using orbit_grpc_protos::ModuleInfo;

  const std::filesystem::path dll_path = orbit_test::GetTestdataDir() / "libtest.dll";

  constexpr uint64_t kStartAddress = 23;
  constexpr uint64_t kEndAddress = 8004;

  auto result = CreateModule(dll_path, kStartAddress, kEndAddress);
  ASSERT_THAT(result, HasNoError());

  EXPECT_EQ(result.value().name(), "libtest.dll");
  EXPECT_EQ(result.value().file_path(), dll_path);
  EXPECT_EQ(result.value().file_size(), 96441);
  EXPECT_EQ(result.value().address_start(), kStartAddress);
  EXPECT_EQ(result.value().address_end(), kEndAddress);
  EXPECT_EQ(result.value().load_bias(), 0x62640000);
  EXPECT_EQ(result.value().executable_segment_offset(), 0x1000);
  EXPECT_EQ(result.value().build_id(), "");
  EXPECT_EQ(result.value().object_file_type(), ModuleInfo::kCoffFile);
}

TEST(LinuxMap, CreateModuleNotElf) {
  using orbit_grpc_protos::ModuleInfo;

  const std::filesystem::path text_file = orbit_test::GetTestdataDir() / "textfile.txt";

  constexpr uint64_t kStartAddress = 23;
  constexpr uint64_t kEndAddress = 8004;
  auto result = CreateModule(text_file, kStartAddress, kEndAddress);
  ASSERT_TRUE(result.has_error());
  EXPECT_THAT(result.error().message(),
              testing::HasSubstr("The file was not recognized as a valid object file"));
}

TEST(LinuxMan, CreateModuleWithSoname) {
  using orbit_grpc_protos::ModuleInfo;

  const std::filesystem::path hello_world_path = orbit_test::GetTestdataDir() / "libtest-1.0.so";

  constexpr uint64_t kStartAddress = 23;
  constexpr uint64_t kEndAddress = 8004;
  auto result = CreateModule(hello_world_path, kStartAddress, kEndAddress);
  ASSERT_THAT(result, HasNoError());

  EXPECT_EQ(result.value().name(), "libtest.so");
  EXPECT_EQ(result.value().file_path(), hello_world_path);
  EXPECT_EQ(result.value().file_size(), 16128);
  EXPECT_EQ(result.value().address_start(), kStartAddress);
  EXPECT_EQ(result.value().address_end(), kEndAddress);
  EXPECT_EQ(result.value().build_id(), "2e70049c5cf42e6c5105825b57104af5882a40a2");
  EXPECT_EQ(result.value().load_bias(), 0x0);
  EXPECT_EQ(result.value().object_file_type(), ModuleInfo::kElfFile);
}

TEST(LinuxMap, CreateModuleFileDoesNotExist) {
  using orbit_grpc_protos::ModuleInfo;

  const std::filesystem::path file_path = "/not/a/valid/file/path";

  constexpr uint64_t kStartAddress = 23;
  constexpr uint64_t kEndAddress = 8004;
  auto result = CreateModule(file_path, kStartAddress, kEndAddress);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ(result.error().message(), "The module file \"/not/a/valid/file/path\" does not exist");
}

TEST(LinuxMap, ReadModules) {
  const auto result = ReadModules(getpid());
  EXPECT_THAT(result, HasNoError());
}

TEST(LinuxMap, ParseMaps) {
  using orbit_grpc_protos::ModuleInfo;

  {
    // Empty data
    const auto result = ParseMaps(std::string_view{""});
    ASSERT_THAT(result, HasNoError());
    EXPECT_TRUE(result.value().empty());
  }

  const std::filesystem::path test_path = orbit_test::GetTestdataDir();
  const std::filesystem::path hello_world_path = test_path / "hello_world_elf";
  const std::filesystem::path text_file = test_path / "textfile.txt";

  {
    // Testing correct size of result. The entry with dev/zero is ignored due to the path starting
    // with /dev/. The last entry has a valid path, but the executable flag is not set.
    const std::string data{absl::StrFormat(
        "7f687428f000-7f6874290000 r-xp 00009000 fe:01 661216                     "
        "/not/a/valid/file/path\n"
        "7f6874290000-7f6874297000 r-xp 00000000 fe:01 661214                     %s\n"
        "7f6874290000-7f6874297000 r-xp 00000000 fe:01 661214                     /dev/zero\n"
        "7f6874290001-7f6874297002 r-dp 00000000 fe:01 661214                     %s\n",
        hello_world_path, text_file)};
    const auto result = ParseMaps(data);
    ASSERT_THAT(result, HasNoError());
    EXPECT_EQ(result.value().size(), 1);
  }

  const std::filesystem::path no_symbols_path = test_path / "no_symbols_elf";
  {
    // Example data
    const std::string data{absl::StrFormat(
        "7f6874285000-7f6874288000 r--p 00000000 fe:01 661216                     %s\n"
        "7f6874288000-7f687428c000 r-xp 00003000 fe:01 661216                     %s\n"
        "7f687428c000-7f687428e000 r--p 00007000 fe:01 661216                     %s\n"
        "7f687428e000-7f687428f000 r--p 00008000 fe:01 661216                     %s\n"
        "7f687428f000-7f6874290000 rw-p 00009000 fe:01 661216                     %s\n"
        "0-1000 r-xp 00009000 fe:01 661216                     %s\n",
        hello_world_path, hello_world_path, hello_world_path, hello_world_path, hello_world_path,
        no_symbols_path)};

    const auto result = ParseMaps(data);
    ASSERT_THAT(result, HasNoError());
    ASSERT_EQ(result.value().size(), 2);

    const ModuleInfo* hello_module_info = nullptr;
    const ModuleInfo* no_symbols_module_info = nullptr;
    ;
    if (result.value()[0].name() == "hello_world_elf") {
      hello_module_info = &result.value()[0];
      no_symbols_module_info = &result.value()[1];
    } else {
      hello_module_info = &result.value()[1];
      no_symbols_module_info = &result.value()[0];
    }

    EXPECT_EQ(hello_module_info->name(), "hello_world_elf");
    EXPECT_EQ(hello_module_info->file_path(), hello_world_path);
    EXPECT_EQ(hello_module_info->file_size(), 16616);
    EXPECT_EQ(hello_module_info->address_start(), 0x7f6874288000);
    EXPECT_EQ(hello_module_info->address_end(), 0x7f687428c000);
    EXPECT_EQ(hello_module_info->build_id(), "d12d54bc5b72ccce54a408bdeda65e2530740ac8");
    EXPECT_EQ(hello_module_info->load_bias(), 0x0);
    EXPECT_EQ(hello_module_info->object_file_type(), ModuleInfo::kElfFile);

    EXPECT_EQ(no_symbols_module_info->name(), "no_symbols_elf");
    EXPECT_EQ(no_symbols_module_info->file_path(), no_symbols_path);
    EXPECT_EQ(no_symbols_module_info->file_size(), 18768);
    EXPECT_EQ(no_symbols_module_info->address_start(), 0x0);
    EXPECT_EQ(no_symbols_module_info->address_end(), 0x1000);
    EXPECT_EQ(no_symbols_module_info->build_id(), "b5413574bbacec6eacb3b89b1012d0e2cd92ec6b");
    EXPECT_EQ(no_symbols_module_info->load_bias(), 0x400000);
    EXPECT_EQ(no_symbols_module_info->object_file_type(), ModuleInfo::kElfFile);
  }
}

}  // namespace orbit_object_utils
