// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAPTURE_CLIENT_CAPTURE_LISTENER_H_
#define CAPTURE_CLIENT_CAPTURE_LISTENER_H_

#include "ClientData/ProcessData.h"
#include "ClientData/TracepointCustom.h"
#include "ClientData/UserDefinedCaptureData.h"
#include "ClientProtos/capture_data.pb.h"
#include "GrpcProtos/capture.pb.h"
#include "OrbitBase/Result.h"
#include "absl/container/flat_hash_set.h"

namespace orbit_capture_client {

class CaptureListener {
 public:
  enum class CaptureOutcome { kComplete, kCancelled };

  virtual ~CaptureListener() = default;

  virtual void OnCaptureStarted(const orbit_grpc_protos::CaptureStarted& capture_started,
                                std::optional<std::filesystem::path> file_path,
                                absl::flat_hash_set<uint64_t> frame_track_function_ids) = 0;
  virtual void OnCaptureFinished(const orbit_grpc_protos::CaptureFinished& capture_finished) = 0;

  virtual void OnTimer(const orbit_client_protos::TimerInfo& timer_info) = 0;
  virtual void OnKeyAndString(uint64_t key, std::string str) = 0;
  virtual void OnUniqueCallstack(uint64_t callstack_id,
                                 orbit_client_protos::CallstackInfo callstack) = 0;
  virtual void OnCallstackEvent(orbit_client_protos::CallstackEvent callstack_event) = 0;
  virtual void OnThreadName(uint32_t thread_id, std::string thread_name) = 0;
  virtual void OnModuleUpdate(uint64_t timestamp_ns, orbit_grpc_protos::ModuleInfo module_info) = 0;
  virtual void OnModulesSnapshot(uint64_t timestamp_ns,
                                 std::vector<orbit_grpc_protos::ModuleInfo> module_infos) = 0;
  virtual void OnThreadStateSlice(orbit_client_protos::ThreadStateSliceInfo thread_state_slice) = 0;
  virtual void OnAddressInfo(orbit_client_protos::LinuxAddressInfo address_info) = 0;
  virtual void OnUniqueTracepointInfo(uint64_t key,
                                      orbit_grpc_protos::TracepointInfo tracepoint_info) = 0;
  virtual void OnTracepointEvent(
      orbit_client_protos::TracepointEventInfo tracepoint_event_info) = 0;
  virtual void OnApiStringEvent(const orbit_client_protos::ApiStringEvent&) = 0;
  virtual void OnApiTrackValue(const orbit_client_protos::ApiTrackValue&) = 0;
  virtual void OnWarningEvent(orbit_grpc_protos::WarningEvent warning_event) = 0;
  virtual void OnClockResolutionEvent(
      orbit_grpc_protos::ClockResolutionEvent clock_resolution_event) = 0;
  virtual void OnErrorsWithPerfEventOpenEvent(
      orbit_grpc_protos::ErrorsWithPerfEventOpenEvent errors_with_perf_event_open_event) = 0;
  virtual void OnErrorEnablingOrbitApiEvent(
      orbit_grpc_protos::ErrorEnablingOrbitApiEvent error_enabling_orbit_api_event) = 0;
  virtual void OnErrorEnablingUserSpaceInstrumentationEvent(
      orbit_grpc_protos::ErrorEnablingUserSpaceInstrumentationEvent error_event) = 0;
  virtual void OnWarningInstrumentingWithUserSpaceInstrumentationEvent(
      orbit_grpc_protos::WarningInstrumentingWithUserSpaceInstrumentationEvent warning_event) = 0;
  virtual void OnLostPerfRecordsEvent(
      orbit_grpc_protos::LostPerfRecordsEvent lost_perf_records_event) = 0;
  virtual void OnOutOfOrderEventsDiscardedEvent(
      orbit_grpc_protos::OutOfOrderEventsDiscardedEvent out_of_order_events_discarded_event) = 0;
};

}  // namespace orbit_capture_client

#endif  // CAPTURE_CLIENT_CAPTURE_LISTENER_H_
