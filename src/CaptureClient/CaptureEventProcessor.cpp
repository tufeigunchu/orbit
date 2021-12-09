// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "CaptureClient/CaptureEventProcessor.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <llvm/Demangle/Demangle.h>

#include <string>
#include <utility>

#include "CaptureClient/ApiEventProcessor.h"
#include "CaptureClient/GpuQueueSubmissionProcessor.h"
#include "ClientProtos/capture_data.pb.h"
#include "GrpcProtos/Constants.h"
#include "OrbitBase/Logging.h"

namespace orbit_capture_client {

using orbit_client_protos::CallstackEvent;
using orbit_client_protos::CallstackInfo;
using orbit_client_protos::LinuxAddressInfo;
using orbit_client_protos::ThreadStateSliceInfo;
using orbit_client_protos::TimerInfo;

using orbit_grpc_protos::AddressInfo;
using orbit_grpc_protos::Callstack;
using orbit_grpc_protos::CallstackSample;
using orbit_grpc_protos::ClientCaptureEvent;
using orbit_grpc_protos::FunctionCall;
using orbit_grpc_protos::GpuJob;
using orbit_grpc_protos::GpuQueueSubmission;
using orbit_grpc_protos::InternedCallstack;
using orbit_grpc_protos::InternedString;
using orbit_grpc_protos::SchedulingSlice;
using orbit_grpc_protos::ThreadName;
using orbit_grpc_protos::ThreadStateSlice;

namespace {
class CaptureEventProcessorForListener : public CaptureEventProcessor {
 public:
  explicit CaptureEventProcessorForListener(CaptureListener* capture_listener,
                                            std::optional<std::filesystem::path> file_path,
                                            absl::flat_hash_set<uint64_t> frame_track_function_ids)
      : file_path_{std::move(file_path)},
        frame_track_function_ids_(std::move(frame_track_function_ids)),
        capture_listener_(capture_listener),
        api_event_processor_{capture_listener} {}
  ~CaptureEventProcessorForListener() override = default;

  void ProcessEvent(const orbit_grpc_protos::ClientCaptureEvent& event) override;

 private:
  void ProcessCaptureStarted(const orbit_grpc_protos::CaptureStarted& capture_started);
  void ProcessCaptureFinished(const orbit_grpc_protos::CaptureFinished& capture_finished);
  void ProcessSchedulingSlice(const orbit_grpc_protos::SchedulingSlice& scheduling_slice);
  void ProcessInternedCallstack(orbit_grpc_protos::InternedCallstack interned_callstack);
  void ProcessCallstackSample(const orbit_grpc_protos::CallstackSample& callstack_sample);
  void ProcessFunctionCall(const orbit_grpc_protos::FunctionCall& function_call);
  void ProcessInternedString(orbit_grpc_protos::InternedString interned_string);
  void ProcessModuleUpdate(orbit_grpc_protos::ModuleUpdateEvent module_update);
  void ProcessModulesSnapshot(const orbit_grpc_protos::ModulesSnapshot& modules_snapshot);
  void ProcessGpuJob(const orbit_grpc_protos::GpuJob& gpu_job);
  void ProcessThreadName(const orbit_grpc_protos::ThreadName& thread_name);
  void ProcessThreadNamesSnapshot(
      const orbit_grpc_protos::ThreadNamesSnapshot& thread_names_snapshot);
  void ProcessThreadStateSlice(const orbit_grpc_protos::ThreadStateSlice& thread_state_slice);
  void ProcessAddressInfo(const orbit_grpc_protos::AddressInfo& address_info);
  void ProcessInternedTracepointInfo(
      orbit_grpc_protos::InternedTracepointInfo interned_tracepoint_info);
  void ProcessTracepointEvent(const orbit_grpc_protos::TracepointEvent& tracepoint_event);
  void ProcessGpuQueueSubmission(const orbit_grpc_protos::GpuQueueSubmission& gpu_queue_submission);
  void ProcessWarningEvent(const orbit_grpc_protos::WarningEvent& warning_event);
  void ProcessClockResolutionEvent(
      const orbit_grpc_protos::ClockResolutionEvent& clock_resolution_event);
  void ProcessErrorsWithPerfEventOpenEvent(
      const orbit_grpc_protos::ErrorsWithPerfEventOpenEvent& errors_with_perf_event_open_event);
  void ProcessErrorEnablingOrbitApiEvent(
      const orbit_grpc_protos::ErrorEnablingOrbitApiEvent& error_enabling_orbit_api_event);
  void ProcessErrorEnablingUserSpaceInstrumentationEvent(
      const orbit_grpc_protos::ErrorEnablingUserSpaceInstrumentationEvent& error_event);
  void ProcessWarningInstrumentingWithUserSpaceInstrumentationEvent(
      const orbit_grpc_protos::WarningInstrumentingWithUserSpaceInstrumentationEvent&
          warning_event);
  void ProcessLostPerfRecordsEvent(
      const orbit_grpc_protos::LostPerfRecordsEvent& lost_perf_records_event);
  void ProcessOutOfOrderEventsDiscardedEvent(
      const orbit_grpc_protos::OutOfOrderEventsDiscardedEvent& out_of_order_events_discarded_event);

  void ProcessMemoryUsageEvent(const orbit_grpc_protos::MemoryUsageEvent& memory_usage_event);
  void ExtractAndProcessSystemMemoryTrackingTimer(
      uint64_t synchronized_timestamp_ns,
      const orbit_grpc_protos::SystemMemoryUsage& system_memory_usage);
  void ExtractAndProcessCGroupAndProcessMemoryTrackingTimer(
      uint64_t synchronized_timestamp_ns,
      const orbit_grpc_protos::CGroupMemoryUsage& cgroup_memory_usage,
      const orbit_grpc_protos::ProcessMemoryUsage& process_memory_usage);
  void ExtractAndProcessPageFaultsTrackingTimer(
      uint64_t synchronized_timestamp_ns,
      const orbit_grpc_protos::SystemMemoryUsage& system_memory_usage,
      const orbit_grpc_protos::CGroupMemoryUsage& cgroup_memory_usage,
      const orbit_grpc_protos::ProcessMemoryUsage& process_memory_usage);

  std::optional<std::filesystem::path> file_path_;
  absl::flat_hash_set<uint64_t> frame_track_function_ids_;

  absl::flat_hash_map<uint64_t, orbit_grpc_protos::Callstack> callstack_intern_pool;
  absl::flat_hash_map<uint64_t, std::string> string_intern_pool_;
  CaptureListener* capture_listener_ = nullptr;

  absl::flat_hash_set<uint64_t> callstack_hashes_seen_;
  void SendCallstackToListenerIfNecessary(uint64_t callstack_id,
                                          const orbit_grpc_protos::Callstack& callstack);
  uint64_t GetStringHashAndSendToListenerIfNecessary(const std::string& str);

  GpuQueueSubmissionProcessor gpu_queue_submission_processor_;
  ApiEventProcessor api_event_processor_;
};

void CaptureEventProcessorForListener::ProcessEvent(const ClientCaptureEvent& event) {
  switch (event.event_case()) {
    case ClientCaptureEvent::kCaptureStarted:
      ProcessCaptureStarted(event.capture_started());
      break;
    case ClientCaptureEvent::kSchedulingSlice:
      ProcessSchedulingSlice(event.scheduling_slice());
      break;
    case ClientCaptureEvent::kInternedCallstack:
      ProcessInternedCallstack(event.interned_callstack());
      break;
    case ClientCaptureEvent::kCallstackSample:
      ProcessCallstackSample(event.callstack_sample());
      break;
    case ClientCaptureEvent::kFunctionCall:
      ProcessFunctionCall(event.function_call());
      break;
    case ClientCaptureEvent::kInternedString:
      ProcessInternedString(event.interned_string());
      break;
    case ClientCaptureEvent::kGpuJob:
      ProcessGpuJob(event.gpu_job());
      break;
    case ClientCaptureEvent::kThreadName:
      ProcessThreadName(event.thread_name());
      break;
    case ClientCaptureEvent::kThreadStateSlice:
      ProcessThreadStateSlice(event.thread_state_slice());
      break;
    case ClientCaptureEvent::kAddressInfo:
      ProcessAddressInfo(event.address_info());
      break;
    case ClientCaptureEvent::kInternedTracepointInfo:
      ProcessInternedTracepointInfo(event.interned_tracepoint_info());
      break;
    case ClientCaptureEvent::kTracepointEvent:
      ProcessTracepointEvent(event.tracepoint_event());
      break;
    case ClientCaptureEvent::kGpuQueueSubmission:
      ProcessGpuQueueSubmission(event.gpu_queue_submission());
      break;
    case ClientCaptureEvent::kModulesSnapshot:
      ProcessModulesSnapshot(event.modules_snapshot());
      break;
    case ClientCaptureEvent::kThreadNamesSnapshot:
      ProcessThreadNamesSnapshot(event.thread_names_snapshot());
      break;
    case ClientCaptureEvent::kModuleUpdateEvent:
      ProcessModuleUpdate(event.module_update_event());
      break;
    case ClientCaptureEvent::kMemoryUsageEvent:
      ProcessMemoryUsageEvent(event.memory_usage_event());
      break;
    case ClientCaptureEvent::kApiEvent:
      api_event_processor_.ProcessApiEventLegacy(event.api_event());
      break;
    case ClientCaptureEvent::kApiScopeStart:
      api_event_processor_.ProcessApiScopeStart(event.api_scope_start());
      break;
    case ClientCaptureEvent::kApiScopeStartAsync:
      api_event_processor_.ProcessApiScopeStartAsync(event.api_scope_start_async());
      break;
    case ClientCaptureEvent::kApiScopeStop:
      api_event_processor_.ProcessApiScopeStop(event.api_scope_stop());
      break;
    case ClientCaptureEvent::kApiScopeStopAsync:
      api_event_processor_.ProcessApiScopeStopAsync(event.api_scope_stop_async());
      break;
    case ClientCaptureEvent::kApiStringEvent:
      api_event_processor_.ProcessApiStringEvent(event.api_string_event());
      break;
    case ClientCaptureEvent::kApiTrackDouble:
      api_event_processor_.ProcessApiTrackDouble(event.api_track_double());
      break;
    case ClientCaptureEvent::kApiTrackFloat:
      api_event_processor_.ProcessApiTrackFloat(event.api_track_float());
      break;
    case ClientCaptureEvent::kApiTrackInt:
      api_event_processor_.ProcessApiTrackInt(event.api_track_int());
      break;
    case ClientCaptureEvent::kApiTrackInt64:
      api_event_processor_.ProcessApiTrackInt64(event.api_track_int64());
      break;
    case ClientCaptureEvent::kApiTrackUint:
      api_event_processor_.ProcessApiTrackUint(event.api_track_uint());
      break;
    case ClientCaptureEvent::kApiTrackUint64:
      api_event_processor_.ProcessApiTrackUint64(event.api_track_uint64());
      break;
    case ClientCaptureEvent::kWarningEvent:
      ProcessWarningEvent(event.warning_event());
      break;
    case ClientCaptureEvent::kClockResolutionEvent:
      ProcessClockResolutionEvent(event.clock_resolution_event());
      break;
    case ClientCaptureEvent::kErrorsWithPerfEventOpenEvent:
      ProcessErrorsWithPerfEventOpenEvent(event.errors_with_perf_event_open_event());
      break;
    case ClientCaptureEvent::kErrorEnablingOrbitApiEvent:
      ProcessErrorEnablingOrbitApiEvent(event.error_enabling_orbit_api_event());
      break;
    case ClientCaptureEvent::kErrorEnablingUserSpaceInstrumentationEvent:
      ProcessErrorEnablingUserSpaceInstrumentationEvent(
          event.error_enabling_user_space_instrumentation_event());
      break;
    case ClientCaptureEvent::kWarningInstrumentingWithUserSpaceInstrumentationEvent:
      ProcessWarningInstrumentingWithUserSpaceInstrumentationEvent(
          event.warning_instrumenting_with_user_space_instrumentation_event());
      break;
    case ClientCaptureEvent::kLostPerfRecordsEvent:
      ProcessLostPerfRecordsEvent(event.lost_perf_records_event());
      break;
    case ClientCaptureEvent::kOutOfOrderEventsDiscardedEvent:
      ProcessOutOfOrderEventsDiscardedEvent(event.out_of_order_events_discarded_event());
      break;
    case ClientCaptureEvent::kCaptureFinished:
      ProcessCaptureFinished(event.capture_finished());
      break;
    case ClientCaptureEvent::EVENT_NOT_SET:
      ERROR("CaptureEvent::EVENT_NOT_SET read from Capture's gRPC stream");
      break;
    default:
      break;
  }
}

void CaptureEventProcessorForListener::ProcessCaptureStarted(
    const orbit_grpc_protos::CaptureStarted& capture_started) {
  capture_listener_->OnCaptureStarted(capture_started, file_path_, frame_track_function_ids_);
}

void CaptureEventProcessorForListener::ProcessCaptureFinished(
    const orbit_grpc_protos::CaptureFinished& capture_finished) {
  capture_listener_->OnCaptureFinished(capture_finished);
}

void CaptureEventProcessorForListener::ProcessSchedulingSlice(
    const SchedulingSlice& scheduling_slice) {
  TimerInfo timer_info;
  uint64_t in_timestamp_ns = scheduling_slice.out_timestamp_ns() - scheduling_slice.duration_ns();
  timer_info.set_start(in_timestamp_ns);
  timer_info.set_end(scheduling_slice.out_timestamp_ns());
  timer_info.set_process_id(scheduling_slice.pid());
  timer_info.set_thread_id(scheduling_slice.tid());
  timer_info.set_processor(static_cast<int8_t>(scheduling_slice.core()));
  timer_info.set_depth(timer_info.processor());
  timer_info.set_type(TimerInfo::kCoreActivity);

  gpu_queue_submission_processor_.UpdateBeginCaptureTime(in_timestamp_ns);

  capture_listener_->OnTimer(timer_info);
}

void CaptureEventProcessorForListener::ProcessInternedCallstack(
    InternedCallstack interned_callstack) {
  if (callstack_intern_pool.contains(interned_callstack.key())) {
    ERROR("Overwriting InternedCallstack with key %llu", interned_callstack.key());
  }
  callstack_intern_pool.emplace(interned_callstack.key(),
                                std::move(*interned_callstack.mutable_intern()));
}

void CaptureEventProcessorForListener::ProcessCallstackSample(
    const CallstackSample& callstack_sample) {
  uint64_t callstack_id = callstack_sample.callstack_id();
  Callstack callstack = callstack_intern_pool[callstack_id];

  SendCallstackToListenerIfNecessary(callstack_id, callstack);

  CallstackEvent callstack_event;
  callstack_event.set_time(callstack_sample.timestamp_ns());
  callstack_event.set_callstack_id(callstack_id);
  // Note: callstack_sample.pid() is available, but currently dropped.
  callstack_event.set_thread_id(callstack_sample.tid());

  gpu_queue_submission_processor_.UpdateBeginCaptureTime(callstack_sample.timestamp_ns());

  capture_listener_->OnCallstackEvent(std::move(callstack_event));
}

void CaptureEventProcessorForListener::ProcessFunctionCall(const FunctionCall& function_call) {
  TimerInfo timer_info;
  timer_info.set_process_id(function_call.pid());
  timer_info.set_thread_id(function_call.tid());
  uint64_t begin_timestamp_ns = function_call.end_timestamp_ns() - function_call.duration_ns();
  timer_info.set_start(begin_timestamp_ns);
  timer_info.set_end(function_call.end_timestamp_ns());
  timer_info.set_depth(static_cast<uint8_t>(function_call.depth()));
  timer_info.set_function_id(function_call.function_id());
  timer_info.set_user_data_key(function_call.return_value());
  timer_info.set_processor(-1);
  timer_info.set_type(TimerInfo::kNone);

  for (int i = 0; i < function_call.registers_size(); ++i) {
    timer_info.add_registers(function_call.registers(i));
  }

  gpu_queue_submission_processor_.UpdateBeginCaptureTime(begin_timestamp_ns);

  capture_listener_->OnTimer(timer_info);
}

void CaptureEventProcessorForListener::ProcessInternedString(InternedString interned_string) {
  if (string_intern_pool_.contains(interned_string.key())) {
    ERROR("Overwriting InternedString with key %llu", interned_string.key());
  }
  capture_listener_->OnKeyAndString(interned_string.key(), interned_string.intern());
  string_intern_pool_.emplace(interned_string.key(), std::move(*interned_string.mutable_intern()));
}

void CaptureEventProcessorForListener::ProcessModuleUpdate(
    orbit_grpc_protos::ModuleUpdateEvent module_update) {
  capture_listener_->OnModuleUpdate(module_update.timestamp_ns(),
                                    std::move(*module_update.mutable_module()));
}

void CaptureEventProcessorForListener::ProcessModulesSnapshot(
    const orbit_grpc_protos::ModulesSnapshot& modules_snapshot) {
  capture_listener_->OnModulesSnapshot(
      modules_snapshot.timestamp_ns(),
      {modules_snapshot.modules().begin(), modules_snapshot.modules().end()});
}

void CaptureEventProcessorForListener::ProcessGpuJob(const GpuJob& gpu_job) {
  uint64_t timeline_key = gpu_job.timeline_key();

  uint32_t process_id = gpu_job.pid();
  uint32_t thread_id = gpu_job.tid();
  uint64_t amdgpu_cs_ioctl_time_ns = gpu_job.amdgpu_cs_ioctl_time_ns();

  constexpr const char* kSwQueue = "sw queue";
  uint64_t sw_queue_key = GetStringHashAndSendToListenerIfNecessary(kSwQueue);

  TimerInfo timer_user_to_sched;
  timer_user_to_sched.set_process_id(process_id);
  timer_user_to_sched.set_thread_id(thread_id);
  timer_user_to_sched.set_start(amdgpu_cs_ioctl_time_ns);
  timer_user_to_sched.set_end(gpu_job.amdgpu_sched_run_job_time_ns());
  timer_user_to_sched.set_depth(gpu_job.depth());
  timer_user_to_sched.set_user_data_key(sw_queue_key);
  timer_user_to_sched.set_timeline_hash(timeline_key);
  timer_user_to_sched.set_processor(-1);
  timer_user_to_sched.set_type(TimerInfo::kGpuActivity);

  gpu_queue_submission_processor_.UpdateBeginCaptureTime(gpu_job.amdgpu_cs_ioctl_time_ns());

  capture_listener_->OnTimer(timer_user_to_sched);

  constexpr const char* kHwQueue = "hw queue";
  uint64_t hw_queue_key = GetStringHashAndSendToListenerIfNecessary(kHwQueue);

  TimerInfo timer_sched_to_start;
  timer_sched_to_start.set_process_id(process_id);
  timer_sched_to_start.set_thread_id(thread_id);
  timer_sched_to_start.set_start(gpu_job.amdgpu_sched_run_job_time_ns());
  timer_sched_to_start.set_end(gpu_job.gpu_hardware_start_time_ns());
  timer_sched_to_start.set_depth(gpu_job.depth());
  timer_sched_to_start.set_user_data_key(hw_queue_key);
  timer_sched_to_start.set_timeline_hash(timeline_key);
  timer_sched_to_start.set_processor(-1);
  timer_sched_to_start.set_type(TimerInfo::kGpuActivity);
  capture_listener_->OnTimer(timer_sched_to_start);

  constexpr const char* kHwExecution = "hw execution";
  uint64_t hw_execution_key = GetStringHashAndSendToListenerIfNecessary(kHwExecution);

  TimerInfo timer_start_to_finish;
  timer_start_to_finish.set_process_id(process_id);
  timer_start_to_finish.set_thread_id(thread_id);
  timer_start_to_finish.set_start(gpu_job.gpu_hardware_start_time_ns());
  timer_start_to_finish.set_end(gpu_job.dma_fence_signaled_time_ns());
  timer_start_to_finish.set_depth(gpu_job.depth());
  timer_start_to_finish.set_user_data_key(hw_execution_key);
  timer_start_to_finish.set_timeline_hash(timeline_key);
  timer_start_to_finish.set_processor(-1);
  timer_start_to_finish.set_type(TimerInfo::kGpuActivity);
  capture_listener_->OnTimer(timer_start_to_finish);

  std::vector<TimerInfo> vulkan_related_timers = gpu_queue_submission_processor_.ProcessGpuJob(
      gpu_job, string_intern_pool_,
      [this](const std::string& str) { return GetStringHashAndSendToListenerIfNecessary(str); });
  for (const TimerInfo& timer : vulkan_related_timers) {
    capture_listener_->OnTimer(timer);
  }
}

void CaptureEventProcessorForListener::ProcessGpuQueueSubmission(
    const GpuQueueSubmission& gpu_queue_submission) {
  std::vector<TimerInfo> vulkan_related_timers =
      gpu_queue_submission_processor_.ProcessGpuQueueSubmission(
          gpu_queue_submission, string_intern_pool_, [this](const std::string& str) {
            return GetStringHashAndSendToListenerIfNecessary(str);
          });
  for (const TimerInfo& timer : vulkan_related_timers) {
    capture_listener_->OnTimer(timer);
  }
}

void CaptureEventProcessorForListener::ProcessMemoryUsageEvent(
    const orbit_grpc_protos::MemoryUsageEvent& memory_usage_event) {
  if (memory_usage_event.has_system_memory_usage()) {
    ExtractAndProcessSystemMemoryTrackingTimer(memory_usage_event.timestamp_ns(),
                                               memory_usage_event.system_memory_usage());
  }

  if (memory_usage_event.has_cgroup_memory_usage() &&
      memory_usage_event.has_process_memory_usage()) {
    ExtractAndProcessCGroupAndProcessMemoryTrackingTimer(memory_usage_event.timestamp_ns(),
                                                         memory_usage_event.cgroup_memory_usage(),
                                                         memory_usage_event.process_memory_usage());
  }

  if (memory_usage_event.has_system_memory_usage() &&
      memory_usage_event.has_cgroup_memory_usage() &&
      memory_usage_event.has_process_memory_usage()) {
    ExtractAndProcessPageFaultsTrackingTimer(
        memory_usage_event.timestamp_ns(), memory_usage_event.system_memory_usage(),
        memory_usage_event.cgroup_memory_usage(), memory_usage_event.process_memory_usage());
  }
}

void CaptureEventProcessorForListener::ExtractAndProcessSystemMemoryTrackingTimer(
    uint64_t synchronized_timestamp_ns,
    const orbit_grpc_protos::SystemMemoryUsage& system_memory_usage) {
  TimerInfo timer;
  timer.set_type(TimerInfo::kSystemMemoryUsage);
  timer.set_start(synchronized_timestamp_ns);
  timer.set_end(synchronized_timestamp_ns);

  // The RepeatedField<Element>::Set(int index, const Element & value) method won't update the
  // current_size_ of the repeated field. If the current_size_ == 0, the repeated field will be
  // ignored when copying the protobuf. Therefore, if encoding the following memory information
  // into TimerInfo registers with the Set method, the registers field of the copied TimerInfo
  // would be empty.
  std::vector<uint64_t> encoded_values(static_cast<size_t>(SystemMemoryUsageEncodingIndex::kEnd));
  encoded_values[static_cast<size_t>(SystemMemoryUsageEncodingIndex::kTotalKb)] =
      orbit_api::Encode<uint64_t>(system_memory_usage.total_kb());
  encoded_values[static_cast<size_t>(SystemMemoryUsageEncodingIndex::kFreeKb)] =
      orbit_api::Encode<uint64_t>(system_memory_usage.free_kb());
  encoded_values[static_cast<size_t>(SystemMemoryUsageEncodingIndex::kAvailableKb)] =
      orbit_api::Encode<uint64_t>(system_memory_usage.available_kb());
  encoded_values[static_cast<size_t>(SystemMemoryUsageEncodingIndex::kBuffersKb)] =
      orbit_api::Encode<uint64_t>(system_memory_usage.buffers_kb());
  encoded_values[static_cast<size_t>(SystemMemoryUsageEncodingIndex::kCachedKb)] =
      orbit_api::Encode<uint64_t>(system_memory_usage.cached_kb());

  *timer.mutable_registers() = {encoded_values.begin(), encoded_values.end()};

  capture_listener_->OnTimer(timer);
}

void CaptureEventProcessorForListener::ExtractAndProcessCGroupAndProcessMemoryTrackingTimer(
    uint64_t synchronized_timestamp_ns,
    const orbit_grpc_protos::CGroupMemoryUsage& cgroup_memory_usage,
    const orbit_grpc_protos::ProcessMemoryUsage& process_memory_usage) {
  TimerInfo timer;
  timer.set_type(TimerInfo::kCGroupAndProcessMemoryUsage);
  timer.set_start(synchronized_timestamp_ns);
  timer.set_end(synchronized_timestamp_ns);
  timer.set_process_id(process_memory_usage.pid());

  // TODO(b/192335025): Change to use dedicated classes / structs to store information for
  // `MemoryTrack`.
  std::vector<uint64_t> encoded_values(
      static_cast<size_t>(CGroupAndProcessMemoryUsageEncodingIndex::kEnd));
  encoded_values[static_cast<size_t>(CGroupAndProcessMemoryUsageEncodingIndex::kCGroupNameHash)] =
      GetStringHashAndSendToListenerIfNecessary(cgroup_memory_usage.cgroup_name());
  encoded_values[static_cast<size_t>(CGroupAndProcessMemoryUsageEncodingIndex::kCGroupLimitBytes)] =
      orbit_api::Encode<uint64_t>(cgroup_memory_usage.limit_bytes());
  encoded_values[static_cast<size_t>(CGroupAndProcessMemoryUsageEncodingIndex::kCGroupRssBytes)] =
      orbit_api::Encode<uint64_t>(cgroup_memory_usage.rss_bytes());
  encoded_values[static_cast<size_t>(
      CGroupAndProcessMemoryUsageEncodingIndex::kCGroupMappedFileBytes)] =
      orbit_api::Encode<uint64_t>(cgroup_memory_usage.mapped_file_bytes());
  encoded_values[static_cast<size_t>(CGroupAndProcessMemoryUsageEncodingIndex::kProcessRssAnonKb)] =
      orbit_api::Encode<uint64_t>(process_memory_usage.rss_anon_kb());

  *timer.mutable_registers() = {encoded_values.begin(), encoded_values.end()};

  capture_listener_->OnTimer(timer);
}

void CaptureEventProcessorForListener::ExtractAndProcessPageFaultsTrackingTimer(
    uint64_t synchronized_timestamp_ns,
    const orbit_grpc_protos::SystemMemoryUsage& system_memory_usage,
    const orbit_grpc_protos::CGroupMemoryUsage& cgroup_memory_usage,
    const orbit_grpc_protos::ProcessMemoryUsage& process_memory_usage) {
  TimerInfo timer;
  timer.set_type(TimerInfo::kPageFaults);
  timer.set_start(synchronized_timestamp_ns);
  timer.set_end(synchronized_timestamp_ns);
  timer.set_process_id(process_memory_usage.pid());

  std::vector<uint64_t> encoded_values(static_cast<size_t>(PageFaultsEncodingIndex::kEnd));
  encoded_values[static_cast<size_t>(PageFaultsEncodingIndex::kSystemPageFaults)] =
      orbit_api::Encode<uint64_t>(system_memory_usage.pgfault());
  encoded_values[static_cast<size_t>(PageFaultsEncodingIndex::kSystemMajorPageFaults)] =
      orbit_api::Encode<uint64_t>(system_memory_usage.pgmajfault());
  encoded_values[static_cast<size_t>(PageFaultsEncodingIndex::kCGroupNameHash)] =
      GetStringHashAndSendToListenerIfNecessary(cgroup_memory_usage.cgroup_name());
  encoded_values[static_cast<size_t>(PageFaultsEncodingIndex::kCGroupPageFaults)] =
      orbit_api::Encode<uint64_t>(cgroup_memory_usage.pgfault());
  encoded_values[static_cast<size_t>(PageFaultsEncodingIndex::kCGroupMajorPageFaults)] =
      orbit_api::Encode<uint64_t>(cgroup_memory_usage.pgmajfault());
  encoded_values[static_cast<size_t>(PageFaultsEncodingIndex::kProcessMinorPageFaults)] =
      orbit_api::Encode<uint64_t>(process_memory_usage.minflt());
  encoded_values[static_cast<size_t>(PageFaultsEncodingIndex::kProcessMajorPageFaults)] =
      orbit_api::Encode<uint64_t>(process_memory_usage.majflt());

  *timer.mutable_registers() = {encoded_values.begin(), encoded_values.end()};

  capture_listener_->OnTimer(timer);
}

void CaptureEventProcessorForListener::ProcessThreadName(const ThreadName& thread_name) {
  // Note: thread_name.pid() is available, but currently dropped.
  capture_listener_->OnThreadName(thread_name.tid(), thread_name.name());
}

void CaptureEventProcessorForListener::ProcessThreadNamesSnapshot(
    const orbit_grpc_protos::ThreadNamesSnapshot& thread_names_snapshot) {
  for (const auto& thread_name : thread_names_snapshot.thread_names()) {
    capture_listener_->OnThreadName(thread_name.tid(), thread_name.name());
  }
}

void CaptureEventProcessorForListener::ProcessThreadStateSlice(
    const ThreadStateSlice& thread_state_slice) {
  ThreadStateSliceInfo slice_info;
  slice_info.set_tid(thread_state_slice.tid());
  switch (thread_state_slice.thread_state()) {
    case ThreadStateSlice::kRunning:
      slice_info.set_thread_state(ThreadStateSliceInfo::kRunning);
      break;
    case ThreadStateSlice::kRunnable:
      slice_info.set_thread_state(ThreadStateSliceInfo::kRunnable);
      break;
    case ThreadStateSlice::kInterruptibleSleep:
      slice_info.set_thread_state(ThreadStateSliceInfo::kInterruptibleSleep);
      break;
    case ThreadStateSlice::kUninterruptibleSleep:
      slice_info.set_thread_state(ThreadStateSliceInfo::kUninterruptibleSleep);
      break;
    case ThreadStateSlice::kStopped:
      slice_info.set_thread_state(ThreadStateSliceInfo::kStopped);
      break;
    case ThreadStateSlice::kTraced:
      slice_info.set_thread_state(ThreadStateSliceInfo::kTraced);
      break;
    case ThreadStateSlice::kDead:
      slice_info.set_thread_state(ThreadStateSliceInfo::kDead);
      break;
    case ThreadStateSlice::kZombie:
      slice_info.set_thread_state(ThreadStateSliceInfo::kZombie);
      break;
    case ThreadStateSlice::kParked:
      slice_info.set_thread_state(ThreadStateSliceInfo::kParked);
      break;
    case ThreadStateSlice::kIdle:
      slice_info.set_thread_state(ThreadStateSliceInfo::kIdle);
      break;
    default:
      UNREACHABLE();
  }
  slice_info.set_begin_timestamp_ns(thread_state_slice.end_timestamp_ns() -
                                    thread_state_slice.duration_ns());
  slice_info.set_end_timestamp_ns(thread_state_slice.end_timestamp_ns());

  gpu_queue_submission_processor_.UpdateBeginCaptureTime(slice_info.begin_timestamp_ns());

  capture_listener_->OnThreadStateSlice(std::move(slice_info));
}

void CaptureEventProcessorForListener::ProcessAddressInfo(const AddressInfo& address_info) {
  CHECK(string_intern_pool_.contains(address_info.function_name_key()));
  CHECK(string_intern_pool_.contains(address_info.module_name_key()));
  std::string function_name = string_intern_pool_.at(address_info.function_name_key());
  std::string module_name = string_intern_pool_.at(address_info.module_name_key());

  LinuxAddressInfo linux_address_info;
  linux_address_info.set_absolute_address(address_info.absolute_address());
  linux_address_info.set_module_path(module_name);
  linux_address_info.set_function_name(llvm::demangle(function_name));
  linux_address_info.set_offset_in_function(address_info.offset_in_function());
  capture_listener_->OnAddressInfo(linux_address_info);
}

void CaptureEventProcessorForListener::SendCallstackToListenerIfNecessary(
    uint64_t callstack_id, const Callstack& callstack) {
  if (callstack_hashes_seen_.contains(callstack_id)) {
    return;
  }
  callstack_hashes_seen_.emplace(callstack_id);

  CallstackInfo callstack_info;
  *callstack_info.mutable_frames() = {callstack.pcs().begin(), callstack.pcs().end()};
  switch (callstack.type()) {
    case orbit_grpc_protos::Callstack::kComplete:
      callstack_info.set_type(CallstackInfo::kComplete);
      break;
    case orbit_grpc_protos::Callstack::kDwarfUnwindingError:
      callstack_info.set_type(CallstackInfo::kDwarfUnwindingError);
      break;
    case orbit_grpc_protos::Callstack::kFramePointerUnwindingError:
      callstack_info.set_type(CallstackInfo::kFramePointerUnwindingError);
      break;
    case orbit_grpc_protos::Callstack::kInUprobes:
      callstack_info.set_type(CallstackInfo::kInUprobes);
      break;
    case orbit_grpc_protos::Callstack::kInUserSpaceInstrumentation:
      callstack_info.set_type(CallstackInfo::kInUserSpaceInstrumentation);
      break;
    case orbit_grpc_protos::Callstack::kCallstackPatchingFailed:
      callstack_info.set_type(CallstackInfo::kCallstackPatchingFailed);
      break;
    case orbit_grpc_protos::Callstack::kStackTopForDwarfUnwindingTooSmall:
      callstack_info.set_type(CallstackInfo::kStackTopForDwarfUnwindingTooSmall);
      break;
    case orbit_grpc_protos::Callstack::kStackTopDwarfUnwindingError:
      callstack_info.set_type(CallstackInfo::kStackTopDwarfUnwindingError);
      break;
    case orbit_grpc_protos::
        Callstack_CallstackType_Callstack_CallstackType_INT_MIN_SENTINEL_DO_NOT_USE_:
      [[fallthrough]];
    case orbit_grpc_protos::
        Callstack_CallstackType_Callstack_CallstackType_INT_MAX_SENTINEL_DO_NOT_USE_:
      UNREACHABLE();
  }

  capture_listener_->OnUniqueCallstack(callstack_id, callstack_info);
}

void CaptureEventProcessorForListener::ProcessInternedTracepointInfo(
    orbit_grpc_protos::InternedTracepointInfo interned_tracepoint_info) {
  capture_listener_->OnUniqueTracepointInfo(interned_tracepoint_info.key(),
                                            std::move(*interned_tracepoint_info.mutable_intern()));
}
void CaptureEventProcessorForListener::ProcessTracepointEvent(
    const orbit_grpc_protos::TracepointEvent& tracepoint_event) {
  uint64_t key = tracepoint_event.tracepoint_info_key();

  orbit_client_protos::TracepointEventInfo tracepoint_event_info;
  tracepoint_event_info.set_pid(tracepoint_event.pid());
  tracepoint_event_info.set_tid(tracepoint_event.tid());
  tracepoint_event_info.set_time(tracepoint_event.timestamp_ns());
  tracepoint_event_info.set_cpu(tracepoint_event.cpu());
  tracepoint_event_info.set_tracepoint_info_key(key);

  gpu_queue_submission_processor_.UpdateBeginCaptureTime(tracepoint_event.timestamp_ns());

  capture_listener_->OnTracepointEvent(std::move(tracepoint_event_info));
}

void CaptureEventProcessorForListener::ProcessWarningEvent(
    const orbit_grpc_protos::WarningEvent& warning_event) {
  capture_listener_->OnWarningEvent(warning_event);
}

void CaptureEventProcessorForListener::ProcessClockResolutionEvent(
    const orbit_grpc_protos::ClockResolutionEvent& clock_resolution_event) {
  capture_listener_->OnClockResolutionEvent(clock_resolution_event);
}

void CaptureEventProcessorForListener::ProcessErrorsWithPerfEventOpenEvent(
    const orbit_grpc_protos::ErrorsWithPerfEventOpenEvent& errors_with_perf_event_open_event) {
  capture_listener_->OnErrorsWithPerfEventOpenEvent(errors_with_perf_event_open_event);
}

void CaptureEventProcessorForListener::ProcessErrorEnablingOrbitApiEvent(
    const orbit_grpc_protos::ErrorEnablingOrbitApiEvent& error_enabling_orbit_api_event) {
  capture_listener_->OnErrorEnablingOrbitApiEvent(error_enabling_orbit_api_event);
}

void CaptureEventProcessorForListener::ProcessErrorEnablingUserSpaceInstrumentationEvent(
    const orbit_grpc_protos::ErrorEnablingUserSpaceInstrumentationEvent& error_event) {
  capture_listener_->OnErrorEnablingUserSpaceInstrumentationEvent(error_event);
}

void CaptureEventProcessorForListener::ProcessWarningInstrumentingWithUserSpaceInstrumentationEvent(
    const orbit_grpc_protos::WarningInstrumentingWithUserSpaceInstrumentationEvent& warning_event) {
  capture_listener_->OnWarningInstrumentingWithUserSpaceInstrumentationEvent(warning_event);
}

void CaptureEventProcessorForListener::ProcessLostPerfRecordsEvent(
    const orbit_grpc_protos::LostPerfRecordsEvent& lost_perf_records_event) {
  capture_listener_->OnLostPerfRecordsEvent(lost_perf_records_event);
}

void CaptureEventProcessorForListener::ProcessOutOfOrderEventsDiscardedEvent(
    const orbit_grpc_protos::OutOfOrderEventsDiscardedEvent& out_of_order_events_discarded_event) {
  capture_listener_->OnOutOfOrderEventsDiscardedEvent(out_of_order_events_discarded_event);
}

uint64_t CaptureEventProcessorForListener::GetStringHashAndSendToListenerIfNecessary(
    const std::string& str) {
  uint64_t hash = std::hash<std::string>{}(str);
  if (!string_intern_pool_.contains(hash)) {
    string_intern_pool_.emplace(hash, str);
    capture_listener_->OnKeyAndString(hash, str);
  }
  return hash;
}

}  // namespace

std::unique_ptr<CaptureEventProcessor> CaptureEventProcessor::CreateForCaptureListener(
    CaptureListener* capture_listener, std::optional<std::filesystem::path> file_path,
    absl::flat_hash_set<uint64_t> frame_track_function_ids) {
  return std::make_unique<CaptureEventProcessorForListener>(capture_listener, std::move(file_path),
                                                            std::move(frame_track_function_ids));
}

}  // namespace orbit_capture_client
