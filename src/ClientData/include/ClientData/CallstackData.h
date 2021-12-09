// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_DATA_CALLSTACK_DATA_H_
#define CLIENT_DATA_CALLSTACK_DATA_H_

#include <stdint.h>

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "CallstackTypes.h"
#include "ClientProtos/capture_data.pb.h"
#include "absl/container/flat_hash_map.h"

namespace orbit_client_data {

class CallstackData {
 public:
  explicit CallstackData() = default;

  CallstackData(const CallstackData& other) = delete;
  CallstackData& operator=(const CallstackData& other) = delete;
  CallstackData(CallstackData&& other) = delete;
  CallstackData& operator=(CallstackData&& other) = delete;

  ~CallstackData() = default;

  // Assume that callstack_event.callstack_hash is filled correctly and the
  // Callstack with the corresponding id is already in unique_callstacks_.
  void AddCallstackEvent(orbit_client_protos::CallstackEvent callstack_event);
  void AddUniqueCallstack(uint64_t callstack_id, orbit_client_protos::CallstackInfo callstack);
  void AddCallstackFromKnownCallstackData(const orbit_client_protos::CallstackEvent& event,
                                          const CallstackData& known_callstack_data);

  [[nodiscard]] uint32_t GetCallstackEventsCount() const;

  [[nodiscard]] std::vector<orbit_client_protos::CallstackEvent> GetCallstackEventsInTimeRange(
      uint64_t time_begin, uint64_t time_end) const;

  [[nodiscard]] uint32_t GetCallstackEventsOfTidCount(uint32_t thread_id) const;

  [[nodiscard]] std::vector<orbit_client_protos::CallstackEvent> GetCallstackEventsOfTidInTimeRange(
      uint32_t tid, uint64_t time_begin, uint64_t time_end) const;

  void ForEachCallstackEvent(
      const std::function<void(const orbit_client_protos::CallstackEvent&)>& action) const;

  void ForEachCallstackEventInTimeRange(
      uint64_t min_timestamp, uint64_t max_timestamp,
      const std::function<void(const orbit_client_protos::CallstackEvent&)>& action) const;

  void ForEachCallstackEventOfTidInTimeRange(
      uint32_t tid, uint64_t min_timestamp, uint64_t max_timestamp,
      const std::function<void(const orbit_client_protos::CallstackEvent&)>& action) const;

  [[nodiscard]] uint64_t max_time() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return max_time_;
  }

  [[nodiscard]] uint64_t min_time() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return min_time_;
  }

  [[nodiscard]] const orbit_client_protos::CallstackInfo* GetCallstack(uint64_t callstack_id) const;

  [[nodiscard]] bool HasCallstack(uint64_t callstack_id) const;

  void ForEachUniqueCallstack(
      const std::function<void(uint64_t callstack_id,
                               const orbit_client_protos::CallstackInfo& callstack)>& action) const;

  [[nodiscard]] absl::flat_hash_map<uint64_t, std::shared_ptr<orbit_client_protos::CallstackInfo>>
  GetUniqueCallstacksCopy() const;

  // Assuming that, for each thread, the outermost frame of each callstack is always the same,
  // update the type of all the kComplete callstacks that have the outermost frame not matching the
  // majority outermost frame. This is a way to filter unwinding errors that were not reported as
  // such.
  void UpdateCallstackTypeBasedOnMajorityStart();

 private:
  [[nodiscard]] std::shared_ptr<orbit_client_protos::CallstackInfo> GetCallstackPtr(
      uint64_t callstack_id) const;

  void RegisterTime(uint64_t time);

  // Use a reentrant mutex so that calls to the ForEach... methods can be nested.
  // E.g., one might want to nest ForEachCallstackEvent and ForEachFrameInCallstack.
  mutable std::recursive_mutex mutex_;
  absl::flat_hash_map<uint64_t, std::shared_ptr<orbit_client_protos::CallstackInfo>>
      unique_callstacks_;
  absl::flat_hash_map<int32_t, std::map<uint64_t, orbit_client_protos::CallstackEvent>>
      callstack_events_by_tid_;

  uint64_t max_time_ = 0;
  uint64_t min_time_ = std::numeric_limits<uint64_t>::max();
};

}  // namespace orbit_client_data

#endif  // CLIENT_DATA_CALLSTACK_DATA_H_
