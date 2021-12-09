// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ObjectUtils/LinuxMap.h"

#include <absl/strings/match.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

#include "ObjectUtils/CoffFile.h"
#include "ObjectUtils/ElfFile.h"
#include "ObjectUtils/ObjectFile.h"
#include "OrbitBase/Align.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/ReadFileToString.h"
#include "OrbitBase/Result.h"

namespace orbit_object_utils {

using orbit_grpc_protos::ModuleInfo;
using orbit_object_utils::CreateObjectFile;
using orbit_object_utils::ElfFile;
using orbit_object_utils::ObjectFile;

ErrorMessageOr<ModuleInfo> CreateModule(const std::filesystem::path& module_path,
                                        uint64_t start_address, uint64_t end_address) {
  // This excludes mapped character or block devices.
  if (absl::StartsWith(module_path.string(), "/dev/")) {
    return ErrorMessage(absl::StrFormat(
        "The module \"%s\" is a character or block device (is in /dev/)", module_path));
  }

  if (!std::filesystem::exists(module_path)) {
    return ErrorMessage(absl::StrFormat("The module file \"%s\" does not exist", module_path));
  }

  std::error_code error;
  uint64_t file_size = std::filesystem::file_size(module_path, error);
  if (error) {
    return ErrorMessage(
        absl::StrFormat("Unable to get size of \"%s\": %s", module_path, error.message()));
  }

  auto object_file_or_error = CreateObjectFile(module_path);
  if (object_file_or_error.has_error()) {
    return ErrorMessage(absl::StrFormat("Unable to create module from object file: %s",
                                        object_file_or_error.error().message()));
  }

  ModuleInfo module_info;
  module_info.set_file_path(module_path);
  module_info.set_file_size(file_size);
  module_info.set_address_start(start_address);
  module_info.set_address_end(end_address);
  module_info.set_name(object_file_or_error.value()->GetName());
  module_info.set_load_bias(object_file_or_error.value()->GetLoadBias());
  module_info.set_build_id(object_file_or_error.value()->GetBuildId());
  module_info.set_executable_segment_offset(
      object_file_or_error.value()->GetExecutableSegmentOffset());

  if (object_file_or_error.value()->IsElf()) {
    auto* elf_file = dynamic_cast<ElfFile*>((object_file_or_error.value().get()));
    CHECK(elf_file != nullptr);
    module_info.set_soname(elf_file->GetSoname());
    module_info.set_object_file_type(ModuleInfo::kElfFile);
  } else if (object_file_or_error.value()->IsCoff()) {
    module_info.set_object_file_type(ModuleInfo::kCoffFile);
  }

  // All fields we need to set for COFF files are already set, no need to handle COFF
  // specifically here.
  return module_info;
}

ErrorMessageOr<std::vector<ModuleInfo>> ReadModules(int32_t pid) {
  std::filesystem::path proc_maps_path{absl::StrFormat("/proc/%i/maps", pid)};
  OUTCOME_TRY(auto&& proc_maps_data, orbit_base::ReadFileToString(proc_maps_path));
  return ParseMaps(proc_maps_data);
}

ErrorMessageOr<std::vector<ModuleInfo>> ParseMaps(std::string_view proc_maps_data) {
  const std::vector<std::string> proc_maps = absl::StrSplit(proc_maps_data, '\n');

  std::vector<ModuleInfo> result;

  for (const std::string& line : proc_maps) {
    std::vector<std::string> tokens = absl::StrSplit(line, ' ', absl::SkipEmpty());
    // tokens[4] is the inode column. If inode equals 0, then the memory is not
    // mapped to a file (might be heap, stack or something else)
    if (tokens.size() != 6 || tokens[4] == "0") continue;

    const std::string& module_path = tokens[5];

    std::vector<std::string> addresses = absl::StrSplit(tokens[0], '-');
    if (addresses.size() != 2) continue;

    uint64_t start = std::stoull(addresses[0], nullptr, 16);
    uint64_t end = std::stoull(addresses[1], nullptr, 16);
    bool is_executable = tokens[1].size() == 4 && tokens[1][2] == 'x';

    // Skip non-executable mappings
    if (!is_executable) continue;
    ErrorMessageOr<ModuleInfo> module_info_or_error = CreateModule(module_path, start, end);

    if (module_info_or_error.has_error()) {
      ERROR("Unable to create module: %s", module_info_or_error.error().message());
      continue;
    }

    result.emplace_back(std::move(module_info_or_error.value()));
  }

  return result;
}

}  // namespace orbit_object_utils
