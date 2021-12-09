// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_DATA_MODULE_DATA_H_
#define CLIENT_DATA_MODULE_DATA_H_

#include <cinttypes>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ClientProtos/capture_data.pb.h"
#include "GrpcProtos/module.pb.h"
#include "GrpcProtos/symbol.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

namespace orbit_client_data {

// Represents information about module on the client
class ModuleData final {
 public:
  explicit ModuleData(orbit_grpc_protos::ModuleInfo info)
      : module_info_(std::move(info)), is_loaded_(false) {}

  [[nodiscard]] const std::string& name() const { return module_info_.name(); }
  [[nodiscard]] const std::string& file_path() const { return module_info_.file_path(); }
  [[nodiscard]] uint64_t file_size() const { return module_info_.file_size(); }
  [[nodiscard]] const std::string& build_id() const { return module_info_.build_id(); }
  [[nodiscard]] uint64_t load_bias() const { return module_info_.load_bias(); }
  [[nodiscard]] orbit_grpc_protos::ModuleInfo::ObjectFileType object_file_type() const {
    return module_info_.object_file_type();
  }
  [[nodiscard]] uint64_t executable_segment_offset() const {
    return module_info_.executable_segment_offset();
  }
  [[nodiscard]] bool is_loaded() const;
  // Returns true of module was unloaded and false otherwise
  [[nodiscard]] bool UpdateIfChangedAndUnload(orbit_grpc_protos::ModuleInfo info);
  // This method does not update module_data in case it needs update and was not loaded.
  // returns true if update was successful or no update was needed and false if module
  // cannot be updated because it was loaded.
  [[nodiscard]] bool UpdateIfChangedAndNotLoaded(orbit_grpc_protos::ModuleInfo info);
  // offset here is the absolute address minus the address this module was loaded at by
  // the process (module base address)
  [[nodiscard]] const orbit_client_protos::FunctionInfo* FindFunctionByOffset(uint64_t offset,
                                                                              bool is_exact) const;
  [[nodiscard]] const orbit_client_protos::FunctionInfo* FindFunctionByElfAddress(
      uint64_t elf_address, bool is_exact) const;
  void AddSymbols(const orbit_grpc_protos::ModuleSymbols& module_symbols);
  [[nodiscard]] const orbit_client_protos::FunctionInfo* FindFunctionFromHash(uint64_t hash) const;
  [[nodiscard]] const orbit_client_protos::FunctionInfo* FindFunctionFromPrettyName(
      std::string_view pretty_name) const;
  [[nodiscard]] std::vector<const orbit_client_protos::FunctionInfo*> GetFunctions() const;

 private:
  [[nodiscard]] bool NeedsUpdate(const orbit_grpc_protos::ModuleInfo& info) const;

  mutable absl::Mutex mutex_;
  orbit_grpc_protos::ModuleInfo module_info_;
  bool is_loaded_;
  std::map<uint64_t, std::unique_ptr<orbit_client_protos::FunctionInfo>> functions_;
  absl::flat_hash_map<std::string_view, orbit_client_protos::FunctionInfo*>
      name_to_function_info_map_;

  // TODO(b/168799822) This is a map of hash to function used for preset loading. Currently presets
  // are based on a hash of the functions pretty name. This should be changed to not use hashes
  // anymore.
  absl::flat_hash_map<uint64_t, orbit_client_protos::FunctionInfo*> hash_to_function_map_;
};

}  // namespace orbit_client_data

#endif  // CLIENT_DATA_MODULE_DATA_H_
