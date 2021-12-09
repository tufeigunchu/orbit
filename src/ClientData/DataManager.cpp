// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ClientData/DataManager.h"

#include <absl/container/flat_hash_set.h>

#include <utility>

#include "ClientData/FunctionUtils.h"
#include "ClientProtos/capture_data.pb.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/ThreadUtils.h"

using orbit_client_data::TracepointInfoSet;
using orbit_client_protos::FunctionInfo;
using orbit_grpc_protos::TracepointInfo;

namespace orbit_client_data {

void DataManager::SelectFunction(const FunctionInfo& function) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  if (!selected_functions_.contains(function) &&
      orbit_client_data::function_utils::IsFunctionSelectable(function)) {
    selected_functions_.insert(function);
  }
}

void DataManager::DeselectFunction(const FunctionInfo& function) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  selected_functions_.erase(function);
}

void DataManager::set_visible_function_ids(absl::flat_hash_set<uint64_t> visible_function_ids) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  visible_function_ids_ = std::move(visible_function_ids);
}

void DataManager::set_highlighted_function_id(uint64_t highlighted_function_id) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  highlighted_function_id_ = highlighted_function_id;
}

void DataManager::set_highlighted_group_id(uint64_t highlighted_group_id) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  highlighted_function_id_ = highlighted_group_id;
}

void DataManager::set_selected_thread_id(uint32_t thread_id) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  selected_thread_id_ = thread_id;
}

bool DataManager::IsFunctionVisible(uint64_t function_id) const {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return visible_function_ids_.contains(function_id);
}

uint64_t DataManager::highlighted_function_id() const {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return highlighted_function_id_;
}

uint64_t DataManager::highlighted_group_id() const {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return highlighted_group_id_;
}

int32_t DataManager::selected_thread_id() const {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return selected_thread_id_;
}

const orbit_client_protos::TimerInfo* DataManager::selected_timer() const {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return selected_timer_;
}

void DataManager::set_selected_timer(const orbit_client_protos::TimerInfo* timer_info) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  selected_timer_ = timer_info;
}

void DataManager::ClearSelectedFunctions() {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  selected_functions_.clear();
}

bool DataManager::IsFunctionSelected(const FunctionInfo& function) const {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return selected_functions_.contains(function);
}

std::vector<FunctionInfo> DataManager::GetSelectedFunctions() const {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return std::vector<FunctionInfo>(selected_functions_.begin(), selected_functions_.end());
}

void DataManager::SelectTracepoint(const TracepointInfo& info) {
  if (!IsTracepointSelected(info)) selected_tracepoints_.emplace(info);
}

void DataManager::DeselectTracepoint(const TracepointInfo& info) {
  CHECK(IsTracepointSelected(info));
  selected_tracepoints_.erase(info);
}

void DataManager::SelectCallstackEvents(
    const std::vector<orbit_client_protos::CallstackEvent>& selected_callstack_events) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  selected_callstack_events_by_thread_id_.clear();

  for (auto& event : selected_callstack_events) {
    selected_callstack_events_by_thread_id_[event.thread_id()].emplace_back(event);
    selected_callstack_events_by_thread_id_[orbit_base::kAllProcessThreadsTid].emplace_back(event);
  }
}

const std::vector<orbit_client_protos::CallstackEvent>& DataManager::GetSelectedCallstackEvents(
    uint32_t thread_id) {
  CHECK(std::this_thread::get_id() == main_thread_id_);
  return selected_callstack_events_by_thread_id_[thread_id];
}

bool DataManager::IsTracepointSelected(const TracepointInfo& info) const {
  return selected_tracepoints_.contains(info);
}

const TracepointInfoSet& DataManager::selected_tracepoints() const { return selected_tracepoints_; }

void DataManager::EnableFrameTrack(const FunctionInfo& function) {
  user_defined_capture_data_.InsertFrameTrack(function);
}

void DataManager::DisableFrameTrack(const FunctionInfo& function) {
  user_defined_capture_data_.EraseFrameTrack(function);
}

[[nodiscard]] bool DataManager::IsFrameTrackEnabled(const FunctionInfo& function) const {
  return user_defined_capture_data_.ContainsFrameTrack(function);
}

void DataManager::ClearUserDefinedCaptureData() { user_defined_capture_data_.Clear(); }

}  // namespace orbit_client_data
