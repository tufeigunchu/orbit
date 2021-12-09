// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "FramePointerValidatorClient.h"

#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>
#include <grpcpp/grpcpp.h>
#include <stddef.h>

#include <algorithm>
#include <chrono>
#include <string>

#include "App.h"
#include "ClientData/FunctionUtils.h"
#include "ClientData/ModuleData.h"
#include "ClientProtos/capture_data.pb.h"
#include "GrpcProtos/code_block.pb.h"
#include "GrpcProtos/services.grpc.pb.h"
#include "GrpcProtos/services.pb.h"
#include "OrbitBase/Logging.h"

using orbit_client_data::ModuleData;
using orbit_client_protos::FunctionInfo;
using orbit_grpc_protos::CodeBlock;
using orbit_grpc_protos::FramePointerValidatorService;
using orbit_grpc_protos::ValidateFramePointersRequest;
using orbit_grpc_protos::ValidateFramePointersResponse;

FramePointerValidatorClient::FramePointerValidatorClient(
    OrbitApp* app, const std::shared_ptr<grpc::Channel>& channel)
    : app_{app}, frame_pointer_validator_service_{FramePointerValidatorService::NewStub(channel)} {}

void FramePointerValidatorClient::AnalyzeModules(const std::vector<const ModuleData*>& modules) {
  if (modules.empty()) {
    ERROR("No module to validate, cancelling");
    return;
  }

  std::vector<std::string> dialogue_messages;
  dialogue_messages.emplace_back("Validation complete.");

  for (const ModuleData* module : modules) {
    ValidateFramePointersRequest request;
    ValidateFramePointersResponse response;
    CHECK(module != nullptr);

    std::vector<const FunctionInfo*> functions = module->GetFunctions();
    request.set_module_path(module->file_path());
    for (const FunctionInfo* function : functions) {
      CodeBlock* function_info = request.add_functions();
      function_info->set_offset(orbit_client_data::function_utils::Offset(*function, *module));
      function_info->set_size(function->size());
    }
    grpc::ClientContext context;
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() + std::chrono::minutes(1);
    context.set_deadline(deadline);

    // careful this is the synchronous call (maybe async is better)
    grpc::Status status =
        frame_pointer_validator_service_->ValidateFramePointers(&context, request, &response);

    if (!status.ok()) {
      return app_->SendErrorToUi(
          "Frame Pointer Validation",
          absl::StrFormat("Grpc call for frame-pointer validation failed for module %s: %s",
                          module->name(), status.error_message()));
    }
    size_t fpo_functions = response.functions_without_frame_pointer_size();
    size_t no_fpo_functions = functions.size() - fpo_functions;
    dialogue_messages.push_back(
        absl::StrFormat("Module %s: %d functions support frame pointers, %d functions don't.",
                        module->name(), no_fpo_functions, fpo_functions));
  }

  std::string text = absl::StrJoin(dialogue_messages, "\n");
  app_->SendInfoToUi("Frame Pointer Validation", text);
}
