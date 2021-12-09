// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "TimeGraph.h"

#include <GteVector.h>
#include <absl/container/flat_hash_map.h>
#include <absl/flags/flag.h>
#include <absl/strings/str_format.h>
#include <absl/time/time.h>
#include <stddef.h>

#include <algorithm>
#include <limits>
#include <utility>

#include "App.h"
#include "AsyncTrack.h"
#include "CGroupAndProcessMemoryTrack.h"
#include "CaptureClient/CaptureEventProcessor.h"
#include "ClientData/FunctionUtils.h"
#include "ClientFlags/ClientFlags.h"
#include "DisplayFormats/DisplayFormats.h"
#include "FrameTrack.h"
#include "Geometry.h"
#include "GlCanvas.h"
#include "GlUtils.h"
#include "GpuTrack.h"
#include "GrpcProtos/Constants.h"
#include "Introspection/Introspection.h"
#include "ManualInstrumentationManager.h"
#include "OrbitBase/Append.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/ThreadConstants.h"
#include "PageFaultsTrack.h"
#include "PickingManager.h"
#include "SchedulerTrack.h"
#include "SystemMemoryTrack.h"
#include "ThreadTrack.h"
#include "TrackManager.h"
#include "VariableTrack.h"

using orbit_capture_client::CaptureEventProcessor;

using orbit_client_data::CaptureData;
using orbit_client_data::TimerChain;
using orbit_client_protos::ApiTrackValue;
using orbit_client_protos::CallstackEvent;
using orbit_client_protos::TimerInfo;

using orbit_gl::CGroupAndProcessMemoryTrack;
using orbit_gl::PageFaultsTrack;
using orbit_gl::SystemMemoryTrack;
using orbit_gl::VariableTrack;

using orbit_grpc_protos::InstrumentedFunction;

TimeGraph::TimeGraph(AccessibleInterfaceProvider* parent, OrbitApp* app,
                     orbit_gl::Viewport* viewport, CaptureData* capture_data,
                     PickingManager* picking_manager)
    // Note that `GlCanvas` and `TimeGraph` span the bridge to OpenGl content, and `TimeGraph`'s
    // parent needs special handling for accessibility. Thus, we use `nullptr` here and we save the
    // parent in accessible_parent_ which doesn't need to be a CaptureViewElement.
    : orbit_gl::CaptureViewElement(nullptr, viewport, &layout_),
      accessible_parent_{parent},
      batcher_(BatcherId::kTimeGraph),
      manual_instrumentation_manager_{app->GetManualInstrumentationManager()},
      capture_data_{capture_data},
      thread_track_data_provider_(capture_data->GetThreadTrackDataProvider()),
      app_{app} {
  text_renderer_static_.Init();
  text_renderer_static_.SetViewport(viewport);
  batcher_.SetPickingManager(picking_manager);
  track_manager_ = std::make_unique<TrackManager>(this, viewport_, &GetLayout(), app, capture_data);
  track_manager_->GetOrCreateSchedulerTrack();

  async_timer_info_listener_ =
      std::make_unique<ManualInstrumentationManager::AsyncTimerInfoListener>(
          [this](const std::string& name, const TimerInfo& timer_info) {
            ProcessAsyncTimer(name, timer_info);
          });
  manual_instrumentation_manager_->AddAsyncTimerListener(async_timer_info_listener_.get());

  if (absl::GetFlag(FLAGS_enforce_full_redraw)) {
    RequestUpdate();
  }
}

TimeGraph::~TimeGraph() {
  manual_instrumentation_manager_->RemoveAsyncTimerListener(async_timer_info_listener_.get());
}

float TimeGraph::GetHeight() const {
  // Top and Bottom Margin. TODO: Margins should be treated in a different way (http://b/192070555).
  float total_height = layout_.GetSchedulerTrackOffset() + layout_.GetBottomMargin();

  // Track height including space between them
  for (auto& track : GetNonHiddenChildren()) {
    total_height += (track->GetHeight() + layout_.GetSpaceBetweenTracks());
  }
  return total_height;
}

void TimeGraph::UpdateCaptureMinMaxTimestamps() {
  auto [tracks_min_time, tracks_max_time] = track_manager_->GetTracksMinMaxTimestamps();

  capture_min_timestamp_ = std::min(capture_min_timestamp_, tracks_min_time);
  capture_max_timestamp_ = std::max(capture_max_timestamp_, tracks_max_time);
}

void TimeGraph::ZoomAll() {
  constexpr double kNumHistorySeconds = 2.f;
  UpdateCaptureMinMaxTimestamps();
  max_time_us_ = TicksToMicroseconds(capture_min_timestamp_, capture_max_timestamp_);
  min_time_us_ = max_time_us_ - (kNumHistorySeconds * 1000 * 1000);
  if (min_time_us_ < 0) min_time_us_ = 0;

  RequestUpdate();
}

void TimeGraph::Zoom(uint64_t min, uint64_t max) {
  double start = TicksToMicroseconds(capture_min_timestamp_, min);
  double end = TicksToMicroseconds(capture_min_timestamp_, max);

  double mid = start + ((end - start) / 2.0);
  double extent = 1.1 * (end - start) / 2.0;

  SetMinMax(mid - extent, mid + extent);
}

void TimeGraph::Zoom(const TimerInfo& timer_info) { Zoom(timer_info.start(), timer_info.end()); }

double TimeGraph::GetCaptureTimeSpanUs() const {
  // Do we have an empty capture?
  if (capture_max_timestamp_ == 0 &&
      capture_min_timestamp_ == std::numeric_limits<uint64_t>::max()) {
    return 0.0;
  }
  CHECK(capture_min_timestamp_ <= capture_max_timestamp_);
  return TicksToMicroseconds(capture_min_timestamp_, capture_max_timestamp_);
}

double TimeGraph::GetCurrentTimeSpanUs() const { return max_time_us_ - min_time_us_; }

void TimeGraph::ZoomTime(float zoom_value, double mouse_ratio) {
  static double increment_ratio = 0.1;
  double scale = (zoom_value > 0) ? (1 + increment_ratio) : (1 / (1 + increment_ratio));
  // The horizontal zoom could have been triggered from the margin of TimeGraph, so we clamp the
  // mouse_ratio to ensure it is between 0 and 1.
  mouse_ratio = std::clamp(mouse_ratio, 0., 1.);

  double current_time_window_us = max_time_us_ - min_time_us_;
  ref_time_us_ = min_time_us_ + mouse_ratio * current_time_window_us;

  double time_left = std::max(ref_time_us_ - min_time_us_, 0.0);
  double time_right = std::max(max_time_us_ - ref_time_us_, 0.0);

  double min_time_us = ref_time_us_ - scale * time_left;
  double max_time_us = ref_time_us_ + scale * time_right;

  SetMinMax(min_time_us, max_time_us);
}

void TimeGraph::VerticalZoom(float zoom_value, float mouse_normalized_y_position) {
  constexpr float kIncrementRatio = 0.1f;

  const float ratio = (zoom_value > 0) ? (1 + kIncrementRatio) : (1 / (1 + kIncrementRatio));

  // We have to scale every item in the layout.
  const float old_scale = layout_.GetScale();
  layout_.SetScale(old_scale / ratio);

  // Adjust the scrolling offset such that the point under the mouse stays the same if possible.
  // For this, calculate the "global" position (including scaling and scrolling offset) of the point
  // underneath the mouse with the old and new scaling, and adjust the scrolling to have them match.
  const float offset_from_top_in_world = viewport_->GetWorldHeight() * mouse_normalized_y_position;
  const float mouse_y_including_scrolling =
      (offset_from_top_in_world + vertical_scrolling_offset_) / old_scale;
  const float new_scrolling_offset =
      mouse_y_including_scrolling * layout_.GetScale() - offset_from_top_in_world;
  SetVerticalScrollingOffset(new_scrolling_offset);
}

void TimeGraph::SetMinMax(double min_time_us, double max_time_us) {
  constexpr double kTimeGraphMinTimeWindowsUs = 0.1; /* 100 ns */
  const double desired_time_window =
      std::max(max_time_us - min_time_us, kTimeGraphMinTimeWindowsUs);

  // Centering the interval in screen.
  const double center_time_us = (max_time_us + min_time_us) / 2.;

  min_time_us_ = std::max(center_time_us - desired_time_window / 2, 0.0);
  max_time_us_ = std::min(min_time_us_ + desired_time_window, GetCaptureTimeSpanUs());

  RequestUpdate();
}

void TimeGraph::PanTime(int initial_x, int current_x, int width, double initial_time) {
  time_window_us_ = max_time_us_ - min_time_us_;
  double initial_local_time = static_cast<double>(initial_x) / width * time_window_us_;
  double dt = static_cast<double>(current_x - initial_x) / width * time_window_us_;
  double current_time = initial_time - dt;
  min_time_us_ =
      std::clamp(current_time - initial_local_time, 0.0, GetCaptureTimeSpanUs() - time_window_us_);
  max_time_us_ = min_time_us_ + time_window_us_;

  RequestUpdate();
}

void TimeGraph::HorizontallyMoveIntoView(VisibilityType vis_type, uint64_t min, uint64_t max,
                                         double distance) {
  if (IsVisible(vis_type, min, max)) {
    return;
  }

  double start = TicksToMicroseconds(capture_min_timestamp_, min);
  double end = TicksToMicroseconds(capture_min_timestamp_, max);

  double current_time_window_us = max_time_us_ - min_time_us_;

  if (vis_type == VisibilityType::kFullyVisible && current_time_window_us < (end - start)) {
    Zoom(min, max);
    return;
  }

  double mid = start + ((end - start) / 2.0);

  // Mirror the final center position if we have to move left
  if (start < min_time_us_) {
    distance = 1 - distance;
  }

  SetMinMax(mid - current_time_window_us * (1 - distance), mid + current_time_window_us * distance);
}

void TimeGraph::HorizontallyMoveIntoView(VisibilityType vis_type, const TimerInfo& timer_info,
                                         double distance) {
  HorizontallyMoveIntoView(vis_type, timer_info.start(), timer_info.end(), distance);
}

void TimeGraph::VerticallyMoveIntoView(const TimerInfo& timer_info) {
  VerticallyMoveIntoView(*track_manager_->GetOrCreateThreadTrack(timer_info.thread_id()));
}

// Move vertically the view to make a Track fully visible.
void TimeGraph::VerticallyMoveIntoView(Track& track) {
  float pos = track.GetPos()[1] + vertical_scrolling_offset_;
  float height = track.GetHeight();

  float max_vertical_scrolling_offset = pos;
  float min_vertical_scrolling_offset =
      pos + height - viewport_->GetWorldHeight() + layout_.GetBottomMargin();
  SetVerticalScrollingOffset(std::clamp(vertical_scrolling_offset_, min_vertical_scrolling_offset,
                                        max_vertical_scrolling_offset));
}

void TimeGraph::UpdateHorizontalScroll(float ratio) {
  double time_span = GetCaptureTimeSpanUs();
  double time_window = max_time_us_ - min_time_us_;
  min_time_us_ = ratio * (time_span - time_window);
  max_time_us_ = min_time_us_ + time_window;
}

double TimeGraph::GetTime(double ratio) const {
  double current_width = max_time_us_ - min_time_us_;
  double delta = ratio * current_width;
  return min_time_us_ + delta;
}

void TimeGraph::ProcessTimer(const TimerInfo& timer_info, const InstrumentedFunction* function) {
  capture_min_timestamp_ = std::min(capture_min_timestamp_, timer_info.start());
  capture_max_timestamp_ = std::max(capture_max_timestamp_, timer_info.end());

  // TODO(b/175869409): Change the way to create and get the tracks. Move this part to TrackManager.
  switch (timer_info.type()) {
    // All GPU timers are handled equally here.
    case TimerInfo::kGpuActivity:
    case TimerInfo::kGpuCommandBuffer:
    case TimerInfo::kGpuDebugMarker: {
      uint64_t timeline_hash = timer_info.timeline_hash();
      GpuTrack* track = track_manager_->GetOrCreateGpuTrack(timeline_hash);
      track->OnTimer(timer_info);
      break;
    }
    case TimerInfo::kFrame: {
      if (function == nullptr) {
        break;
      }
      FrameTrack* track = track_manager_->GetOrCreateFrameTrack(*function);
      track->OnTimer(timer_info);
      break;
    }
    case TimerInfo::kCoreActivity: {
      // TODO(b/176962090): We need to create the `ThreadTrack` here even we don't use it, as we
      //  don't create it on new callstack events, yet.
      track_manager_->GetOrCreateThreadTrack(timer_info.thread_id());
      SchedulerTrack* scheduler_track = track_manager_->GetOrCreateSchedulerTrack();
      scheduler_track->OnTimer(timer_info);
      break;
    }
    case TimerInfo::kSystemMemoryUsage: {
      ProcessSystemMemoryTrackingTimer(timer_info);
      break;
    }
    case TimerInfo::kCGroupAndProcessMemoryUsage: {
      ProcessCGroupAndProcessMemoryTrackingTimer(timer_info);
      break;
    }
    case TimerInfo::kPageFaults: {
      ProcessPageFaultsTrackingTimer(timer_info);
      break;
    }
    case TimerInfo::kNone: {
      // TODO (http://b/198135618): Create tracks only before drawing.
      track_manager_->GetOrCreateThreadTrack(timer_info.thread_id());
      thread_track_data_provider_->AddTimer(timer_info);
      break;
    }
    case TimerInfo::kApiScope: {
      // TODO (http://b/198135618): Create tracks only before drawing.
      track_manager_->GetOrCreateThreadTrack(timer_info.thread_id());
      thread_track_data_provider_->AddTimer(timer_info);
      break;
    }
    case TimerInfo::kApiScopeAsync: {
      manual_instrumentation_manager_->ProcessAsyncTimer(timer_info);
      break;
    }
    default:
      UNREACHABLE();
  }

  RequestUpdate();
}

void TimeGraph::ProcessApiStringEvent(const orbit_client_protos::ApiStringEvent& string_event) {
  manual_instrumentation_manager_->ProcessStringEvent(string_event);
}

void TimeGraph::ProcessApiTrackValueEvent(const orbit_client_protos::ApiTrackValue& track_event) {
  VariableTrack* track = track_manager_->GetOrCreateVariableTrack(track_event.name());

  uint64_t time = track_event.timestamp_ns();

  switch (track_event.data_case()) {
    case ApiTrackValue::kDataDouble:
      track->AddValue(time, track_event.data_double());
      break;
    case ApiTrackValue::kDataFloat:
      track->AddValue(time, track_event.data_float());
      break;
    case ApiTrackValue::kDataInt:
      track->AddValue(time, track_event.data_int());
      break;
    case ApiTrackValue::kDataInt64:
      track->AddValue(time, track_event.data_int64());
      break;
    case ApiTrackValue::kDataUint:
      track->AddValue(time, track_event.data_uint());
      break;
    case ApiTrackValue::kDataUint64:
      track->AddValue(time, track_event.data_uint64());
      break;
    default:
      UNREACHABLE();
  }
}

void TimeGraph::ProcessSystemMemoryTrackingTimer(const TimerInfo& timer_info) {
  SystemMemoryTrack* track = track_manager_->GetSystemMemoryTrack();
  if (track == nullptr) {
    track = track_manager_->CreateAndGetSystemMemoryTrack();
  }
  track->OnTimer(timer_info);

  if (absl::GetFlag(FLAGS_enable_warning_threshold) && !track->GetWarningThreshold().has_value()) {
    constexpr double kMegabytesToKilobytes = 1024.0;
    double warning_threshold_mb =
        static_cast<double>(app_->GetMemoryWarningThresholdKb()) / kMegabytesToKilobytes;
    track->SetWarningThreshold(warning_threshold_mb);
  }
}

void TimeGraph::ProcessCGroupAndProcessMemoryTrackingTimer(const TimerInfo& timer_info) {
  uint64_t cgroup_name_hash = timer_info.registers(static_cast<size_t>(
      CaptureEventProcessor::CGroupAndProcessMemoryUsageEncodingIndex::kCGroupNameHash));
  std::string cgroup_name = app_->GetStringManager()->Get(cgroup_name_hash).value_or("");
  if (cgroup_name.empty()) return;

  CGroupAndProcessMemoryTrack* track = track_manager_->GetCGroupAndProcessMemoryTrack();
  if (track == nullptr) {
    track = track_manager_->CreateAndGetCGroupAndProcessMemoryTrack(cgroup_name);
  }
  track->OnTimer(timer_info);
}

void TimeGraph::ProcessPageFaultsTrackingTimer(const TimerInfo& timer_info) {
  uint64_t cgroup_name_hash = timer_info.registers(
      static_cast<size_t>(CaptureEventProcessor::PageFaultsEncodingIndex::kCGroupNameHash));
  std::string cgroup_name = app_->GetStringManager()->Get(cgroup_name_hash).value_or("");
  if (cgroup_name.empty()) return;

  PageFaultsTrack* track = track_manager_->GetPageFaultsTrack();
  if (track == nullptr) {
    uint64_t memory_sampling_period_ms = app_->GetMemorySamplingPeriodMs();
    track = track_manager_->CreateAndGetPageFaultsTrack(cgroup_name, memory_sampling_period_ms);
  }
  CHECK(track != nullptr);
  track->OnTimer(timer_info);
}

void TimeGraph::ProcessAsyncTimer(const std::string& track_name, const TimerInfo& timer_info) {
  AsyncTrack* track = track_manager_->GetOrCreateAsyncTrack(track_name);
  track->OnTimer(timer_info);
}

std::vector<const TimerChain*> TimeGraph::GetAllThreadTrackTimerChains() const {
  return thread_track_data_provider_->GetAllThreadTimerChains();
}

int TimeGraph::GetNumVisiblePrimitives() const {
  int num_visible_primitives = 0;
  for (auto track : track_manager_->GetAllTracks()) {
    num_visible_primitives += track->GetVisiblePrimitiveCount();
  }
  return num_visible_primitives;
}

float TimeGraph::GetWorldFromTick(uint64_t time) const {
  if (time_window_us_ > 0) {
    double start = TicksToMicroseconds(capture_min_timestamp_, time) - min_time_us_;
    double normalized_start = start / time_window_us_;
    float pos = static_cast<float>(normalized_start * GetWidth());
    return pos;
  }

  return 0;
}

float TimeGraph::GetWorldFromUs(double micros) const {
  return GetWorldFromTick(GetTickFromUs(micros));
}

double TimeGraph::GetUsFromTick(uint64_t time) const {
  return TicksToMicroseconds(capture_min_timestamp_, time) - min_time_us_;
}

uint64_t TimeGraph::GetTickFromWorld(float world_x) const {
  double ratio = GetWidth() > 0 ? static_cast<double>(world_x / GetWidth()) : 0;
  auto time_span_ns = static_cast<uint64_t>(1000 * GetTime(ratio));
  return capture_min_timestamp_ + time_span_ns;
}

uint64_t TimeGraph::GetTickFromUs(double micros) const {
  auto nanos = static_cast<uint64_t>(1000 * micros);
  return capture_min_timestamp_ + nanos;
}

// Select a timer_info. Also move the view in order to assure that the timer_info and its track are
// visible.
void TimeGraph::SelectAndMakeVisible(const TimerInfo* timer_info) {
  CHECK(timer_info != nullptr);
  app_->SelectTimer(timer_info);
  HorizontallyMoveIntoView(VisibilityType::kPartlyVisible, *timer_info);
  VerticallyMoveIntoView(*timer_info);
}

const TimerInfo* TimeGraph::FindPreviousFunctionCall(uint64_t function_address,
                                                     uint64_t current_time,
                                                     std::optional<uint32_t> thread_id) const {
  const TimerInfo* previous_timer = nullptr;
  uint64_t goal_time = std::numeric_limits<uint64_t>::lowest();
  std::vector<const TimerChain*> chains = GetAllThreadTrackTimerChains();
  for (const TimerChain* chain : chains) {
    for (const auto& block : *chain) {
      if (!block.Intersects(goal_time, current_time)) continue;
      for (uint64_t i = 0; i < block.size(); i++) {
        const TimerInfo& timer_info = block[i];
        auto timer_end_time = timer_info.end();
        if ((timer_info.function_id() == function_address) &&
            (!thread_id || thread_id.value() == timer_info.thread_id()) &&
            (timer_end_time < current_time) && (goal_time < timer_end_time)) {
          previous_timer = &timer_info;
          goal_time = timer_end_time;
        }
      }
    }
  }
  return previous_timer;
}

const TimerInfo* TimeGraph::FindNextFunctionCall(uint64_t function_address, uint64_t current_time,
                                                 std::optional<uint32_t> thread_id) const {
  const TimerInfo* next_timer = nullptr;
  uint64_t goal_time = std::numeric_limits<uint64_t>::max();
  std::vector<const TimerChain*> chains = GetAllThreadTrackTimerChains();
  for (const TimerChain* chain : chains) {
    CHECK(chain != nullptr);
    for (const auto& block : *chain) {
      if (!block.Intersects(current_time, goal_time)) continue;
      for (uint64_t i = 0; i < block.size(); i++) {
        const TimerInfo& timer_info = block[i];
        auto timer_end_time = timer_info.end();
        if ((timer_info.function_id() == function_address) &&
            (!thread_id || thread_id.value() == timer_info.thread_id()) &&
            (timer_end_time > current_time) && (goal_time > timer_end_time)) {
          next_timer = &timer_info;
          goal_time = timer_end_time;
        }
      }
    }
  }
  return next_timer;
}

std::vector<const TimerInfo*> TimeGraph::GetAllTimersForHookedFunction(
    uint64_t function_address) const {
  std::vector<const TimerInfo*> timers;
  std::vector<const TimerChain*> chains = GetAllThreadTrackTimerChains();
  for (const TimerChain* chain : chains) {
    CHECK(chain != nullptr);
    for (const auto& block : *chain) {
      for (uint64_t i = 0; i < block.size(); i++) {
        const TimerInfo& timer = block[i];
        if (timer.function_id() == function_address) timers.push_back(&timer);
      }
    }
  }
  return timers;
}

void TimeGraph::RequestUpdate() {
  CaptureViewElement::RequestUpdate();
  update_primitives_requested_ = true;
}

void TimeGraph::PrepareBatcherAndUpdatePrimitives(PickingMode picking_mode) {
  ORBIT_SCOPE_FUNCTION;
  CHECK(app_->GetStringManager() != nullptr);

  batcher_.StartNewFrame();

  text_renderer_static_.Clear();

  uint64_t min_tick = GetTickFromUs(min_time_us_);
  uint64_t max_tick = GetTickFromUs(max_time_us_);

  CaptureViewElement::UpdatePrimitives(batcher_, text_renderer_static_, min_tick, max_tick,
                                       picking_mode);

  if (!absl::GetFlag(FLAGS_enforce_full_redraw)) {
    update_primitives_requested_ = false;
  }
}

void TimeGraph::DoUpdateLayout() {
  CaptureViewElement::DoUpdateLayout();

  capture_min_timestamp_ =
      std::min(capture_min_timestamp_, capture_data_->GetCallstackData().min_time());
  capture_max_timestamp_ =
      std::max(capture_max_timestamp_, capture_data_->GetCallstackData().max_time());

  time_window_us_ = max_time_us_ - min_time_us_;

  track_manager_->UpdateTrackListForRendering();
  UpdateTracksPosition();

  // This is called to make sure the current scrolling value is correctly clamped
  // in case any changes in track visibility occured before
  SetVerticalScrollingOffset(vertical_scrolling_offset_);
}

void TimeGraph::UpdateTracksPosition() {
  const float track_pos_x = GetPos()[0];

  float current_y = layout_.GetSchedulerTrackOffset() - vertical_scrolling_offset_;

  // Track height including space between them
  for (auto& track : track_manager_->GetVisibleTracks()) {
    if (!track->IsMoving()) {
      track->SetPos(track_pos_x, current_y);
    }
    track->SetWidth(GetWidth());
    current_y += (track->GetHeight() + layout_.GetSpaceBetweenTracks());
  }
}

namespace {

[[nodiscard]] std::string GetLabelBetweenIterators(const InstrumentedFunction& function_a,
                                                   const InstrumentedFunction& function_b) {
  const std::string& function_from = function_a.function_name();
  const std::string& function_to = function_b.function_name();
  return absl::StrFormat("%s to %s", function_from, function_to);
}

std::string GetTimeString(const TimerInfo& timer_a, const TimerInfo& timer_b) {
  absl::Duration duration = TicksToDuration(timer_a.start(), timer_b.start());

  return orbit_display_formats::GetDisplayTime(duration);
}

[[nodiscard]] Color GetIteratorBoxColor(uint64_t index) {
  constexpr uint64_t kNumColors = 2;
  const Color kLightBlueGray = Color(177, 203, 250, 60);
  const Color kMidBlueGray = Color(81, 102, 157, 60);
  Color colors[kNumColors] = {kLightBlueGray, kMidBlueGray};
  return colors[index % kNumColors];
}

}  // namespace

void TimeGraph::DrawIteratorBox(Batcher& batcher, TextRenderer& text_renderer, Vec2 pos, Vec2 size,
                                const Color& color, const std::string& label,
                                const std::string& time, float text_box_y) {
  Box box(pos, size, GlCanvas::kZValueOverlay);
  batcher.AddBox(box, color);

  std::string text = absl::StrFormat("%s: %s", label, time);

  float max_size = size[0];

  const Color kBlack(0, 0, 0, 255);
  float text_width = text_renderer.AddTextTrailingCharsPrioritized(
      text.c_str(), pos[0], text_box_y + layout_.GetTextOffset(), GlCanvas::kZValueTextUi,
      {GetLayout().GetFontSize(), kBlack, max_size}, time.length());

  Vec2 white_box_size(std::min(static_cast<float>(text_width), max_size), GetTextBoxHeight());
  Vec2 white_box_position(pos[0], text_box_y);

  Box white_box(white_box_position, white_box_size, GlCanvas::kZValueOverlayTextBackground);

  const Color kWhite(255, 255, 255, 255);
  batcher.AddBox(white_box, kWhite);

  Vec2 line_from(pos[0] + white_box_size[0], white_box_position[1] + GetTextBoxHeight() / 2.f);
  Vec2 line_to(pos[0] + size[0], white_box_position[1] + GetTextBoxHeight() / 2.f);
  batcher.AddLine(line_from, line_to, GlCanvas::kZValueOverlay, Color(255, 255, 255, 255));
}

void TimeGraph::DrawOverlay(Batcher& batcher, TextRenderer& text_renderer,
                            PickingMode picking_mode) {
  if (picking_mode != PickingMode::kNone || iterator_timer_info_.empty()) {
    return;
  }

  std::vector<std::pair<uint64_t, const TimerInfo*>> timers(iterator_timer_info_.size());
  std::copy(iterator_timer_info_.begin(), iterator_timer_info_.end(), timers.begin());

  // Sort timers by start time.
  std::sort(timers.begin(), timers.end(),
            [](const std::pair<uint64_t, const TimerInfo*>& timer_a,
               const std::pair<uint64_t, const TimerInfo*>& timer_b) -> bool {
              return timer_a.second->start() < timer_b.second->start();
            });

  // We will need the world x coordinates for the timers multiple times, so
  // we avoid recomputing them and just cache them here.
  std::vector<float> x_coords;
  x_coords.reserve(timers.size());

  float world_start_x = 0;
  float world_width = GetWidth();

  float world_start_y = 0;
  float world_height = viewport_->GetWorldHeight();

  double inv_time_window = 1.0 / GetTimeWindowUs();

  // Draw lines for iterators.
  for (const auto& box : timers) {
    const TimerInfo* timer_info = box.second;

    double start_us = GetUsFromTick(timer_info->start());
    double normalized_start = start_us * inv_time_window;
    auto world_timer_x = static_cast<float>(world_start_x + normalized_start * world_width);

    Vec2 pos(world_timer_x, world_start_y);
    x_coords.push_back(pos[0]);

    batcher.AddVerticalLine(pos, world_height, GlCanvas::kZValueOverlay,
                            GetThreadColor(timer_info->thread_id()));
  }

  // Draw timers with timings between iterators.
  for (size_t k = 1; k < timers.size(); ++k) {
    Vec2 pos(x_coords[k - 1], world_start_y);
    float size_x = x_coords[k] - pos[0];
    Vec2 size(size_x, world_height);
    Color color = GetIteratorBoxColor(k - 1);

    uint64_t id_a = timers[k - 1].first;
    uint64_t id_b = timers[k].first;
    CHECK(iterator_id_to_function_id_.find(id_a) != iterator_id_to_function_id_.end());
    CHECK(iterator_id_to_function_id_.find(id_b) != iterator_id_to_function_id_.end());
    uint64_t function_a_id = iterator_id_to_function_id_.at(id_a);
    uint64_t function_b_id = iterator_id_to_function_id_.at(id_b);
    const CaptureData& capture_data = app_->GetCaptureData();
    const InstrumentedFunction* function_a =
        capture_data.GetInstrumentedFunctionById(function_a_id);
    const InstrumentedFunction* function_b =
        capture_data.GetInstrumentedFunctionById(function_b_id);
    CHECK(function_a != nullptr);
    CHECK(function_b != nullptr);
    const std::string& label = GetLabelBetweenIterators(*function_a, *function_b);
    const std::string& time = GetTimeString(*timers[k - 1].second, *timers[k].second);

    // Distance from the bottom where we don't want to draw.
    float bottom_margin = layout_.GetBottomMargin();

    // The height of text is chosen such that the text of the last box drawn is
    // at pos[1] + bottom_margin (lowest possible position) and the height of
    // the box showing the overall time (see below) is at pos[1] + (world_height
    // / 2.f), corresponding to the case k == 0 in the formula for 'text_y'.
    float height_per_text = ((world_height / 2.f) - bottom_margin) /
                            static_cast<float>(iterator_timer_info_.size() - 1);
    float text_y = pos[1] + (world_height / 2.f) + static_cast<float>(k) * height_per_text -
                   layout_.GetTextBoxHeight();

    DrawIteratorBox(batcher, text_renderer, pos, size, color, label, time, text_y);
  }

  // When we have at least 3 boxes, we also draw the total time from the first
  // to the last iterator.
  if (timers.size() > 2) {
    size_t last_index = timers.size() - 1;

    Vec2 pos(x_coords[0], world_start_y);
    float size_x = x_coords[last_index] - pos[0];
    Vec2 size(size_x, world_height);

    std::string time = GetTimeString(*timers[0].second, *timers[last_index].second);
    std::string label("Total");

    float text_y = pos[1] + (world_height / 2.f);

    // We do not want the overall box to add any color, so we just set alpha to
    // 0.
    const Color kColorBlackTransparent(0, 0, 0, 0);
    DrawIteratorBox(batcher, text_renderer, pos, size, kColorBlackTransparent, label, time, text_y);
  }
}

void TimeGraph::DrawIncompleteDataIntervals(Batcher& batcher, PickingMode picking_mode) {
  if (picking_mode == PickingMode::kClick) return;  // Allow to click through.

  auto min_visible_timestamp_ns =
      capture_min_timestamp_ + static_cast<uint64_t>(GetMinTimeUs() * 1000L);
  auto max_visible_timestamp_ns =
      capture_min_timestamp_ + static_cast<uint64_t>(GetMaxTimeUs() * 1000L);

  std::vector<std::pair<float, float>> x_ranges;
  for (auto it = capture_data_->incomplete_data_intervals().LowerBound(min_visible_timestamp_ns);
       it != capture_data_->incomplete_data_intervals().end() &&
       it->start_inclusive() <= max_visible_timestamp_ns;
       ++it) {
    uint64_t start_timestamp_ns = it->start_inclusive();
    uint64_t end_timestamp_ns = it->end_exclusive();

    float start_x = GetWorldFromTick(start_timestamp_ns);
    float end_x = GetWorldFromTick(end_timestamp_ns);
    float width = end_x - start_x;
    constexpr float kMinWidth = 9.0f;
    // These intervals are very short, usually measurable in microseconds, but can have relatively
    // large effects on the capture. Extend ranges in order to make them visible even when not
    // zoomed very far in.
    if (width < kMinWidth) {
      const float center_x = (start_x + end_x) / 2;
      start_x = center_x - kMinWidth / 2;
      end_x = center_x + kMinWidth / 2;
      width = end_x - start_x;
    }

    // Merge ranges that are now overlapping due to having been extended for visibility.
    if (x_ranges.empty() || start_x > x_ranges.back().second) {
      x_ranges.emplace_back(start_x, end_x);
    } else {
      x_ranges.back().second = end_x;
    }
  }

  const float world_start_y = 0;
  const float world_height = viewport_->GetWorldHeight();

  // Actually draw the ranges.
  for (const auto& [start_x, end_x] : x_ranges) {
    const Vec2 pos{start_x, world_start_y};
    const Vec2 size{end_x - start_x, world_height};
    float z_value = GlCanvas::kZValueIncompleteDataOverlay;

    std::unique_ptr<PickingUserData> user_data = nullptr;
    // Show a tooltip when hovering.
    if (picking_mode == PickingMode::kHover) {
      // This overlay is placed in front of the tracks (with transparency), but when it comes to
      // tooltips give it a much lower Z value, so that it's possible to "hover through" it.
      z_value = GlCanvas::kZValueIncompleteDataOverlayPicking;
      user_data = std::make_unique<PickingUserData>(nullptr, [](PickingId /*id*/) {
        return std::string{
            "Capture data is incomplete in this time range. Some information might be inaccurate."};
      });
    }

    static const Color kIncompleteDataIntervalOrange{255, 128, 0, 32};
    batcher.AddBox(Box{pos, size, z_value}, kIncompleteDataIntervalOrange, std::move(user_data));
  }
}

void TimeGraph::SetThreadFilter(const std::string& filter) {
  track_manager_->SetFilter(filter);
  RequestUpdate();
}

void TimeGraph::SelectAndZoom(const TimerInfo* timer_info) {
  CHECK(timer_info);
  Zoom(*timer_info);
  SelectAndMakeVisible(timer_info);
}

void TimeGraph::JumpToNeighborTimer(const TimerInfo* from, JumpDirection jump_direction,
                                    JumpScope jump_scope) {
  if (from == nullptr || !TrackManager::IteratableType(from->type())) {
    return;
  }
  if ((jump_scope == JumpScope::kSameFunction ||
       jump_scope == JumpScope::kSameThreadSameFunction) &&
      !TrackManager::FunctionIteratableType(from->type())) {
    jump_scope = JumpScope::kSameDepth;
  }
  const TimerInfo* goal = nullptr;
  auto function_id = from->function_id();
  auto current_time = from->end();
  auto thread_id = from->thread_id();
  if (jump_direction == JumpDirection::kPrevious) {
    switch (jump_scope) {
      case JumpScope::kSameDepth:
        goal = FindPrevious(*from);
        break;
      case JumpScope::kSameFunction:
        goal = FindPreviousFunctionCall(function_id, current_time);
        break;
      case JumpScope::kSameThreadSameFunction:
        goal = FindPreviousFunctionCall(function_id, current_time, thread_id);
        break;
      default:
        // Other choices are not implemented.
        UNREACHABLE();
    }
  }
  if (jump_direction == JumpDirection::kNext) {
    switch (jump_scope) {
      case JumpScope::kSameDepth:
        goal = FindNext(*from);
        break;
      case JumpScope::kSameFunction:
        goal = FindNextFunctionCall(function_id, current_time);
        break;
      case JumpScope::kSameThreadSameFunction:
        goal = FindNextFunctionCall(function_id, current_time, thread_id);
        break;
      default:
        UNREACHABLE();
    }
  }
  if (jump_direction == JumpDirection::kTop) {
    goal = FindTop(*from);
  }
  if (jump_direction == JumpDirection::kDown) {
    goal = FindDown(*from);
  }
  if (goal != nullptr) {
    SelectAndMakeVisible(goal);
  }
}

const TimerInfo* TimeGraph::FindPrevious(const TimerInfo& from) {
  Track* track = track_manager_->GetOrCreateTrackFromTimerInfo(from);
  if (track == nullptr) return nullptr;
  return track->GetLeft(from);
}

const TimerInfo* TimeGraph::FindNext(const TimerInfo& from) {
  Track* track = track_manager_->GetOrCreateTrackFromTimerInfo(from);
  if (track == nullptr) return nullptr;
  return track->GetRight(from);
}

const TimerInfo* TimeGraph::FindTop(const TimerInfo& from) {
  Track* track = track_manager_->GetOrCreateTrackFromTimerInfo(from);
  if (track == nullptr) return nullptr;
  return track->GetUp(from);
}

const TimerInfo* TimeGraph::FindDown(const TimerInfo& from) {
  Track* track = track_manager_->GetOrCreateTrackFromTimerInfo(from);
  if (track == nullptr) return nullptr;
  return track->GetDown(from);
}

std::pair<const TimerInfo*, const TimerInfo*> TimeGraph::GetMinMaxTimerInfoForFunction(
    uint64_t function_id) const {
  const TimerInfo* min_timer = nullptr;
  const TimerInfo* max_timer = nullptr;
  std::vector<const TimerChain*> chains = GetAllThreadTrackTimerChains();
  for (const TimerChain* chain : chains) {
    for (const auto& block : *chain) {
      for (size_t i = 0; i < block.size(); i++) {
        const TimerInfo& timer_info = block[i];
        if (timer_info.function_id() != function_id) continue;

        uint64_t elapsed_nanos = timer_info.end() - timer_info.start();
        if (min_timer == nullptr || elapsed_nanos < (min_timer->end() - min_timer->start())) {
          min_timer = &timer_info;
        }
        if (max_timer == nullptr || elapsed_nanos > (max_timer->end() - max_timer->start())) {
          max_timer = &timer_info;
        }
      }
    }
  }
  return std::make_pair(min_timer, max_timer);
}

void TimeGraph::DoDraw(Batcher& batcher, TextRenderer& text_renderer,
                       const DrawContext& draw_context) {
  CaptureViewElement::DoDraw(batcher, text_renderer, draw_context);

  DrawIncompleteDataIntervals(batcher, draw_context.picking_mode);
  DrawOverlay(batcher, text_renderer, draw_context.picking_mode);
}

void TimeGraph::DrawAllElements(Batcher& batcher, TextRenderer& text_renderer,
                                PickingMode& picking_mode, uint64_t current_mouse_time_ns) {
  const bool picking = picking_mode != PickingMode::kNone;

  DrawContext context{current_mouse_time_ns, picking_mode};
  Draw(batcher, text_renderer, context);

  if ((!picking && update_primitives_requested_) || picking) {
    PrepareBatcherAndUpdatePrimitives(picking_mode);
  }
}

void TimeGraph::DrawText(float layer) { text_renderer_static_.RenderLayer(layer); }

bool TimeGraph::IsFullyVisible(uint64_t min, uint64_t max) const {
  double start = TicksToMicroseconds(capture_min_timestamp_, min);
  double end = TicksToMicroseconds(capture_min_timestamp_, max);

  return start > min_time_us_ && end < max_time_us_;
}

bool TimeGraph::IsPartlyVisible(uint64_t min, uint64_t max) const {
  double start = TicksToMicroseconds(capture_min_timestamp_, min);
  double end = TicksToMicroseconds(capture_min_timestamp_, max);

  return !(min_time_us_ > end || max_time_us_ < start);
}

bool TimeGraph::IsVisible(VisibilityType vis_type, uint64_t min, uint64_t max) const {
  switch (vis_type) {
    case VisibilityType::kPartlyVisible:
      return IsPartlyVisible(min, max);
    case VisibilityType::kFullyVisible:
      return IsFullyVisible(min, max);
    default:
      return false;
  }
}

void TimeGraph::SetVerticalScrollingOffset(float value) {
  float clamped_value = std::max(std::min(value, GetHeight() - viewport_->GetWorldHeight()), 0.f);
  if (clamped_value == vertical_scrolling_offset_) return;

  vertical_scrolling_offset_ = clamped_value;
  RequestUpdate();
}

bool TimeGraph::HasFrameTrack(uint64_t function_id) const {
  auto frame_tracks = track_manager_->GetFrameTracks();
  return (std::find_if(frame_tracks.begin(), frame_tracks.end(), [&](auto frame_track) {
            return frame_track->GetFunctionId() == function_id;
          }) != frame_tracks.end());
}

void TimeGraph::RemoveFrameTrack(uint64_t function_id) {
  track_manager_->RemoveFrameTrack(function_id);
  RequestUpdate();
}

std::vector<orbit_gl::CaptureViewElement*> TimeGraph::GetAllChildren() const {
  std::vector<Track*> all_tracks = track_manager_->GetAllTracks();
  return {all_tracks.begin(), all_tracks.end()};
}

std::vector<orbit_gl::CaptureViewElement*> TimeGraph::GetNonHiddenChildren() const {
  std::vector<Track*> all_tracks = track_manager_->GetVisibleTracks();
  return {all_tracks.begin(), all_tracks.end()};
}

std::unique_ptr<orbit_accessibility::AccessibleInterface> TimeGraph::CreateAccessibleInterface() {
  return std::make_unique<orbit_gl::TimeGraphAccessibility>(this);
}
