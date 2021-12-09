// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ClientData/CallstackTypes.h"
#include "ClientData/PostProcessedSamplingData.h"
#include "ClientModel/SamplingDataPostProcessor.h"
#include "ClientProtos/capture_data.pb.h"
#include "DataViews/AppInterface.h"
#include "DataViews/CallstackDataView.h"
#include "DataViews/DataView.h"
#include "DataViews/SamplingReportInterface.h"
#include "absl/container/flat_hash_set.h"

class SamplingReport;

namespace orbit_data_views {

class SamplingReportDataView : public DataView {
 public:
  explicit SamplingReportDataView(AppInterface* app);

  const std::vector<Column>& GetColumns() override;
  int GetDefaultSortingColumn() override { return kColumnInclusive; }
  std::vector<std::vector<std::string>> GetContextMenuWithGrouping(
      int clicked_index, const std::vector<int>& selected_indices) override;
  std::string GetValue(int row, int column) override;
  std::string GetValueForCopy(int row, int column) override;
  const std::string& GetName() { return name_; }

  void OnSelect(const std::vector<int>& indices) override;
  void OnRefresh(const std::vector<int>& visible_selected_indices,
                 const RefreshMode& mode) override;

  void LinkDataView(DataView* data_view) override;
  void SetSamplingReport(SamplingReportInterface* sampling_report) {
    sampling_report_ = sampling_report;
  }
  void SetSampledFunctions(const std::vector<orbit_client_data::SampledFunction>& functions);
  void SetThreadID(orbit_client_data::ThreadID tid);
  orbit_client_data::ThreadID GetThreadID() const { return tid_; }

 protected:
  void DoSort() override;
  void DoFilter() override;
  const orbit_client_data::SampledFunction& GetSampledFunction(unsigned int row) const;
  orbit_client_data::SampledFunction& GetSampledFunction(unsigned int row);
  [[nodiscard]] std::optional<std::pair<std::string, std::string>> GetModulePathAndBuildIdFromRow(
      int row) const;

 private:
  [[nodiscard]] orbit_client_data::ModuleData* GetModuleDataFromRow(int row) const override;
  [[nodiscard]] const orbit_client_protos::FunctionInfo* GetFunctionInfoFromRow(int row) override;

  void UpdateSelectedIndicesAndFunctionIds(const std::vector<int>& selected_indices);
  void RestoreSelectedIndicesAfterFunctionsChanged();
  // The callstack view will be updated according to the visible selected addresses and thread id.
  void UpdateVisibleSelectedAddressesAndTid(const std::vector<int>& visible_selected_indices);

  std::vector<orbit_client_data::SampledFunction> functions_;
  // We need to keep user's selected function ids such that if functions_ changes, the
  // selected_indices_ can be updated according to the selected function ids.
  absl::flat_hash_set<uint64_t> selected_function_ids_;
  orbit_client_data::ThreadID tid_ = -1;
  std::string name_;
  SamplingReportInterface* sampling_report_ = nullptr;

  enum ColumnIndex {
    kColumnSelected,
    kColumnFunctionName,
    kColumnInclusive,  // Default sorting column.
    kColumnExclusive,
    kColumnModuleName,
    kColumnAddress,
    kColumnUnwindErrors,
    kNumColumns
  };
};

}  // namespace orbit_data_views