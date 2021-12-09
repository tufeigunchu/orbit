// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_QT_CAPTURE_OPTIONS_DIALOG_H_
#define ORBIT_QT_CAPTURE_OPTIONS_DIALOG_H_

#include <QDialog>
#include <QObject>
#include <QString>
#include <QValidator>
#include <QWidget>
#include <memory>

#include "GrpcProtos/capture.pb.h"
#include "OrbitBase/Logging.h"
#include "ui_CaptureOptionsDialog.h"

namespace orbit_qt {

class UInt64Validator : public QValidator {
 public:
  explicit UInt64Validator(QObject* parent = nullptr) : QValidator(parent) {}
  explicit UInt64Validator(uint64_t minimum, QObject* parent = nullptr)
      : QValidator(parent), minimum_(minimum) {}
  QValidator::State validate(QString& input, int& /*pos*/) const override {
    if (input.isEmpty()) {
      return QValidator::State::Acceptable;
    }
    bool valid = false;
    uint64_t input_value = input.toULongLong(&valid);
    if (valid && input_value >= minimum_) {
      return QValidator::State::Acceptable;
    }
    return QValidator::State::Invalid;
  }

 private:
  uint64_t minimum_ = 0;
};

class CaptureOptionsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit CaptureOptionsDialog(QWidget* parent = nullptr);

  void SetEnableSampling(bool enable_sampling);
  [[nodiscard]] bool GetEnableSampling() const;
  void SetSamplingPeriodMs(double sampling_period_ms);
  [[nodiscard]] double GetSamplingPeriodMs() const;
  void SetUnwindingMethod(orbit_grpc_protos::CaptureOptions::UnwindingMethod unwinding_method);
  [[nodiscard]] orbit_grpc_protos::CaptureOptions::UnwindingMethod GetUnwindingMethod() const;
  void SetCollectSchedulerInfo(bool collect_scheduler_info);
  [[nodiscard]] bool GetCollectSchedulerInfo() const;
  void SetCollectThreadStates(bool collect_thread_state);
  [[nodiscard]] bool GetCollectThreadStates() const;
  void SetTraceGpuSubmissions(bool trace_gpu_submissions);
  [[nodiscard]] bool GetTraceGpuSubmissions() const;
  void SetEnableApi(bool enable_api);
  [[nodiscard]] bool GetEnableApi() const;
  void SetDynamicInstrumentationMethod(
      orbit_grpc_protos::CaptureOptions::DynamicInstrumentationMethod method);
  [[nodiscard]] orbit_grpc_protos::CaptureOptions::DynamicInstrumentationMethod
  GetDynamicInstrumentationMethod() const;
  void SetEnableIntrospection(bool enable_introspection);
  [[nodiscard]] bool GetEnableIntrospection() const;

  void SetLimitLocalMarkerDepthPerCommandBuffer(bool limit_local_marker_depth_per_command_buffer);
  [[nodiscard]] bool GetLimitLocalMarkerDepthPerCommandBuffer() const;
  void SetMaxLocalMarkerDepthPerCommandBuffer(uint64_t local_marker_depth_per_command_buffer);
  [[nodiscard]] uint64_t GetMaxLocalMarkerDepthPerCommandBuffer() const;

  void SetCollectMemoryInfo(bool collect_memory_info);
  [[nodiscard]] bool GetCollectMemoryInfo() const;
  void SetMemorySamplingPeriodMs(uint64_t memory_sampling_period_ms);
  [[nodiscard]] uint64_t GetMemorySamplingPeriodMs() const;
  void SetMemoryWarningThresholdKb(uint64_t memory_warning_threshold_kb);
  [[nodiscard]] uint64_t GetMemoryWarningThresholdKb() const;

 public slots:
  void ResetLocalMarkerDepthLineEdit();
  void ResetMemorySamplingPeriodMsLineEditWhenEmpty();
  void ResetMemoryWarningThresholdKbLineEditWhenEmpty();

 private:
  std::unique_ptr<Ui::CaptureOptionsDialog> ui_;
  UInt64Validator uint64_validator_;
};

}  // namespace orbit_qt

#endif  // ORBIT_QT_CAPTURE_OPTIONS_DIALOG_H_
