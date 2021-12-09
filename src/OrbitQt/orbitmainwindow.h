// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_QT_ORBIT_MAIN_WINDOW_H_
#define ORBIT_QT_ORBIT_MAIN_WINDOW_H_

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QObject>
#include <QPoint>
#include <QPushButton>
#include <QString>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "App.h"
#include "CallTreeView.h"
#include "ClientProtos/capture_data.pb.h"
#include "ClientServices/ProcessManager.h"
#include "DataViews/DataView.h"
#include "DataViews/DataViewType.h"
#include "FilterPanelWidgetAction.h"
#include "GrpcProtos/process.pb.h"
#include "MainThreadExecutor.h"
#include "MetricsUploader/MetricsUploader.h"
#include "OrbitBase/CrashHandler.h"
#include "SessionSetup/ServiceDeployManager.h"
#include "SessionSetup/TargetConfiguration.h"
#include "SessionSetup/TargetLabel.h"
#include "StatusListener.h"
#include "orbitglwidget.h"

namespace Ui {
class OrbitMainWindow;
}

class OrbitMainWindow final : public QMainWindow, public orbit_gl::MainWindowInterface {
  Q_OBJECT

 public:
  static constexpr int kQuitOrbitReturnCode = 0;
  static constexpr int kEndSessionReturnCode = 1;

  explicit OrbitMainWindow(orbit_session_setup::TargetConfiguration target_configuration,
                           const orbit_base::CrashHandler* crash_handler,
                           orbit_metrics_uploader::MetricsUploader* metrics_uploader = nullptr,
                           const QStringList& command_line_flags = QStringList());
  ~OrbitMainWindow() override;

  void RegisterGlWidget(OrbitGLWidget* widget) { gl_widgets_.push_back(widget); }
  void UnregisterGlWidget(OrbitGLWidget* widget) {
    const auto it = std::find(gl_widgets_.begin(), gl_widgets_.end(), widget);
    if (it != gl_widgets_.end()) {
      gl_widgets_.erase(it);
    }
  }
  void OnRefreshDataViewPanels(orbit_data_views::DataViewType type);
  void UpdatePanel(orbit_data_views::DataViewType type);

  void OnNewSamplingReport(orbit_data_views::DataView* callstack_data_view,
                           const std::shared_ptr<class SamplingReport>& sampling_report);
  void OnNewSelectionReport(orbit_data_views::DataView* callstack_data_view,
                            const std::shared_ptr<class SamplingReport>& sampling_report);

  void OnNewTopDownView(std::unique_ptr<CallTreeView> top_down_view);
  void OnNewSelectionTopDownView(std::unique_ptr<CallTreeView> selection_top_down_view);

  void OnNewBottomUpView(std::unique_ptr<CallTreeView> bottom_up_view);
  void OnNewSelectionBottomUpView(std::unique_ptr<CallTreeView> selection_bottom_up_view);

  std::string OnGetSaveFileName(const std::string& extension);
  void OnSetClipboard(const std::string& text);
  void OpenCapture(const std::string& filepath);
  void OnCaptureCleared();

  Ui::OrbitMainWindow* GetUi() { return ui; }

  bool eventFilter(QObject* watched, QEvent* event) override;

  void RestoreDefaultTabLayout();

  [[nodiscard]] orbit_session_setup::TargetConfiguration ClearTargetConfiguration();

  void ShowTooltip(std::string_view message) override;
  void ShowSourceCode(
      const std::filesystem::path& file_path, size_t line_number,
      std::optional<std::unique_ptr<orbit_code_report::CodeReport>> maybe_code_report) override;
  void ShowDisassembly(const orbit_client_protos::FunctionInfo& function_info,
                       const std::string& assembly,
                       orbit_code_report::DisassemblyReport report) override;

  void AppendToCaptureLog(CaptureLogSeverity severity, absl::Duration capture_time,
                          std::string_view message) override;
  void ShowWarningWithDontShowAgainCheckboxIfNeeded(
      std::string_view title, std::string_view text,
      std::string_view dont_show_again_setting_key) override;

 protected:
  void closeEvent(QCloseEvent* event) override;

 public slots:
  void OnFilterFunctionsTextChanged(const QString& text);
  void OnFilterTracksTextChanged(const QString& text);

 private slots:
  void on_actionOpenUserDataDirectory_triggered();
  void on_actionOpenAppDataDirectory_triggered();
  void on_actionAbout_triggered();

  void on_actionReport_Missing_Feature_triggered();
  void on_actionReport_Bug_triggered();

  void OnTimer();
  void OnLiveTabFunctionsFilterTextChanged(const QString& text);

  void on_actionOpen_Preset_triggered();
  void on_actionSave_Preset_As_triggered();

  void on_actionEnd_Session_triggered();
  void on_actionQuit_triggered();

  void on_actionToggle_Capture_triggered();
  void on_actionOpen_Capture_triggered();
  void on_actionRename_Capture_File_triggered();
  void on_actionCaptureOptions_triggered();
  void on_actionHelp_toggled(bool checked);
  void on_actionIntrospection_triggered();

  void on_actionCheckFalse_triggered();
  void on_actionStackOverflow_triggered();
  void on_actionServiceCheckFalse_triggered();
  void on_actionServiceStackOverflow_triggered();

  void on_actionSourcePathMappings_triggered();

  void on_actionSymbolsDialog_triggered();

  void OnTimerSelectionChanged(const orbit_client_protos::TimerInfo* timer_info);

 private:
  void UpdateFilePath(const std::filesystem::path& file_path);
  void StartMainTimer();
  void SetupCaptureToolbar();
  void SetupMainWindow();
  void SetupHintFrame();
  void SetupTargetLabel();
  void SetupStatusBarLogButton();
  void SetupTrackConfigurationUi();

  void SetupAccessibleNamesForAutomation();

  void SaveCurrentTabLayoutAsDefaultInMemory();

  void SaveMainWindowGeometry();
  void RestoreMainWindowGeometry();

  void CreateTabBarContextMenu(QTabWidget* tab_widget, int tab_index, const QPoint& pos);
  void UpdateCaptureStateDependentWidgets();
  void UpdateProcessConnectionStateDependentWidgets();
  void ClearCaptureFilters();

  void UpdateActiveTabsAfterSelection(bool selection_has_samples);

  QTabWidget* FindParentTabWidget(const QWidget* widget) const;

  void SetTarget(const orbit_session_setup::StadiaTarget& target);
  void SetTarget(const orbit_session_setup::LocalTarget& target);
  void SetTarget(const orbit_session_setup::FileTarget& target);

  void OnProcessListUpdated(const std::vector<orbit_grpc_protos::ProcessInfo>& processes);

  static const QString kEnableCallstackSamplingSettingKey;
  static const QString kCallstackSamplingPeriodMsSettingKey;
  static const QString kCallstackUnwindingMethodSettingKey;
  static const QString kCollectSchedulerInfoSettingKey;
  static const QString kCollectThreadStatesSettingKey;
  static const QString kTraceGpuSubmissionsSettingKey;
  static const QString kCollectMemoryInfoSettingKey;
  static const QString kEnableApiSettingKey;
  static const QString kEnableIntrospectionSettingKey;
  static const QString kDynamicInstrumentationMethodSettingKey;
  static const QString kMemorySamplingPeriodMsSettingKey;
  static const QString kMemoryWarningThresholdKbSettingKey;
  static const QString kLimitLocalMarkerDepthPerCommandBufferSettingsKey;
  static const QString kMaxLocalMarkerDepthPerCommandBufferSettingsKey;
  static const QString kMainWindowGeometrySettingKey;
  static const QString kMainWindowStateSettingKey;

  void LoadCaptureOptionsIntoApp();

  [[nodiscard]] bool ConfirmExit();
  void Exit(int return_code);

  void OnStadiaConnectionError(std::error_code error);

  void UpdateCaptureToolbarIconOpacity();

  std::optional<QString> LoadSourceCode(const std::filesystem::path& file_path);

 private:
  std::shared_ptr<MainThreadExecutor> main_thread_executor_;
  std::unique_ptr<OrbitApp> app_;
  Ui::OrbitMainWindow* ui;
  FilterPanelWidgetAction* filter_panel_action_ = nullptr;
  QTimer* main_timer_ = nullptr;
  std::vector<OrbitGLWidget*> gl_widgets_;
  std::unique_ptr<OrbitGLWidget> introspection_widget_ = nullptr;
  QFrame* hint_frame_ = nullptr;
  orbit_session_setup::TargetLabel* target_label_ = nullptr;
  QPushButton* capture_log_button_ = nullptr;

  QStringList command_line_flags_;

  // Capture toolbar.
  QIcon icon_start_capture_;
  QIcon icon_stop_capture_;
  QIcon icon_toolbar_extension_;

  QIcon icon_keyboard_arrow_left_;
  QIcon icon_keyboard_arrow_right_;

  // Status listener
  std::unique_ptr<StatusListener> status_listener_;

  struct TabWidgetLayout {
    std::vector<std::pair<QWidget*, QString>> tabs_and_titles;
    int current_index;
  };
  std::map<QTabWidget*, TabWidgetLayout> default_tab_layout_;

  orbit_session_setup::TargetConfiguration target_configuration_;

  enum class TargetProcessState { kRunning, kEnded };

  TargetProcessState target_process_state_ = TargetProcessState::kEnded;

  // This value indicates whether Orbit (Ui / OrbitQt) is connected to an OrbitService. This can
  // currently be connection to a Stadia instance (ssh tunnel via ServiceDeployManager) or a
  // connection to an OrbitService running on the local machine. If Orbit displays a capture saved
  // to a file, it is not connected and this bool is false. This is also false when the connection
  // broke.
  bool is_connected_ = false;

  orbit_metrics_uploader::MetricsUploader* metrics_uploader_;
};

#endif  // ORBIT_QT_ORBIT_MAIN_WINDOW_H_
