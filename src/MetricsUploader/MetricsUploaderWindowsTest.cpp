// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <chrono>

#include "MetricsUploader/MetricsUploader.h"
#include "MetricsUploader/MetricsUploaderStub.h"
#include "OrbitBase/Result.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"

namespace orbit_metrics_uploader {

using orbit_metrics_uploader::MetricsUploaderStub;

[[nodiscard]] static bool IsMetricsUploaderStub(
    const std::unique_ptr<MetricsUploader>& metrics_uploader) {
  return dynamic_cast<MetricsUploaderStub*>(metrics_uploader.get()) != nullptr;
}

TEST(MetricsUploader, CreateMetricsUploaderFromClientWithoutSendEvent) {
  auto metrics_uploader =
      MetricsUploader::CreateMetricsUploader("MetricsUploaderClientWithoutSendEvent");
  EXPECT_TRUE(IsMetricsUploaderStub(metrics_uploader));
}

TEST(MetricsUploader, CreateMetricsUploaderFromClientWithoutSetup) {
  auto metrics_uploader =
      MetricsUploader::CreateMetricsUploader("MetricsUploaderClientWithoutSetup");
  EXPECT_TRUE(IsMetricsUploaderStub(metrics_uploader));
}

TEST(MetricsUploader, CreateMetricsUploaderFromClientWithoutShutdown) {
  auto metrics_uploader =
      MetricsUploader::CreateMetricsUploader("MetricsUploaderClientWithoutShutdown");
  EXPECT_TRUE(IsMetricsUploaderStub(metrics_uploader));
}

TEST(MetricsUploader, SetupMetricsUploaderWithError) {
  auto metrics_uploader =
      MetricsUploader::CreateMetricsUploader("MetricsUploaderSetupWithErrorClient");
  EXPECT_TRUE(IsMetricsUploaderStub(metrics_uploader));
}

TEST(MetricsUploader, SendLogEvent) {
  auto metrics_uploader = MetricsUploader::CreateMetricsUploader("MetricsUploaderCompleteClient");
  EXPECT_FALSE(IsMetricsUploaderStub(metrics_uploader));
  bool result = metrics_uploader->SendLogEvent(OrbitLogEvent::UNKNOWN_EVENT_TYPE);
  EXPECT_FALSE(result);
  result = metrics_uploader->SendLogEvent(OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN);
  EXPECT_TRUE(result);
  result = metrics_uploader->SendLogEvent(OrbitLogEvent::ORBIT_CAPTURE_DURATION,
                                          std::chrono::milliseconds(100));
  EXPECT_TRUE(result);
  result = metrics_uploader->SendLogEvent(OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN,
                                          std::chrono::milliseconds(0), OrbitLogEvent::SUCCESS);
  EXPECT_TRUE(result);
}

TEST(MetricsUploader, SendCaptureEvent) {
  auto metrics_uploader = MetricsUploader::CreateMetricsUploader("MetricsUploaderCompleteClient");
  EXPECT_FALSE(IsMetricsUploaderStub(metrics_uploader));
  bool result = metrics_uploader->SendCaptureEvent(OrbitCaptureData{}, OrbitLogEvent::SUCCESS);
  EXPECT_TRUE(result);
}

TEST(MetricsUploader, CreateTwoMetricsUploaders) {
  auto metrics_uploader1 = MetricsUploader::CreateMetricsUploader("MetricsUploaderCompleteClient");
  EXPECT_FALSE(IsMetricsUploaderStub(metrics_uploader1));
  auto metrics_uploader2 = MetricsUploader::CreateMetricsUploader("MetricsUploaderCompleteClient");
  EXPECT_TRUE(IsMetricsUploaderStub(metrics_uploader2));
}

TEST(MetricsUploader, CreateMetricsUploaderFromNonexistentClient) {
  auto metrics_uploader =
      MetricsUploader::CreateMetricsUploader("NonexistentMetricsUploaderClient");
  EXPECT_TRUE(IsMetricsUploaderStub(metrics_uploader));
}

TEST(MetricsUploader, GenerateUUID) {
  ErrorMessageOr<std::string> uuid_result = GenerateUUID();
  ASSERT_TRUE(uuid_result.has_value());
}

TEST(MetricsUploader, CheckUUIDFormat) {
  ErrorMessageOr<std::string> uuid_result = GenerateUUID();
  ASSERT_TRUE(uuid_result.has_value());

  const std::string& uuid{uuid_result.value()};

  EXPECT_EQ(uuid.length(), 36);
  EXPECT_EQ(uuid[8], '-');
  EXPECT_EQ(uuid[13], '-');
  EXPECT_EQ(uuid[18], '-');
  EXPECT_EQ(uuid[23], '-');

  EXPECT_EQ(uuid[14], '4');  // Version 4

  EXPECT_EQ(absl::AsciiStrToLower(uuid), uuid);
}

TEST(MetricsUploader, CheckUUIDUniqueness) {
  absl::flat_hash_set<std::string> hash_set;
  for (int i = 0; i < 1000; ++i) {
    ErrorMessageOr<std::string> uuid = GenerateUUID();
    ASSERT_TRUE(uuid.has_value());
    auto [unused_it, success] = hash_set.insert(uuid.value());
    EXPECT_TRUE(success);
  }
}

}  // namespace orbit_metrics_uploader