// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_SERVICE_TARGET_SIDE_SERVER_H_
#define ORBIT_SERVICE_TARGET_SIDE_SERVER_H_

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string_view>

#include "CaptureService/CaptureStartStopListener.h"
#include "GrpcProtos/capture.pb.h"
#include "OrbitBase/Logging.h"
#include "ProducerEventProcessor/ProducerEventProcessor.h"
#include "ProducerSideServiceImpl.h"

namespace orbit_service {

// Wrapper around a grpc::Server that registers the service ProducerSideServiceImpl
// and listens on a Unix domain socket.
class ProducerSideServer final : public orbit_capture_service::CaptureStartStopListener {
 public:
  bool BuildAndStart(std::string_view unix_domain_socket_path);
  void ShutdownAndWait();

  void OnCaptureStartRequested(
      orbit_grpc_protos::CaptureOptions capture_options,
      orbit_producer_event_processor::ProducerEventProcessor* producer_event_processor) override;
  void OnCaptureStopRequested() override;

 private:
  ProducerSideServiceImpl producer_side_service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace orbit_service

#endif  // ORBIT_SERVICE_TARGET_SIDE_SERVER_H_
