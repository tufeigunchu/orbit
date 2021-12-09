// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "BasicPageFaultsTrack.h"

#include "GlCanvas.h"

namespace orbit_gl {

namespace {

static std::array<std::string, kBasicPageFaultsTrackDimension> CreateSeriesName(
    const std::string& cgroup_name, const std::string& process_name) {
  return {absl::StrFormat("Process [%s]", process_name),
          absl::StrFormat("CGroup [%s]", cgroup_name), "System"};
}

}  // namespace

static constexpr uint8_t kTrackValueDecimalDigits = 0;
static constexpr const char* kTrackValueUnits = "";

BasicPageFaultsTrack::BasicPageFaultsTrack(Track* parent,
                                           const orbit_gl::TimelineInfoInterface* timeline_info,
                                           orbit_gl::Viewport* viewport, TimeGraphLayout* layout,
                                           std::string cgroup_name,
                                           uint64_t memory_sampling_period_ms,
                                           const orbit_client_data::CaptureData* capture_data)
    : LineGraphTrack<kBasicPageFaultsTrackDimension>(
          parent, timeline_info, viewport, layout,
          CreateSeriesName(cgroup_name, capture_data->process_name()), kTrackValueDecimalDigits,
          kTrackValueUnits, capture_data),
      AnnotationTrack(),
      cgroup_name_(std::move(cgroup_name)),
      memory_sampling_period_ms_(memory_sampling_period_ms),
      parent_(parent) {
  draw_background_ = false;
}

void BasicPageFaultsTrack::AddValues(
    uint64_t timestamp_ns, const std::array<double, kBasicPageFaultsTrackDimension>& values) {
  if (previous_time_and_values_.has_value()) {
    std::array<double, kBasicPageFaultsTrackDimension> differences;
    std::transform(values.begin(), values.end(), previous_time_and_values_.value().second.begin(),
                   differences.begin(), std::minus<double>());
    series_.AddValues(previous_time_and_values_.value().first, differences);
  }

  previous_time_and_values_ = std::make_pair(timestamp_ns, values);
}

void BasicPageFaultsTrack::AddValuesAndUpdateAnnotations(
    uint64_t timestamp_ns, const std::array<double, kBasicPageFaultsTrackDimension>& values) {
  AddValues(timestamp_ns, values);

  double updated_max = GetGraphMaxValue();
  std::optional<std::pair<std::string, double>> value_upper_bound = GetValueUpperBound();
  if (!value_upper_bound.has_value() || value_upper_bound.value().second < updated_max) {
    SetValueUpperBound(
        absl::StrFormat("Maximum Rate: %.0f per %d ms", updated_max, memory_sampling_period_ms_),
        updated_max);
  }

  double updated_min = GetGraphMinValue();
  std::optional<std::pair<std::string, double>> value_lower_bound = GetValueLowerBound();
  if (!value_lower_bound.has_value() || value_lower_bound.value().second > updated_min) {
    SetValueLowerBound(
        absl::StrFormat("Minimum Rate: %.0f per %d ms", updated_min, memory_sampling_period_ms_),
        updated_min);
  }
}

void BasicPageFaultsTrack::DoDraw(Batcher& batcher, TextRenderer& text_renderer,
                                  const DrawContext& draw_context) {
  LineGraphTrack<kBasicPageFaultsTrackDimension>::DoDraw(batcher, text_renderer, draw_context);

  if (draw_context.picking_mode != PickingMode::kNone || IsCollapsed()) return;
  AnnotationTrack::DrawAnnotation(batcher, text_renderer, layout_, indentation_level_,
                                  GlCanvas::kZValueTrackText);
}

void BasicPageFaultsTrack::DrawSingleSeriesEntry(
    Batcher& batcher, uint64_t start_tick, uint64_t end_tick,
    const std::array<float, kBasicPageFaultsTrackDimension>& current_normalized_values,
    const std::array<float, kBasicPageFaultsTrackDimension>& next_normalized_values, float z,
    bool is_last) {
  LineGraphTrack<kBasicPageFaultsTrackDimension>::DrawSingleSeriesEntry(
      batcher, start_tick, end_tick, current_normalized_values, next_normalized_values, z, is_last);

  if (!index_of_series_to_highlight_.has_value()) return;
  if (current_normalized_values[index_of_series_to_highlight_.value()] == 0) return;

  const Color kHightlightingColor(231, 68, 53, 100);
  float x0 = timeline_info_->GetWorldFromTick(start_tick);
  float width = timeline_info_->GetWorldFromTick(end_tick) - x0;
  float content_height = GetGraphContentHeight();
  float y0 = GetGraphContentBottomY() - GetGraphContentHeight();
  batcher.AddShadedBox(Vec2(x0, y0), Vec2(width, content_height), z, kHightlightingColor);
}

bool BasicPageFaultsTrack::IsCollapsed() const {
  return collapse_toggle_->IsCollapsed() || GetParent()->IsCollapsed();
}

float BasicPageFaultsTrack::GetAnnotatedTrackContentHeight() const {
  return GetGraphContentHeight();
}

}  // namespace orbit_gl