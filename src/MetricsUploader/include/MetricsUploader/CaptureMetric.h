// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_UPLOADER_CAPTURE_METRIC_H_
#define METRICS_UPLOADER_CAPTURE_METRIC_H_

#include <stdint.h>

#include <chrono>
#include <filesystem>

#include "MetricsUploader/MetricsUploader.h"
#include "MetricsUploader/orbit_log_event.pb.h"

namespace orbit_metrics_uploader {

struct CaptureStartData {
  int64_t number_of_instrumented_functions = 0;
  int64_t number_of_frame_tracks = 0;
  orbit_metrics_uploader::OrbitCaptureData_ThreadStates thread_states =
      orbit_metrics_uploader::OrbitCaptureData_ThreadStates_THREAD_STATES_UNKNOWN;
  int64_t memory_information_sampling_period_ms = 0;
  orbit_metrics_uploader::OrbitCaptureData_LibOrbitVulkanLayer lib_orbit_vulkan_layer =
      orbit_metrics_uploader::OrbitCaptureData_LibOrbitVulkanLayer_LIB_ORBIT_VULKAN_LAYER_UNKNOWN;
  orbit_metrics_uploader::OrbitCaptureData_LocalMarkerDepthPerCommandBuffer
      local_marker_depth_per_command_buffer = orbit_metrics_uploader::
          OrbitCaptureData_LocalMarkerDepthPerCommandBuffer_LOCAL_MARKER_DEPTH_PER_COMMAND_BUFFER_UNKNOWN;
  uint64_t max_local_marker_depth_per_command_buffer = 0;
  orbit_metrics_uploader::OrbitCaptureData_DynamicInstrumentationMethod
      dynamic_instrumentation_method = orbit_metrics_uploader::
          OrbitCaptureData_DynamicInstrumentationMethod_DYNAMIC_INSTRUMENTATION_METHOD_UNKNOWN;
  uint64_t callstack_samples_per_second = 0;
  orbit_metrics_uploader::OrbitCaptureData_CallstackUnwindingMethod callstack_unwinding_method =
      orbit_metrics_uploader::
          OrbitCaptureData_CallstackUnwindingMethod_CALLSTACK_UNWINDING_METHOD_UNKNOWN;
};

struct CaptureCompleteData {
  int64_t number_of_instrumented_function_timers = 0;
  int64_t number_of_gpu_activity_timers = 0;
  int64_t number_of_vulkan_layer_gpu_command_buffer_timers = 0;
  int64_t number_of_vulkan_layer_gpu_debug_marker_timers = 0;
  int64_t number_of_manual_start_timers = 0;
  int64_t number_of_manual_stop_timers = 0;
  int64_t number_of_manual_start_async_timers = 0;
  int64_t number_of_manual_stop_async_timers = 0;
  int64_t number_of_manual_tracked_value_timers = 0;
  std::filesystem::path file_path;
};

class CaptureMetric {
 public:
  explicit CaptureMetric(MetricsUploader* uploader, const CaptureStartData& start_data);

  void SetCaptureCompleteData(const CaptureCompleteData& complete_data);

  bool SendCaptureFailed();
  bool SendCaptureCancelled();
  bool SendCaptureSucceeded(std::chrono::milliseconds duration_in_milliseconds);

 private:
  MetricsUploader* uploader_;
  OrbitCaptureData capture_data_;
  OrbitLogEvent::StatusCode status_code_ = OrbitLogEvent::UNKNOWN_STATUS;
  std::chrono::steady_clock::time_point start_;
  std::filesystem::path file_path_;
};

}  // namespace orbit_metrics_uploader

#endif  // METRICS_UPLOADER_CAPTURE_METRIC_H_