// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_DATA_DATA_MANAGER_H_
#define CLIENT_DATA_DATA_MANAGER_H_

#include <absl/container/flat_hash_set.h>
#include <absl/container/node_hash_map.h>

#include <cstdint>
#include <thread>
#include <vector>

#include "ApiInterface/Orbit.h"
#include "ClientData/FunctionInfoSet.h"
#include "ClientData/TracepointCustom.h"
#include "ClientData/UserDefinedCaptureData.h"
#include "ClientProtos/capture_data.pb.h"
#include "GrpcProtos/Constants.h"
#include "GrpcProtos/capture.pb.h"
#include "GrpcProtos/tracepoint.pb.h"

namespace orbit_client_data {

// This class is responsible for storing and
// navigating data on the client side. Note that
// every method of this class should be called
// on the main thread.
class DataManager final {
 public:
  explicit DataManager(std::thread::id thread_id = std::this_thread::get_id())
      : main_thread_id_(thread_id) {}

  void SelectFunction(const orbit_client_protos::FunctionInfo& function);
  void DeselectFunction(const orbit_client_protos::FunctionInfo& function);
  void ClearSelectedFunctions();
  void set_visible_function_ids(absl::flat_hash_set<uint64_t> visible_function_ids);
  void set_highlighted_function_id(uint64_t highlighted_function_id);
  void set_highlighted_group_id(uint64_t highlighted_function_id);
  void set_selected_thread_id(uint32_t thread_id);
  void set_selected_timer(const orbit_client_protos::TimerInfo* timer_info);

  [[nodiscard]] bool IsFunctionSelected(const orbit_client_protos::FunctionInfo& function) const;
  [[nodiscard]] std::vector<orbit_client_protos::FunctionInfo> GetSelectedFunctions() const;
  [[nodiscard]] bool IsFunctionVisible(uint64_t function_address) const;
  [[nodiscard]] uint64_t highlighted_function_id() const;
  [[nodiscard]] uint64_t highlighted_group_id() const;
  [[nodiscard]] int32_t selected_thread_id() const;
  [[nodiscard]] const orbit_client_protos::TimerInfo* selected_timer() const;

  void SelectTracepoint(const orbit_grpc_protos::TracepointInfo& info);
  void DeselectTracepoint(const orbit_grpc_protos::TracepointInfo& info);

  void SelectCallstackEvents(
      const std::vector<orbit_client_protos::CallstackEvent>& selected_callstack_events);
  [[nodiscard]] const std::vector<orbit_client_protos::CallstackEvent>& GetSelectedCallstackEvents(
      uint32_t thread_id);

  [[nodiscard]] bool IsTracepointSelected(const orbit_grpc_protos::TracepointInfo& info) const;

  [[nodiscard]] const TracepointInfoSet& selected_tracepoints() const;

  void EnableFrameTrack(const orbit_client_protos::FunctionInfo& function);
  void DisableFrameTrack(const orbit_client_protos::FunctionInfo& function);
  [[nodiscard]] bool IsFrameTrackEnabled(const orbit_client_protos::FunctionInfo& function) const;
  void ClearUserDefinedCaptureData();

  void set_user_defined_capture_data(const UserDefinedCaptureData& user_defined_capture_data) {
    user_defined_capture_data_ = user_defined_capture_data;
  }
  [[nodiscard]] const UserDefinedCaptureData& user_defined_capture_data() const {
    return user_defined_capture_data_;
  }
  [[nodiscard]] UserDefinedCaptureData& mutable_user_defined_capture_data() {
    return user_defined_capture_data_;
  }

  void set_collect_scheduler_info(bool collect_scheduler_info) {
    collect_scheduler_info_ = collect_scheduler_info;
  }
  [[nodiscard]] bool collect_scheduler_info() const { return collect_scheduler_info_; }

  void set_collect_thread_states(bool collect_thread_states) {
    collect_thread_states_ = collect_thread_states;
  }
  [[nodiscard]] bool collect_thread_states() const { return collect_thread_states_; }

  void set_trace_gpu_submissions(bool trace_gpu_submissions) {
    trace_gpu_submissions_ = trace_gpu_submissions;
  }
  [[nodiscard]] bool trace_gpu_submissions() const { return trace_gpu_submissions_; }

  void set_enable_api(bool enable_api) { enable_api_ = enable_api; }
  [[nodiscard]] bool get_enable_api() const { return enable_api_; }

  void set_enable_introspection(bool enable_introspection) {
    enable_introspection_ = enable_introspection;
  }
  [[nodiscard]] bool get_enable_introspection() const { return enable_introspection_; }

  void set_dynamic_instrumentation_method(
      orbit_grpc_protos::CaptureOptions::DynamicInstrumentationMethod method) {
    dynamic_instrumentation_method_ = method;
  }
  [[nodiscard]] orbit_grpc_protos::CaptureOptions::DynamicInstrumentationMethod
  dynamic_instrumentation_method() const {
    return dynamic_instrumentation_method_;
  }

  void set_samples_per_second(double samples_per_second) {
    samples_per_second_ = samples_per_second;
  }
  [[nodiscard]] double samples_per_second() const { return samples_per_second_; }

  void set_stack_dump_size(uint16_t stack_dump_size) { stack_dump_size_ = stack_dump_size; }
  [[nodiscard]] uint16_t stack_dump_size() const { return stack_dump_size_; }

  void set_unwinding_method(orbit_grpc_protos::CaptureOptions::UnwindingMethod method) {
    unwinding_method_ = method;
  }
  orbit_grpc_protos::CaptureOptions::UnwindingMethod unwinding_method() const {
    return unwinding_method_;
  }

  void set_max_local_marker_depth_per_command_buffer(
      uint64_t max_local_marker_depth_per_command_buffer) {
    max_local_marker_depth_per_command_buffer_ = max_local_marker_depth_per_command_buffer;
  }

  [[nodiscard]] uint64_t max_local_marker_depth_per_command_buffer() {
    return max_local_marker_depth_per_command_buffer_;
  }

  void set_collect_memory_info(bool collect_memory_info) {
    collect_memory_info_ = collect_memory_info;
  }
  [[nodiscard]] bool collect_memory_info() const { return collect_memory_info_; }

  void set_memory_sampling_period_ms(uint64_t memory_sampling_period_ms) {
    memory_sampling_period_ms_ = memory_sampling_period_ms;
  }
  [[nodiscard]] uint64_t memory_sampling_period_ms() const { return memory_sampling_period_ms_; }

  void set_memory_warning_threshold_kb(uint64_t memory_warning_threshold_kb) {
    memory_warning_threshold_kb_ = memory_warning_threshold_kb;
  }
  [[nodiscard]] uint64_t memory_warning_threshold_kb() const {
    return memory_warning_threshold_kb_;
  }

 private:
  const std::thread::id main_thread_id_;
  FunctionInfoSet selected_functions_;
  absl::flat_hash_set<uint64_t> visible_function_ids_;
  uint64_t highlighted_function_id_ = orbit_grpc_protos::kInvalidFunctionId;
  uint64_t highlighted_group_id_ = kOrbitDefaultGroupId;

  TracepointInfoSet selected_tracepoints_;

  int32_t selected_thread_id_ = -1;
  const orbit_client_protos::TimerInfo* selected_timer_ = nullptr;

  // DataManager needs a copy of this so that we can persist user choices like frame tracks between
  // captures.
  UserDefinedCaptureData user_defined_capture_data_;

  bool collect_scheduler_info_ = false;
  bool collect_thread_states_ = false;
  bool trace_gpu_submissions_ = false;
  bool enable_api_ = false;
  bool enable_introspection_ = false;
  orbit_grpc_protos::CaptureOptions::DynamicInstrumentationMethod dynamic_instrumentation_method_{};
  uint64_t max_local_marker_depth_per_command_buffer_ = std::numeric_limits<uint64_t>::max();
  double samples_per_second_ = 0;
  uint16_t stack_dump_size_ = 0;
  orbit_grpc_protos::CaptureOptions::UnwindingMethod unwinding_method_{};

  bool collect_memory_info_ = false;
  uint64_t memory_sampling_period_ms_ = 10;
  uint64_t memory_warning_threshold_kb_ = 1024 * 1024 * 8;

  absl::flat_hash_map<uint32_t, std::vector<orbit_client_protos::CallstackEvent>>
      selected_callstack_events_by_thread_id_;
};

}  // namespace orbit_client_data

#endif  // CLIENT_DATA_DATA_MANAGER_H_
