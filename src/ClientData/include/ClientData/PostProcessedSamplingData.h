// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_DATA_POST_PROCESSED_SAMPLING_DATA_H_
#define CLIENT_DATA_POST_PROCESSED_SAMPLING_DATA_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ClientData/CallstackTypes.h"
#include "ClientProtos/capture_data.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace orbit_client_data {

struct SampledFunction {
  SampledFunction() = default;

  std::string name;
  std::string module_path;
  uint32_t exclusive = 0;
  float exclusive_percent = 0.f;
  uint32_t inclusive = 0;
  float inclusive_percent = 0.f;
  uint32_t unwind_errors = 0;
  float unwind_errors_percent = 0.f;
  uint64_t absolute_address = 0;
  const orbit_client_protos::FunctionInfo* function = nullptr;
};

struct ThreadSampleData {
  ThreadSampleData() = default;

  ThreadID thread_id = 0;
  uint32_t samples_count = 0;
  absl::flat_hash_map<uint64_t, uint32_t> sampled_callstack_id_to_count;
  absl::flat_hash_map<uint64_t, uint32_t> sampled_address_to_count;
  absl::flat_hash_map<uint64_t, uint32_t> resolved_address_to_count;
  absl::flat_hash_map<uint64_t, uint32_t> resolved_address_to_exclusive_count;
  absl::flat_hash_map<uint64_t, uint32_t> resolved_address_to_error_count;
  std::multimap<uint32_t, uint64_t> sorted_count_to_resolved_address;
  std::vector<SampledFunction> sampled_functions;

  [[nodiscard]] uint32_t GetCountForAddress(uint64_t address) const;
};

struct CallstackCount {
  CallstackCount() = default;

  int count = 0;
  uint64_t callstack_id = 0;
};

struct SortedCallstackReport {
  SortedCallstackReport() = default;

  int total_callstack_count = 0;
  std::vector<CallstackCount> callstack_counts;
};

class PostProcessedSamplingData {
 public:
  PostProcessedSamplingData() = default;
  PostProcessedSamplingData(
      absl::flat_hash_map<ThreadID, ThreadSampleData> thread_id_to_sample_data,
      absl::flat_hash_map<uint64_t, orbit_client_protos::CallstackInfo> id_to_resolved_callstack,
      absl::flat_hash_map<uint64_t, uint64_t> original_id_to_resolved_callstack_id,
      absl::flat_hash_map<uint64_t, absl::flat_hash_set<uint64_t>>
          function_address_to_sampled_callstack_ids,
      std::vector<ThreadSampleData> sorted_thread_sample_data)
      : thread_id_to_sample_data_{std::move(thread_id_to_sample_data)},
        id_to_resolved_callstack_{std::move(id_to_resolved_callstack)},
        original_id_to_resolved_callstack_id_{std::move(original_id_to_resolved_callstack_id)},
        function_address_to_sampled_callstack_ids_{
            std::move(function_address_to_sampled_callstack_ids)},
        sorted_thread_sample_data_{std::move(sorted_thread_sample_data)} {};

  ~PostProcessedSamplingData() = default;

  PostProcessedSamplingData(const PostProcessedSamplingData& other) = default;
  PostProcessedSamplingData& operator=(const PostProcessedSamplingData& other) = default;
  PostProcessedSamplingData(PostProcessedSamplingData&& other) = default;
  PostProcessedSamplingData& operator=(PostProcessedSamplingData&& other) = default;

  [[nodiscard]] const orbit_client_protos::CallstackInfo& GetResolvedCallstack(
      uint64_t sampled_callstack_id) const;

  [[nodiscard]] std::unique_ptr<SortedCallstackReport>
  GetSortedCallstackReportFromFunctionAddresses(const std::vector<uint64_t>& function_addresses,
                                                ThreadID thread_id) const;

  [[nodiscard]] const std::vector<ThreadSampleData>& GetThreadSampleData() const {
    return sorted_thread_sample_data_;
  }
  [[nodiscard]] const ThreadSampleData* GetThreadSampleDataByThreadId(uint32_t thread_id) const;

  [[nodiscard]] const ThreadSampleData* GetSummary() const;
  [[nodiscard]] uint32_t GetCountOfFunction(uint64_t function_address) const;

 private:
  [[nodiscard]] std::multimap<int, uint64_t> GetCallstacksFromFunctionAddresses(
      const std::vector<uint64_t>& function_addresses, ThreadID thread_id) const;

  absl::flat_hash_map<ThreadID, ThreadSampleData> thread_id_to_sample_data_;
  absl::flat_hash_map<uint64_t, orbit_client_protos::CallstackInfo> id_to_resolved_callstack_;
  absl::flat_hash_map<uint64_t, uint64_t> original_id_to_resolved_callstack_id_;
  absl::flat_hash_map<uint64_t, absl::flat_hash_set<uint64_t>>
      function_address_to_sampled_callstack_ids_;
  std::vector<ThreadSampleData> sorted_thread_sample_data_;
};

}  // namespace orbit_client_data

#endif  // CLIENT_DATA_POST_PROCESSED_SAMPLING_DATA_H_
