// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OBJECT_UTILS_SYMBOLS_FILE_H_
#define OBJECT_UTILS_SYMBOLS_FILE_H_

#include <filesystem>
#include <memory>
#include <string>

#include "GrpcProtos/symbol.pb.h"
#include "OrbitBase/Result.h"

namespace orbit_object_utils {

struct ObjectFileInfo {
  // This is the load bias for ELF, for COFF we use ImageBase here, so that our
  // address computations are consistent between what we do we ELF and for COFF.
  uint64_t load_bias = 0;
  // File offset to the beginning of the executable segment. For COFF, this is
  // the file offset to the beginning of the .text section.
  uint64_t executable_segment_offset = 0;
};

class SymbolsFile {
 public:
  SymbolsFile() = default;
  virtual ~SymbolsFile() = default;

  // For ELF files, the string returned by GetBuildId() is the standard build id that can be found
  // in the .note.gnu.build-id section, formatted as a human readable string.
  // PE/COFF object files are uniquely identfied by the PDB debug info consisting of a GUID and age.
  // The build id is formed from these to provide a string that uniquely identifies this object file
  // and the corresponding PDB debug info. The build id for PDB files is formed in the same way.
  [[nodiscard]] virtual std::string GetBuildId() const = 0;
  [[nodiscard]] virtual ErrorMessageOr<orbit_grpc_protos::ModuleSymbols> LoadDebugSymbols() = 0;
  [[nodiscard]] virtual const std::filesystem::path& GetFilePath() const = 0;
};

// Create a symbols file from the file at symbol_file_path. Additional info about the corresponding
// module can be passed in via object_file_info. This is necessary for PDB files, where information
// such as the load bias cannot be determined from the PDB file alone but is needed to compute the
// right addresses for symbols.
ErrorMessageOr<std::unique_ptr<SymbolsFile>> CreateSymbolsFile(
    const std::filesystem::path& symbol_file_path, const ObjectFileInfo& object_file_info);

}  // namespace orbit_object_utils

#endif  // OBJECT_UTILS_SYMBOLS_FILE_H_