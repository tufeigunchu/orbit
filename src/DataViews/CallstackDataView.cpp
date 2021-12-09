// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "DataViews/CallstackDataView.h"

#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>
#include <stddef.h>

#include <cstdint>
#include <filesystem>

#include "ClientData/CaptureData.h"
#include "ClientData/FunctionUtils.h"
#include "ClientProtos/capture_data.pb.h"
#include "DataViews/DataViewType.h"
#include "DataViews/FunctionsDataView.h"
#include "OrbitBase/Append.h"
#include "OrbitBase/Logging.h"

using orbit_client_data::CaptureData;
using orbit_client_data::ModuleData;

using orbit_client_protos::CallstackInfo;
using orbit_client_protos::FunctionInfo;

namespace orbit_data_views {

CallstackDataView::CallstackDataView(AppInterface* app) : DataView(DataViewType::kCallstack, app) {}

const std::vector<DataView::Column>& CallstackDataView::GetColumns() {
  static const std::vector<Column> columns = [] {
    std::vector<Column> columns;
    columns.resize(kNumColumns);
    columns[kColumnSelected] = {"Hooked", .0f, SortingOrder::kDescending};
    columns[kColumnName] = {"Function", .65f, SortingOrder::kAscending};
    columns[kColumnSize] = {"Size", .0f, SortingOrder::kAscending};
    columns[kColumnModule] = {"Module", .0f, SortingOrder::kAscending};
    columns[kColumnAddress] = {"Sampled Address", .0f, SortingOrder::kAscending};
    return columns;
  }();
  return columns;
}

std::string CallstackDataView::GetValue(int row, int column) {
  if (row >= static_cast<int>(GetNumElements())) {
    return "";
  }

  CallstackDataViewFrame frame = GetFrameFromRow(row);
  const FunctionInfo* function = frame.function;
  const ModuleData* module = frame.module;

  switch (column) {
    case kColumnSelected:
      return (function != nullptr && app_->IsFunctionSelected(*function))
                 ? FunctionsDataView::kSelectedFunctionString
                 : FunctionsDataView::kUnselectedFunctionString;
    case kColumnName:
      return absl::StrCat(
          functions_to_highlight_.contains(frame.address) ? kHighlightedFunctionString
                                                          : kHighlightedFunctionBlankString,
          function != nullptr ? orbit_client_data::function_utils::GetDisplayName(*function)
                              : frame.fallback_name);
    case kColumnSize:
      return function != nullptr ? absl::StrFormat("%lu", function->size()) : "";
    case kColumnModule: {
      if (function != nullptr &&
          !orbit_client_data::function_utils::GetLoadedModuleName(*function).empty()) {
        return orbit_client_data::function_utils::GetLoadedModuleName(*function);
      }
      if (module != nullptr) {
        return module->name();
      }
      const CaptureData& capture_data = app_->GetCaptureData();
      return std::filesystem::path(capture_data.GetModulePathByAddress(frame.address))
          .filename()
          .string();
    }
    case kColumnAddress:
      return absl::StrFormat("%#llx", frame.address);
    default:
      return "";
  }
}

std::string CallstackDataView::GetToolTip(int row, int /*column*/) {
  CallstackDataViewFrame frame = GetFrameFromRow(row);
  if (functions_to_highlight_.find(frame.address) != functions_to_highlight_.end()) {
    return absl::StrFormat(
        "Functions marked with %s are part of the selection in the sampling report above",
        CallstackDataView::kHighlightedFunctionString);
  }
  return "";
}

const std::string CallstackDataView::kHighlightedFunctionString = "➜ ";
const std::string CallstackDataView::kHighlightedFunctionBlankString =
    std::string(kHighlightedFunctionString.size(), ' ');

std::vector<std::vector<std::string>> CallstackDataView::GetContextMenuWithGrouping(
    int clicked_index, const std::vector<int>& selected_indices) {
  bool enable_load = false;
  bool enable_select = false;
  bool enable_unselect = false;
  bool enable_disassembly = false;
  bool enable_source_code = false;
  for (int index : selected_indices) {
    CallstackDataViewFrame frame = GetFrameFromRow(index);
    const FunctionInfo* function = frame.function;
    const ModuleData* module = frame.module;

    if (frame.function != nullptr && app_->IsCaptureConnected(app_->GetCaptureData())) {
      enable_select |= !app_->IsFunctionSelected(*function) &&
                       orbit_client_data::function_utils::IsFunctionSelectable(*function);
      enable_unselect |= app_->IsFunctionSelected(*function);
      enable_disassembly = true;
      enable_source_code = true;
    } else if (module != nullptr && !module->is_loaded()) {
      enable_load = true;
    }
  }

  std::vector<std::string> action_group;
  if (enable_load) action_group.emplace_back(std::string{kMenuActionLoadSymbols});
  if (enable_select) action_group.emplace_back(std::string{kMenuActionSelect});
  if (enable_unselect) action_group.emplace_back(std::string{kMenuActionUnselect});
  if (enable_disassembly) action_group.emplace_back(std::string{kMenuActionDisassembly});
  if (enable_source_code) action_group.emplace_back(std::string{kMenuActionSourceCode});

  std::vector<std::vector<std::string>> menu =
      DataView::GetContextMenuWithGrouping(clicked_index, selected_indices);
  menu.insert(menu.begin(), action_group);

  return menu;
}

void CallstackDataView::DoFilter() {
  if (callstack_.frames_size() == 0) {
    return;
  }

  std::vector<uint64_t> indices;
  std::vector<std::string> tokens = absl::StrSplit(absl::AsciiStrToLower(filter_), ' ');

  for (int i = 0; i < callstack_.frames_size(); ++i) {
    CallstackDataViewFrame frame = GetFrameFromIndex(i);
    const FunctionInfo* function = frame.function;
    std::string name = absl::AsciiStrToLower(
        function != nullptr ? orbit_client_data::function_utils::GetDisplayName(*function)
                            : frame.fallback_name);
    bool match = true;

    for (std::string& filter_token : tokens) {
      if (name.find(filter_token) == std::string::npos) {
        match = false;
        break;
      }
    }

    if (match) {
      indices.push_back(i);
    }
  }

  indices_ = std::move(indices);
}

void CallstackDataView::OnDataChanged() {
  int num_functions = callstack_.frames_size();
  indices_.resize(num_functions);
  for (int i = 0; i < num_functions; ++i) {
    indices_[i] = i;
  }

  DataView::OnDataChanged();
}

void CallstackDataView::SetFunctionsToHighlight(
    const absl::flat_hash_set<uint64_t>& absolute_addresses) {
  const CaptureData& capture_data = app_->GetCaptureData();
  functions_to_highlight_.clear();

  for (int index : indices_) {
    CallstackDataViewFrame frame = GetFrameFromIndex(index);
    std::optional<uint64_t> callstack_function_absolute_address =
        capture_data.FindFunctionAbsoluteAddressByInstructionAbsoluteAddress(frame.address);
    if (callstack_function_absolute_address.has_value() &&
        absolute_addresses.contains(callstack_function_absolute_address.value())) {
      functions_to_highlight_.insert(frame.address);
    }
  }
}

bool CallstackDataView::GetDisplayColor(int row, int /*column*/, unsigned char& red,
                                        unsigned char& green, unsigned char& blue) {
  CallstackDataViewFrame frame = GetFrameFromRow(row);
  if (functions_to_highlight_.contains(frame.address)) {
    red = 200;
    green = 240;
    blue = 200;
    return true;
  }
  return false;
}

CallstackDataView::CallstackDataViewFrame CallstackDataView::GetFrameFromRow(int row) const {
  return GetFrameFromIndex(indices_[row]);
}

CallstackDataView::CallstackDataViewFrame CallstackDataView::GetFrameFromIndex(
    int index_in_callstack) const {
  CHECK(index_in_callstack < callstack_.frames_size());
  uint64_t address = callstack_.frames(index_in_callstack);

  CaptureData& capture_data = app_->GetMutableCaptureData();
  const FunctionInfo* function = capture_data.FindFunctionByAddress(address, false);
  ModuleData* module = capture_data.FindMutableModuleByAddress(address);

  if (function != nullptr) {
    return CallstackDataViewFrame(address, function, module);
  }
  const std::string& fallback_name = capture_data.GetFunctionNameByAddress(address);
  return CallstackDataViewFrame(address, fallback_name, module);
}

}  // namespace orbit_data_views
