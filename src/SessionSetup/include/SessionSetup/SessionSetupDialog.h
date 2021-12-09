// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SESSION_SETUP_PROFILING_TARGET_DIALOG_H_
#define SESSION_SETUP_PROFILING_TARGET_DIALOG_H_

#include <grpcpp/channel.h>

#include <QDialog>
#include <QHistoryState>
#include <QModelIndex>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QState>
#include <QStateMachine>
#include <QString>
#include <QWidget>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "ClientData/ProcessData.h"
#include "ClientServices/ProcessManager.h"
#include "Connections.h"
#include "GrpcProtos/process.pb.h"
#include "MetricsUploader/MetricsUploader.h"
#include "ProcessItemModel.h"
#include "TargetConfiguration.h"

namespace Ui {
class SessionSetupDialog;  // IWYU pragma: keep
}
namespace orbit_session_setup {

class SessionSetupDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SessionSetupDialog(SshConnectionArtifacts* ssh_connection_artifacts,
                              std::optional<TargetConfiguration> target_configuration_opt,
                              orbit_metrics_uploader::MetricsUploader* metrics_uploader,
                              QWidget* parent = nullptr);
  ~SessionSetupDialog() override;

  [[nodiscard]] std::optional<TargetConfiguration> Exec();
 private slots:
  void SetupStadiaProcessManager();
  void SetupLocalProcessManager();
  void TearDownProcessManager();
  void ProcessSelectionChanged(const QModelIndex& current);
  void ConnectToLocal();

 signals:
  void ProcessSelected();
  void NoProcessSelected();
  void StadiaIsConnected();
  void ProcessListUpdated();

 private:
  std::unique_ptr<Ui::SessionSetupDialog> ui_;

  ProcessItemModel process_model_;
  QSortFilterProxyModel process_proxy_model_;

  std::unique_ptr<orbit_client_data::ProcessData> process_;
  std::unique_ptr<orbit_client_services::ProcessManager> process_manager_;

  std::shared_ptr<grpc::Channel> local_grpc_channel_;
  uint16_t local_grpc_port_;

  std::filesystem::path selected_file_path_;

  orbit_metrics_uploader::MetricsUploader* metrics_uploader_;

  // State Machine & States
  QStateMachine state_machine_;
  QState state_stadia_;
  QHistoryState state_stadia_history_;
  QState state_stadia_connecting_;
  QState state_stadia_connected_;
  QState state_stadia_processes_loading_;
  QState state_stadia_process_selected_;
  QState state_stadia_no_process_selected_;

  QState state_file_;
  QHistoryState state_file_history_;
  QState state_file_selected_;
  QState state_file_no_selection_;

  QState state_local_;
  QHistoryState state_local_history_;
  QState state_local_connecting_;
  QState state_local_connected_;
  QState state_local_processes_loading_;
  QState state_local_process_selected_;
  QState state_local_no_process_selected_;

  void SetupStadiaStates();
  void SetupFileStates();
  void SetupLocalStates();
  void SetStateMachineInitialState();
  [[nodiscard]] bool TrySelectProcessByName(const std::string& process_name);
  void OnProcessListUpdate(std::vector<orbit_grpc_protos::ProcessInfo> process_list);
  void SetupProcessManager(const std::shared_ptr<grpc::Channel>& grpc_channel);
  void SetTargetAndStateMachineInitialState(StadiaTarget target);
  void SetTargetAndStateMachineInitialState(LocalTarget target);
  void SetTargetAndStateMachineInitialState(FileTarget target);
};

}  // namespace orbit_session_setup

#endif  // SESSION_SETUP_PROFILING_TARGET_DIALOG_H_