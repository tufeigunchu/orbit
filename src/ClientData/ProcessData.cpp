// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ClientData/ProcessData.h"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <vector>

#include "GrpcProtos/symbol.pb.h"
#include "OrbitBase/Result.h"
#include "absl/strings/str_format.h"

using orbit_grpc_protos::ModuleInfo;

namespace orbit_client_data {

ProcessData::ProcessData() { process_info_.set_pid(-1); }

void ProcessData::SetProcessInfo(const orbit_grpc_protos::ProcessInfo& process_info) {
  absl::MutexLock lock(&mutex_);
  process_info_ = process_info;
}

uint32_t ProcessData::pid() const {
  absl::MutexLock lock(&mutex_);
  return process_info_.pid();
}

const std::string& ProcessData::name() const {
  absl::MutexLock lock(&mutex_);
  return process_info_.name();
}

double ProcessData::cpu_usage() const {
  absl::MutexLock lock(&mutex_);
  return process_info_.cpu_usage();
}

const std::string& ProcessData::full_path() const {
  absl::MutexLock lock(&mutex_);
  return process_info_.full_path();
}

const std::string& ProcessData::command_line() const {
  absl::MutexLock lock(&mutex_);
  return process_info_.command_line();
}

bool ProcessData::is_64_bit() const {
  absl::MutexLock lock(&mutex_);
  return process_info_.is_64_bit();
}

const std::string& ProcessData::build_id() const {
  absl::MutexLock lock(&mutex_);
  return process_info_.build_id();
}

[[maybe_unused]] static bool IsModuleMapValid(
    const std::map<uint64_t, ModuleInMemory>& module_map) {
  // Check that modules do not intersect in the address space.
  uint64_t last_end_address = 0;
  for (const auto& [unused_address, module_in_memory] : module_map) {
    if (module_in_memory.start() < last_end_address) return false;
    last_end_address = module_in_memory.end();
  }

  return true;
}

void ProcessData::UpdateModuleInfos(absl::Span<const ModuleInfo> module_infos) {
  absl::MutexLock lock(&mutex_);
  start_address_to_module_in_memory_.clear();

  for (const auto& module_info : module_infos) {
    const auto [unused_it, success] = start_address_to_module_in_memory_.try_emplace(
        module_info.address_start(), module_info.address_start(), module_info.address_end(),
        module_info.file_path(), module_info.build_id());
    CHECK(success);
  }

  // Files saved with Orbit 1.65 may have intersecting maps, this is why we use DCHECK here
  // instead of CHECK
  DCHECK(IsModuleMapValid(start_address_to_module_in_memory_));
}

std::vector<std::string> ProcessData::FindModuleBuildIdsByPath(
    const std::string& module_path) const {
  absl::MutexLock lock(&mutex_);
  std::set<std::string> build_ids;

  for (const auto& [unused_address, module_in_memory] : start_address_to_module_in_memory_) {
    if (module_in_memory.file_path() == module_path) {
      build_ids.insert(module_in_memory.build_id());
    }
  }

  return {build_ids.begin(), build_ids.end()};
}

void ProcessData::AddOrUpdateModuleInfo(const ModuleInfo& module_info) {
  absl::MutexLock lock(&mutex_);
  ModuleInMemory module_in_memory{module_info.address_start(), module_info.address_end(),
                                  module_info.file_path(), module_info.build_id()};

  auto it = start_address_to_module_in_memory_.upper_bound(module_in_memory.start());
  if (it != start_address_to_module_in_memory_.begin()) {
    --it;
    if (it->second.end() > module_in_memory.start()) {
      it = start_address_to_module_in_memory_.erase(it);
    } else {
      ++it;
    }
  }

  while (it != start_address_to_module_in_memory_.end() &&
         it->second.start() < module_in_memory.end()) {
    it = start_address_to_module_in_memory_.erase(it);
  }

  start_address_to_module_in_memory_.insert_or_assign(module_info.address_start(),
                                                      module_in_memory);

  CHECK(IsModuleMapValid(start_address_to_module_in_memory_));
}

ErrorMessageOr<ModuleInMemory> ProcessData::FindModuleByAddress(uint64_t absolute_address) const {
  absl::MutexLock lock(&mutex_);
  if (start_address_to_module_in_memory_.empty()) {
    return ErrorMessage(
        absl::StrFormat("Unable to find module for address %016x: No modules loaded by process %s",
                        absolute_address, process_info_.name()));
  }

  ErrorMessage not_found_error = ErrorMessage(absl::StrFormat(
      "Unable to find module for address %016x: No module loaded at this address by process %s",
      absolute_address, process_info_.name()));

  auto it = start_address_to_module_in_memory_.upper_bound(absolute_address);
  if (it == start_address_to_module_in_memory_.begin()) return not_found_error;

  --it;
  const ModuleInMemory& module_in_memory = it->second;
  CHECK(absolute_address >= module_in_memory.start());
  if (absolute_address >= module_in_memory.end()) return not_found_error;

  return module_in_memory;
}

std::vector<uint64_t> ProcessData::GetModuleBaseAddresses(const std::string& module_path,
                                                          const std::string& build_id) const {
  absl::MutexLock lock(&mutex_);
  std::vector<uint64_t> result;
  for (const auto& [start_address, module_in_memory] : start_address_to_module_in_memory_) {
    if (module_in_memory.file_path() == module_path && module_in_memory.build_id() == build_id) {
      result.emplace_back(start_address);
    }
  }
  return result;
}

std::map<uint64_t, ModuleInMemory> ProcessData::GetMemoryMapCopy() const {
  absl::MutexLock lock(&mutex_);
  return start_address_to_module_in_memory_;
}

bool ProcessData::IsModuleLoadedByProcess(const ModuleData* module) const {
  absl::MutexLock lock(&mutex_);
  return std::any_of(start_address_to_module_in_memory_.begin(),
                     start_address_to_module_in_memory_.end(), [module](const auto& it) {
                       return it.second.file_path() == module->file_path() &&
                              it.second.build_id() == module->build_id();
                     });
}

std::vector<std::pair<std::string, std::string>> ProcessData::GetUniqueModulesPathAndBuildId()
    const {
  absl::MutexLock lock(&mutex_);
  std::set<std::pair<std::string, std::string>> module_keys;
  for (const auto& [unused_address, module_in_memory] : start_address_to_module_in_memory_) {
    module_keys.insert(std::make_pair(module_in_memory.file_path(), module_in_memory.build_id()));
  }

  return {module_keys.begin(), module_keys.end()};
}

}  // namespace orbit_client_data
