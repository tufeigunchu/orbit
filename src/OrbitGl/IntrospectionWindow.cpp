// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "IntrospectionWindow.h"

#include "App.h"
#include "ClientProtos/capture_data.pb.h"
#include "OrbitBase/Logging.h"

using orbit_client_data::CaptureData;
using orbit_client_protos::TimerInfo;
using orbit_grpc_protos::CaptureStarted;

namespace {
void HandleCaptureEvent(const orbit_api::ApiScopeStart& scope_start,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiScopeStart api_event;
  scope_start.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiScopeStart(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiScopeStop& scope_stop,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiScopeStop api_event;
  scope_stop.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiScopeStop(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiScopeStartAsync& scope_start_async,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiScopeStartAsync api_event;
  scope_start_async.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiScopeStartAsync(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiScopeStopAsync& scope_stop_async,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiScopeStopAsync api_event;
  scope_stop_async.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiScopeStopAsync(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiStringEvent& string_event,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiStringEvent api_event;
  string_event.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiStringEvent(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiTrackDouble& track_double,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiTrackDouble api_event;
  track_double.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiTrackDouble(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiTrackFloat& track_float,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiTrackFloat api_event;
  track_float.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiTrackFloat(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiTrackInt& track_int,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiTrackInt api_event;
  track_int.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiTrackInt(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiTrackInt64& track_int64,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiTrackInt64 api_event;
  track_int64.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiTrackInt64(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiTrackUint& track_uint,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiTrackUint api_event;
  track_uint.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiTrackUint(api_event);
}

void HandleCaptureEvent(const orbit_api::ApiTrackUint64& track_uint64,
                        orbit_capture_client::ApiEventProcessor* api_event_processor) {
  orbit_grpc_protos::ApiTrackUint64 api_event;
  track_uint64.CopyToGrpcProto(&api_event);
  api_event_processor->ProcessApiTrackUint64(api_event);
}

// The variant type `ApiEventVariant` requires to contain `std::monostate` in order to be default-
// constructable. However, that state is never expected to be called in the visitor.
void HandleCaptureEvent(const std::monostate& /*unused*/,
                        orbit_capture_client::ApiEventProcessor* /*unused*/) {
  UNREACHABLE();
}

class IntrospectionCaptureListener : public orbit_capture_client::CaptureListener {
 public:
  explicit IntrospectionCaptureListener(IntrospectionWindow* introspection_window)
      : introspection_window_{introspection_window} {
    CHECK(introspection_window_ != nullptr);
  }

 private:
  void OnTimer(const orbit_client_protos::TimerInfo& timer_info) override {
    introspection_window_->GetTimeGraph()->ProcessTimer(timer_info, nullptr);
  }

  void OnApiStringEvent(const orbit_client_protos::ApiStringEvent& api_string_event) override {
    introspection_window_->GetTimeGraph()->ProcessApiStringEvent(api_string_event);
  }

  void OnApiTrackValue(const orbit_client_protos::ApiTrackValue& api_track_value) override {
    introspection_window_->GetTimeGraph()->ProcessApiTrackValueEvent(api_track_value);
  }

  void OnCaptureStarted(const orbit_grpc_protos::CaptureStarted& /*capture_started*/,
                        std::optional<std::filesystem::path> /*file_path*/,
                        absl::flat_hash_set<uint64_t> /*frame_track_function_ids*/) override {
    UNREACHABLE();
  }
  void OnCaptureFinished(const orbit_grpc_protos::CaptureFinished& /*capture_finished*/) override {
    UNREACHABLE();
  }
  void OnKeyAndString(uint64_t /*key*/, std::string /*str*/) override { UNREACHABLE(); }
  void OnUniqueCallstack(uint64_t /*callstack_id*/,
                         orbit_client_protos::CallstackInfo /*callstack*/) override {
    UNREACHABLE();
  }
  void OnCallstackEvent(orbit_client_protos::CallstackEvent /*callstack_event*/) override {
    UNREACHABLE();
  }
  void OnThreadName(uint32_t /*thread_id*/, std::string /*thread_name*/) override { UNREACHABLE(); }
  void OnThreadStateSlice(
      orbit_client_protos::ThreadStateSliceInfo /*thread_state_slice*/) override {
    UNREACHABLE();
  }
  void OnAddressInfo(orbit_client_protos::LinuxAddressInfo /*address_info*/) override {
    UNREACHABLE();
  }
  void OnUniqueTracepointInfo(uint64_t /*key*/,
                              orbit_grpc_protos::TracepointInfo /*tracepoint_info*/) override {
    UNREACHABLE();
  }
  void OnTracepointEvent(
      orbit_client_protos::TracepointEventInfo /*tracepoint_event_info*/) override {
    UNREACHABLE();
  }
  void OnModuleUpdate(uint64_t /*timestamp_ns*/,
                      orbit_grpc_protos::ModuleInfo /*module_info*/) override {
    UNREACHABLE();
  }
  void OnModulesSnapshot(uint64_t /*timestamp_ns*/,
                         std::vector<orbit_grpc_protos::ModuleInfo> /*module_infos*/) override {
    UNREACHABLE();
  }
  void OnWarningEvent(orbit_grpc_protos::WarningEvent /*warning_event*/) override { UNREACHABLE(); }
  void OnClockResolutionEvent(
      orbit_grpc_protos::ClockResolutionEvent /*clock_resolution_event*/) override {
    UNREACHABLE();
  }
  void OnErrorsWithPerfEventOpenEvent(
      orbit_grpc_protos::ErrorsWithPerfEventOpenEvent /*errors_with_perf_event_open_event*/)
      override {
    UNREACHABLE();
  }
  void OnErrorEnablingOrbitApiEvent(
      orbit_grpc_protos::ErrorEnablingOrbitApiEvent /*error_enabling_orbit_api_event*/) override {
    UNREACHABLE();
  }
  void OnErrorEnablingUserSpaceInstrumentationEvent(
      orbit_grpc_protos::ErrorEnablingUserSpaceInstrumentationEvent /*error_event*/) override {
    UNREACHABLE();
  }
  void OnWarningInstrumentingWithUserSpaceInstrumentationEvent(
      orbit_grpc_protos::WarningInstrumentingWithUserSpaceInstrumentationEvent /*warning_event*/)
      override {
    UNREACHABLE();
  }
  void OnLostPerfRecordsEvent(
      orbit_grpc_protos::LostPerfRecordsEvent /*lost_perf_records_event*/) override {
    UNREACHABLE();
  }
  void OnOutOfOrderEventsDiscardedEvent(orbit_grpc_protos::OutOfOrderEventsDiscardedEvent
                                        /*out_of_order_events_discarded_event*/) override {
    UNREACHABLE();
  }

  IntrospectionWindow* introspection_window_;
};

}  // namespace

IntrospectionWindow::IntrospectionWindow(OrbitApp* app)
    : CaptureWindow(app),
      capture_listener_{std::make_unique<IntrospectionCaptureListener>(this)},
      api_event_processor_{capture_listener_.get()} {
  // Create CaptureData.
  CaptureStarted capture_started;
  capture_started.set_process_id(orbit_base::GetCurrentProcessId());
  capture_started.set_executable_path("Orbit");
  absl::flat_hash_set<uint64_t> frame_track_function_ids;
  capture_data_ = std::make_unique<CaptureData>(/*module_manager=*/nullptr, capture_started,
                                                std::nullopt, std::move(frame_track_function_ids),
                                                CaptureData::DataSource::kLiveCapture);
}

IntrospectionWindow::~IntrospectionWindow() { StopIntrospection(); }

const char* IntrospectionWindow::GetHelpText() const {
  const char* help_message =
      "Client Side Introspection\n\n"
      "Start/Stop Capture: 'X'\n"
      "Toggle Help: 'H'";
  return help_message;
}

bool IntrospectionWindow::IsIntrospecting() const { return introspection_listener_ != nullptr; }

void IntrospectionWindow::StartIntrospection() {
  CHECK(!IsIntrospecting());
  set_draw_help(false);
  CreateTimeGraph(capture_data_.get());
  introspection_listener_ = std::make_unique<orbit_introspection::IntrospectionListener>(
      [this](const orbit_api::ApiEventVariant& api_event_variant) {
        std::visit(
            [this](const auto& api_event) { HandleCaptureEvent(api_event, &api_event_processor_); },
            api_event_variant);
      });
}
void IntrospectionWindow::StopIntrospection() { introspection_listener_ = nullptr; }

void IntrospectionWindow::Draw() {
  ORBIT_SCOPE_FUNCTION;
  CaptureWindow::Draw();
}

void IntrospectionWindow::DrawScreenSpace() {
  ORBIT_SCOPE_FUNCTION;
  CaptureWindow::DrawScreenSpace();
}

void IntrospectionWindow::RenderText(float layer) {
  ORBIT_SCOPE_FUNCTION;
  CaptureWindow::RenderText(layer);
}

void IntrospectionWindow::ToggleRecording() {
  if (!IsIntrospecting()) {
    StartIntrospection();
  } else {
    StopIntrospection();
  }
}

void IntrospectionWindow::RenderImGuiDebugUI() {
  CaptureWindow::RenderImGuiDebugUI();

  if (ImGui::CollapsingHeader("IntrospectionWindow")) {
    IMGUI_VAR_TO_TEXT(IsIntrospecting());
  }
}

void IntrospectionWindow::KeyPressed(unsigned int key_code, bool ctrl, bool shift, bool alt) {
  CaptureWindow::KeyPressed(key_code, ctrl, shift, alt);

  switch (key_code) {
    case 'H':
      set_draw_help(!get_draw_help());
      break;
  }
}

bool IntrospectionWindow::ShouldAutoZoom() const { return IsIntrospecting(); }
