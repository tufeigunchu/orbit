// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "TimerTrack.h"

#include <GteVector.h>
#include <absl/flags/declare.h>
#include <absl/synchronization/mutex.h>
#include <stddef.h>

#include <algorithm>
#include <limits>
#include <utility>

#include "ApiInterface/Orbit.h"
#include "App.h"
#include "Batcher.h"
#include "ClientFlags/ClientFlags.h"
#include "ClientProtos/capture_data.pb.h"
#include "DisplayFormats/DisplayFormats.h"
#include "GlCanvas.h"
#include "TimeGraphLayout.h"
#include "TriangleToggle.h"
#include "Viewport.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"

using orbit_client_data::TimerChain;
using orbit_client_data::TimerData;
using orbit_client_protos::TimerInfo;

const Color TimerTrack::kHighlightColor = Color(100, 181, 246, 255);

TimerTrack::TimerTrack(CaptureViewElement* parent,
                       const orbit_gl::TimelineInfoInterface* timeline_info,
                       orbit_gl::Viewport* viewport, TimeGraphLayout* layout, OrbitApp* app,
                       const orbit_client_data::CaptureData* capture_data, TimerData* timer_data)
    : Track(parent, timeline_info, viewport, layout, capture_data),
      app_{app},
      timer_data_{timer_data} {}

std::string TimerTrack::GetExtraInfo(const TimerInfo& timer_info) const {
  std::string info;
  static bool show_return_value = absl::GetFlag(FLAGS_show_return_values);
  if (show_return_value && timer_info.type() == TimerInfo::kNone) {
    info = absl::StrFormat("[%lu]", timer_info.user_data_key());
  }
  return info;
}

float TimerTrack::GetYFromTimer(const TimerInfo& timer_info) const {
  return GetYFromDepth(timer_info.depth());
}

float TimerTrack::GetYFromDepth(uint32_t depth) const {
  return GetPos()[1] + GetHeaderHeight() + GetDefaultBoxHeight() * static_cast<float>(depth);
}

namespace {
struct WorldXInfo {
  float world_x_start;
  float world_x_width;
};

WorldXInfo ToWorldX(double start_us, double end_us, double inv_time_window, float track_start_x,
                    float track_width) {
  double width_us = end_us - start_us;

  double normalized_start = start_us * inv_time_window;
  double normalized_width = width_us * inv_time_window;

  WorldXInfo result{};
  result.world_x_start = static_cast<float>(track_start_x + normalized_start * track_width);
  result.world_x_width = static_cast<float>(normalized_width * track_width);
  return result;
}

}  // namespace

std::string TimerTrack::GetDisplayTime(const TimerInfo& timer) const {
  return orbit_display_formats::GetDisplayTime(absl::Nanoseconds(timer.end() - timer.start()));
}

void TimerTrack::DrawTimesliceText(TextRenderer& text_renderer,
                                   const orbit_client_protos::TimerInfo& timer, float min_x,
                                   Vec2 box_pos, Vec2 box_size) {
  std::string timeslice_text = GetTimesliceText(timer);

  const std::string elapsed_time = GetDisplayTime(timer);
  const auto elapsed_time_length = elapsed_time.length();
  const Color kTextWhite(255, 255, 255, 255);
  float pos_x = std::max(box_pos[0], min_x);
  float max_size = box_pos[0] + box_size[0] - pos_x;

  TextRenderer::TextFormatting formatting{layout_->CalculateZoomedFontSize(), kTextWhite, max_size};
  formatting.valign = TextRenderer::VAlign::Bottom;

  text_renderer.AddTextTrailingCharsPrioritized(
      timeslice_text.c_str(), pos_x, box_pos[1] + box_size[1] - layout_->GetTextOffset(),
      GlCanvas::kZValueBox, formatting, elapsed_time_length);
}

bool TimerTrack::DrawTimer(TextRenderer& text_renderer, const TimerInfo* prev_timer_info,
                           const TimerInfo* next_timer_info, const internal::DrawData& draw_data,
                           const TimerInfo* current_timer_info, uint64_t* min_ignore,
                           uint64_t* max_ignore) {
  CHECK(min_ignore != nullptr);
  CHECK(max_ignore != nullptr);
  if (current_timer_info == nullptr) return false;
  if (draw_data.min_tick > current_timer_info->end() ||
      draw_data.max_tick < current_timer_info->start()) {
    return false;
  }
  if (current_timer_info->start() >= *min_ignore && current_timer_info->end() <= *max_ignore)
    return false;
  if (!TimerFilter(*current_timer_info)) return false;

  double start_us = timeline_info_->GetUsFromTick(current_timer_info->start());
  double start_or_prev_end_us = start_us;
  double end_us = timeline_info_->GetUsFromTick(current_timer_info->end());
  double end_or_next_start_us = end_us;

  float world_timer_y = GetYFromTimer(*current_timer_info);
  float box_height = GetDynamicBoxHeight(*current_timer_info);

  // Check if the previous timer overlaps with the current one, and if so draw the overlap
  // as triangles rather than as overlapping rectangles.
  if (prev_timer_info != nullptr) {
    // TODO(b/179985943): Turn this back into a check.
    if (prev_timer_info->start() < current_timer_info->start()) {
      // Note, that for timers that are completely inside the previous one, we will keep drawing
      // them above each other, as a proper solution would require us to keep a list of all
      // prev. intersecting timers. Further, we also compare the type, as for the Gpu timers,
      // timers of different type but same depth are drawn below each other (and thus do not
      // overlap).
      if (prev_timer_info->end() > current_timer_info->start() &&
          prev_timer_info->end() <= current_timer_info->end() &&
          prev_timer_info->type() == current_timer_info->type()) {
        start_or_prev_end_us = timeline_info_->GetUsFromTick(prev_timer_info->end());
      }
    }
  }

  // Check if the next timer overlaps with the current one, and if so draw the overlap
  // as triangles rather than as overlapping rectangles.
  if (next_timer_info != nullptr) {
    // TODO(b/179985943): Turn this back into a check.
    if (current_timer_info->start() < next_timer_info->start()) {
      // Note, that for timers that are completely inside the next one, we will keep drawing
      // them above each other, as a proper solution would require us to keep a list of all
      // upcoming intersecting timers. We also compare the type, as for the Gpu timers, timers
      // of different type but same depth are drawn below each other (and thus do not overlap).
      if (current_timer_info->end() > next_timer_info->start() &&
          current_timer_info->end() <= next_timer_info->end() &&
          next_timer_info->type() == current_timer_info->type()) {
        end_or_next_start_us = timeline_info_->GetUsFromTick(next_timer_info->start());
      }
    }
  }

  double elapsed_us = end_us - start_us;

  // Draw the Timer's text if it is not collapsed.
  if (!draw_data.is_collapsed) {
    // For overlaps, let us make the textbox just a bit wider:
    double left_overlap_width_us = start_or_prev_end_us - start_us;
    double text_x_start_us = start_or_prev_end_us - (.25 * left_overlap_width_us);
    double right_overlap_width_us = end_us - end_or_next_start_us;
    double text_x_end_us = end_or_next_start_us + (.25 * right_overlap_width_us);
    WorldXInfo world_x_info = ToWorldX(text_x_start_us, text_x_end_us, draw_data.inv_time_window,
                                       draw_data.track_start_x, draw_data.track_width);

    if (BoxHasRoomForText(text_renderer, world_x_info.world_x_width)) {
      Vec2 pos{world_x_info.world_x_start, world_timer_y};
      Vec2 size{world_x_info.world_x_width, GetDynamicBoxHeight(*current_timer_info)};

      DrawTimesliceText(text_renderer, *current_timer_info, draw_data.track_start_x, pos, size);
    }
  }

  uint64_t function_id = current_timer_info->function_id();
  uint64_t group_id = current_timer_info->group_id();

  bool is_selected = current_timer_info == draw_data.selected_timer;
  bool is_function_id_highlighted = function_id != orbit_grpc_protos::kInvalidFunctionId &&
                                    function_id == draw_data.highlighted_function_id;
  bool is_group_id_highlighted =
      group_id != kOrbitDefaultGroupId && group_id == draw_data.highlighted_group_id;
  bool is_highlighted = !is_selected && (is_function_id_highlighted || is_group_id_highlighted);

  Color color = GetTimerColor(*current_timer_info, is_selected, is_highlighted, draw_data);

  bool is_visible_width = elapsed_us * draw_data.inv_time_window *
                              viewport_->WorldToScreen({draw_data.track_width, 0})[0] >
                          1;

  if (is_visible_width) {
    WorldXInfo world_x_info_left_overlap =
        ToWorldX(start_us, start_or_prev_end_us, draw_data.inv_time_window, draw_data.track_start_x,
                 draw_data.track_width);

    WorldXInfo world_x_info_right_overlap =
        ToWorldX(end_or_next_start_us, end_us, draw_data.inv_time_window, draw_data.track_start_x,
                 draw_data.track_width);

    Vec3 top_left(world_x_info_left_overlap.world_x_start, world_timer_y, draw_data.z);
    Vec3 bottom_left(
        world_x_info_left_overlap.world_x_start + world_x_info_left_overlap.world_x_width,
        world_timer_y + box_height, draw_data.z);
    Vec3 top_right(world_x_info_right_overlap.world_x_start, world_timer_y, draw_data.z);
    Vec3 bottom_right(
        world_x_info_right_overlap.world_x_start + world_x_info_right_overlap.world_x_width,
        world_timer_y + box_height, draw_data.z);
    Batcher* batcher = draw_data.batcher;
    draw_data.batcher->AddShadedTrapezium(top_left, bottom_left, bottom_right, top_right, color,
                                          CreatePickingUserData(*batcher, *current_timer_info));
  } else {
    Batcher* batcher = draw_data.batcher;
    auto user_data = std::make_unique<PickingUserData>(
        current_timer_info,
        [&, batcher](PickingId id) { return this->GetBoxTooltip(*batcher, id); });

    WorldXInfo world_x_info = ToWorldX(start_us, end_us, draw_data.inv_time_window,
                                       draw_data.track_start_x, draw_data.track_width);

    Vec2 pos(world_x_info.world_x_start, world_timer_y);
    draw_data.batcher->AddVerticalLine(pos, GetDynamicBoxHeight(*current_timer_info), draw_data.z,
                                       color, std::move(user_data));
    // For lines, we can ignore the entire pixel into which this event
    // falls. We align this precisely on the pixel x-coordinate of the
    // current line being drawn (in ticks).
    int num_pixel = static_cast<int>((current_timer_info->start() - draw_data.min_timegraph_tick) /
                                     draw_data.ns_per_pixel);
    *min_ignore =
        draw_data.min_timegraph_tick + static_cast<uint64_t>(num_pixel * draw_data.ns_per_pixel);
    *max_ignore = draw_data.min_timegraph_tick +
                  static_cast<uint64_t>((num_pixel + 1) * draw_data.ns_per_pixel);
  }

  return true;
}

void TimerTrack::DoUpdatePrimitives(Batcher& batcher, TextRenderer& text_renderer,
                                    uint64_t min_tick, uint64_t max_tick,
                                    PickingMode picking_mode) {
  ORBIT_SCOPE_WITH_COLOR("TimerTrack::DoUpdatePrimitives", kOrbitColorOrange);
  Track::DoUpdatePrimitives(batcher, text_renderer, min_tick, max_tick, picking_mode);

  visible_timer_count_ = 0;

  internal::DrawData draw_data{};
  draw_data.min_tick = min_tick;
  draw_data.max_tick = max_tick;

  draw_data.batcher = &batcher;
  draw_data.viewport = viewport_;

  draw_data.track_start_x = GetPos()[0];
  draw_data.track_width = GetWidth();
  draw_data.inv_time_window = 1.0 / timeline_info_->GetTimeWindowUs();
  draw_data.is_collapsed = IsCollapsed();

  draw_data.z = GlCanvas::kZValueBox;

  std::vector<const orbit_client_data::TimerChain*> chains = timer_data_->GetChains();
  draw_data.selected_timer = app_->selected_timer();
  draw_data.highlighted_function_id = app_->GetFunctionIdToHighlight();
  draw_data.highlighted_group_id = app_->GetGroupIdToHighlight();

  // We minimize overdraw when drawing lines for small events by discarding
  // events that would just draw over an already drawn line. When zoomed in
  // enough that all events are drawn as boxes, this has no effect. When zoomed
  // out, many events will be discarded quickly.
  uint64_t time_window_ns = static_cast<uint64_t>(1000 * timeline_info_->GetTimeWindowUs());
  draw_data.ns_per_pixel =
      static_cast<double>(time_window_ns) / viewport_->WorldToScreen({GetWidth(), 0})[0];
  draw_data.min_timegraph_tick = timeline_info_->GetTickFromUs(timeline_info_->GetMinTimeUs());

  for (const TimerChain* chain : chains) {
    CHECK(chain != nullptr);
    // In order to draw overlaps correctly, we need for every text box to be drawn (current),
    // its previous and next text box. In order to avoid looking ahead for the next text (which is
    // error-prone), we are doing just one traversal of the text boxes, while keeping track of the
    // previous two timers, thus the currents iteration value being the "next" textbox.
    // Note: This will require us to draw the last timer after the traversal of the text boxes.
    // Also note: The draw method will take care of nullptr's being passed into (first iteration).
    const orbit_client_protos::TimerInfo* prev_timer_info = nullptr;
    const orbit_client_protos::TimerInfo* current_timer_info = nullptr;
    const orbit_client_protos::TimerInfo* next_timer_info = nullptr;

    // We have to reset this when we go to the next depth, as otherwise we
    // would miss drawing events that should be drawn.
    uint64_t min_ignore = std::numeric_limits<uint64_t>::max();
    uint64_t max_ignore = std::numeric_limits<uint64_t>::min();
    for (const orbit_client_data::TimerBlock& block : *chain) {
      if (!block.Intersects(min_tick, max_tick)) continue;

      for (size_t k = 0; k < block.size(); ++k) {
        // The current index (k) points to the "next" text box and we want to draw the text box
        // from the previous iteration ("current").
        next_timer_info = &block[k];

        if (DrawTimer(text_renderer, prev_timer_info, next_timer_info, draw_data,
                      current_timer_info, &min_ignore, &max_ignore)) {
          ++visible_timer_count_;
        }

        prev_timer_info = current_timer_info;
        current_timer_info = next_timer_info;
      }
    }

    // We still need to draw the last timer.
    next_timer_info = nullptr;
    if (DrawTimer(text_renderer, prev_timer_info, next_timer_info, draw_data, current_timer_info,
                  &min_ignore, &max_ignore)) {
      ++visible_timer_count_;
    }
  }
}

void TimerTrack::OnTimer(const TimerInfo& timer_info) {
  timer_data_->AddTimer(timer_info, timer_info.depth());
}

float TimerTrack::GetHeight() const {
  uint32_t collapsed_depth = std::min<uint32_t>(1, GetDepth());
  uint32_t depth = collapse_toggle_->IsCollapsed() ? collapsed_depth : GetDepth();
  return GetHeaderHeight() + layout_->GetTrackContentTopMargin() +
         layout_->GetTextBoxHeight() * depth +
         (depth > 0 ? layout_->GetSpaceBetweenTracksAndThread() : 0) +
         layout_->GetTrackContentBottomMargin();
}

std::string TimerTrack::GetTooltip() const {
  return "Shows collected samples and timings from dynamically instrumented "
         "functions";
}

const TimerInfo* TimerTrack::GetLeft(const TimerInfo& timer_info) const {
  return timer_data_->GetFirstBeforeStartTime(timer_info.start(), timer_info.depth());
}

const TimerInfo* TimerTrack::GetRight(const TimerInfo& timer_info) const {
  return timer_data_->GetFirstAfterStartTime(timer_info.start(), timer_info.depth());
}

const TimerInfo* TimerTrack::GetUp(const TimerInfo& timer_info) const {
  return timer_data_->GetFirstBeforeStartTime(timer_info.start(), timer_info.depth() - 1);
}

const TimerInfo* TimerTrack::GetDown(const TimerInfo& timer_info) const {
  return timer_data_->GetFirstAfterStartTime(timer_info.start(), timer_info.depth() + 1);
}

bool TimerTrack::IsEmpty() const { return timer_data_->IsEmpty(); }

std::string TimerTrack::GetBoxTooltip(const Batcher& /*batcher*/, PickingId /*id*/) const {
  return "";
}

float TimerTrack::GetHeaderHeight() const {
  return layout_->GetTrackTabHeight() + layout_->GetTrackContentTopMargin();
}

internal::DrawData TimerTrack::GetDrawData(uint64_t min_tick, uint64_t max_tick, float track_pos_x,
                                           float track_width, Batcher* batcher,
                                           const orbit_gl::TimelineInfoInterface* timeline_info,
                                           orbit_gl::Viewport* viewport, bool is_collapsed,
                                           const orbit_client_protos::TimerInfo* selected_timer,
                                           uint64_t highlighted_function_id,
                                           uint64_t highlighted_group_id) {
  internal::DrawData draw_data{};
  draw_data.min_tick = min_tick;
  draw_data.max_tick = max_tick;
  draw_data.batcher = batcher;
  draw_data.viewport = viewport;
  draw_data.track_start_x = track_pos_x;
  draw_data.track_width = track_width;
  draw_data.inv_time_window = 1.0 / timeline_info->GetTimeWindowUs();
  draw_data.is_collapsed = is_collapsed;
  draw_data.z = GlCanvas::kZValueBox;
  draw_data.selected_timer = selected_timer;
  draw_data.highlighted_function_id = highlighted_function_id;
  draw_data.highlighted_group_id = highlighted_group_id;

  uint64_t time_window_ns = static_cast<uint64_t>(1000 * timeline_info->GetTimeWindowUs());
  draw_data.ns_per_pixel =
      static_cast<double>(time_window_ns) / viewport->WorldToScreen({track_width, 0})[0];
  draw_data.min_timegraph_tick = timeline_info->GetTickFromUs(timeline_info->GetMinTimeUs());
  return draw_data;
}

size_t TimerTrack::GetNumberOfTimers() const { return timer_data_->GetNumberOfTimers(); }
uint64_t TimerTrack::GetMinTime() const { return timer_data_->GetMinTime(); }
uint64_t TimerTrack::GetMaxTime() const { return timer_data_->GetMaxTime(); }
