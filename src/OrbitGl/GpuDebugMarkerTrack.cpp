// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "GpuDebugMarkerTrack.h"

#include <absl/time/time.h>

#include <algorithm>
#include <memory>

#include "App.h"
#include "Batcher.h"
#include "ClientData/TimerChain.h"
#include "ClientProtos/capture_data.pb.h"
#include "DisplayFormats/DisplayFormats.h"
#include "GlUtils.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/ThreadConstants.h"
#include "TimeGraphLayout.h"
#include "TriangleToggle.h"
#include "absl/strings/str_format.h"

using orbit_client_protos::TimerInfo;

GpuDebugMarkerTrack::GpuDebugMarkerTrack(CaptureViewElement* parent,
                                         const orbit_gl::TimelineInfoInterface* timeline_info,
                                         orbit_gl::Viewport* viewport, TimeGraphLayout* layout,
                                         uint64_t timeline_hash, OrbitApp* app,
                                         const orbit_client_data::CaptureData* capture_data,
                                         orbit_client_data::TimerData* timer_data)
    : TimerTrack(parent, timeline_info, viewport, layout, app, capture_data, timer_data),
      string_manager_{app->GetStringManager()},
      timeline_hash_{timeline_hash} {
  draw_background_ = false;
}

std::string GpuDebugMarkerTrack::GetName() const {
  return absl::StrFormat(
      "%s_marker", string_manager_->Get(timeline_hash_).value_or(std::to_string(timeline_hash_)));
}

std::string GpuDebugMarkerTrack::GetTooltip() const {
  return "Shows execution times for Vulkan debug markers";
}

Color GpuDebugMarkerTrack::GetTimerColor(const TimerInfo& timer_info, bool is_selected,
                                         bool is_highlighted,
                                         const internal::DrawData& /*draw_data*/) const {
  CHECK(timer_info.type() == TimerInfo::kGpuDebugMarker);
  const Color kInactiveColor(100, 100, 100, 255);
  const Color kSelectionColor(0, 128, 255, 255);
  if (is_highlighted) {
    return TimerTrack::kHighlightColor;
  }
  if (is_selected) {
    return kSelectionColor;
  }
  if (!IsTimerActive(timer_info)) {
    return kInactiveColor;
  }
  if (timer_info.has_color()) {
    CHECK(timer_info.color().red() < 256);
    CHECK(timer_info.color().green() < 256);
    CHECK(timer_info.color().blue() < 256);
    CHECK(timer_info.color().alpha() < 256);
    return Color(static_cast<uint8_t>(timer_info.color().red()),
                 static_cast<uint8_t>(timer_info.color().green()),
                 static_cast<uint8_t>(timer_info.color().blue()),
                 static_cast<uint8_t>(timer_info.color().alpha()));
  }
  std::string marker_text = string_manager_->Get(timer_info.user_data_key()).value_or("");
  return TimeGraph::GetColor(marker_text);
}

std::string GpuDebugMarkerTrack::GetTimesliceText(const TimerInfo& timer_info) const {
  CHECK(timer_info.type() == TimerInfo::kGpuDebugMarker);

  std::string time = GetDisplayTime(timer_info);
  return absl::StrFormat("%s  %s", string_manager_->Get(timer_info.user_data_key()).value_or(""),
                         time);
}

std::string GpuDebugMarkerTrack::GetBoxTooltip(const Batcher& batcher, PickingId id) const {
  const TimerInfo* timer_info = batcher.GetTimerInfo(id);
  if (timer_info == nullptr) {
    return "";
  }

  CHECK(timer_info->type() == TimerInfo::kGpuDebugMarker);

  std::string marker_text = string_manager_->Get(timer_info->user_data_key()).value_or("");
  return absl::StrFormat(
      "<b>Vulkan Debug Marker</b><br/>"
      "<i>At the marker's begin and end `vkCmdWriteTimestamp`s have been "
      "inserted. The GPU timestamps get aligned with the corresponding hardware execution of the "
      "submission.</i>"
      "<br/>"
      "<br/>"
      "<b>Marker text:</b> %s<br/>"
      "<b>Submitted from process:</b> %s [%d]<br/>"
      "<b>Submitted from thread:</b> %s [%d]<br/>"
      "<b>Time:</b> %s",
      marker_text, capture_data_->GetThreadName(timer_info->process_id()), timer_info->process_id(),
      capture_data_->GetThreadName(timer_info->thread_id()), timer_info->thread_id(),
      orbit_display_formats::GetDisplayTime(TicksToDuration(timer_info->start(), timer_info->end()))
          .c_str());
}

float GpuDebugMarkerTrack::GetYFromTimer(const TimerInfo& timer_info) const {
  uint32_t depth = timer_info.depth();
  if (collapse_toggle_->IsCollapsed()) {
    depth = 0;
  }
  return GetPos()[1] + layout_->GetTrackTabHeight() + layout_->GetTextBoxHeight() * depth;
}

float GpuDebugMarkerTrack::GetHeight() const {
  bool collapsed = collapse_toggle_->IsCollapsed();
  uint32_t depth = collapsed ? std::min<uint32_t>(1, GetDepth()) : GetDepth();
  return layout_->GetTrackTabHeight() + layout_->GetTrackContentTopMargin() +
         layout_->GetTextBoxHeight() * depth + layout_->GetTrackContentBottomMargin();
}

bool GpuDebugMarkerTrack::TimerFilter(const TimerInfo& timer_info) const {
  if (collapse_toggle_->IsCollapsed()) {
    return timer_info.depth() == 0;
  }
  return true;
}
