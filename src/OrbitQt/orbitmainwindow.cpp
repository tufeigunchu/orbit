// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "orbitmainwindow.h"

#include <absl/container/flat_hash_set.h>
#include <absl/flags/declare.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFlags>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIODevice>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <Qt>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "AnnotatingSourceCodeDialog.h"
#include "App.h"
#include "CallTreeWidget.h"
#include "CaptureClient/CaptureClient.h"
#include "CaptureClient/CaptureListener.h"
#include "CaptureOptionsDialog.h"
#include "ClientData/CaptureData.h"
#include "ClientData/ProcessData.h"
#include "ClientFlags/ClientFlags.h"
#include "ClientModel/CaptureSerializer.h"
#include "ClientProtos/capture_data.pb.h"
#include "ClientServices/ProcessManager.h"
#include "CodeReport/DisassemblyReport.h"
#include "CodeViewer/Dialog.h"
#include "CodeViewer/FontSizeInEm.h"
#include "CodeViewer/OwningDialog.h"
#include "ConfigWidgets/SourcePathsMappingDialog.h"
#include "ConfigWidgets/SymbolsDialog.h"
#include "DataViewFactory.h"
#include "DataViews/DataViewType.h"
#include "DataViews/LiveFunctionsDataView.h"
#include "DisplayFormats/DisplayFormats.h"
#include "GlCanvas.h"
#include "GrpcProtos/capture.pb.h"
#include "GrpcProtos/services.pb.h"
#include "Introspection/Introspection.h"
#include "LiveFunctionsController.h"
#include "OrbitBase/ExecutablePath.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/ReadFileToString.h"
#include "OrbitBase/Result.h"
#include "OrbitGgp/Instance.h"
#include "OrbitPaths/Paths.h"
#include "OrbitVersion/OrbitVersion.h"
#include "QtUtils/MainThreadExecutorImpl.h"
#include "SamplingReport.h"
#include "SessionSetup/Connections.h"
#include "SessionSetup/ServiceDeployManager.h"
#include "SessionSetup/TargetConfiguration.h"
#include "SessionSetup/TargetLabel.h"
#include "SourcePathsMapping/Mapping.h"
#include "SourcePathsMapping/MappingManager.h"
#include "SourcePathsMappingUI/AskUserForFile.h"
#include "StatusListenerImpl.h"
#include "SymbolPaths/QSettingsWrapper.h"
#include "Symbols/SymbolHelper.h"
#include "SyntaxHighlighter/Cpp.h"
#include "SyntaxHighlighter/X86Assembly.h"
#include "TutorialContent.h"
#include "absl/flags/flag.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "orbitaboutdialog.h"
#include "orbitdataviewpanel.h"
#include "orbitglwidget.h"
#include "orbitlivefunctions.h"
#include "orbitsamplingreport.h"
#include "types.h"
#include "ui_orbitmainwindow.h"

using orbit_capture_client::CaptureClient;
using orbit_capture_client::CaptureListener;

using orbit_grpc_protos::CaptureOptions;
using orbit_grpc_protos::CrashOrbitServiceRequest_CrashType;
using orbit_grpc_protos::CrashOrbitServiceRequest_CrashType_CHECK_FALSE;
using orbit_grpc_protos::CrashOrbitServiceRequest_CrashType_STACK_OVERFLOW;

using orbit_session_setup::LocalTarget;
using orbit_session_setup::ServiceDeployManager;
using orbit_session_setup::StadiaTarget;
using orbit_session_setup::TargetConfiguration;
using orbit_session_setup::TargetLabel;

using orbit_data_views::DataViewType;

using DynamicInstrumentationMethod =
    orbit_grpc_protos::CaptureOptions::DynamicInstrumentationMethod;
using UnwindingMethod = orbit_grpc_protos::CaptureOptions::UnwindingMethod;

namespace {
const QString kLightGrayColor = "rgb(117, 117, 117)";
const QString kMediumGrayColor = "rgb(68, 68, 68)";
const QString kGreenColor = "rgb(41, 218, 130)";
constexpr int kHintFramePosX = 21;
constexpr int kHintFramePosY = 62;
constexpr int kHintFrameWidth = 140;
constexpr int kHintFrameHeight = 45;
}  // namespace

OrbitMainWindow::OrbitMainWindow(TargetConfiguration target_configuration,
                                 const orbit_base::CrashHandler* crash_handler,
                                 orbit_metrics_uploader::MetricsUploader* metrics_uploader,
                                 const QStringList& command_line_flags)
    : QMainWindow(nullptr),
      main_thread_executor_{orbit_qt_utils::MainThreadExecutorImpl::Create()},
      app_{OrbitApp::Create(this, main_thread_executor_.get(), crash_handler, metrics_uploader)},
      ui(new Ui::OrbitMainWindow),
      command_line_flags_(command_line_flags),
      target_configuration_(std::move(target_configuration)),
      metrics_uploader_(metrics_uploader) {
  SetupMainWindow();

  SetupTargetLabel();
  SetupStatusBarLogButton();
  SetupHintFrame();

  DataViewFactory* data_view_factory = app_.get();
  ui->ModulesList->Initialize(data_view_factory->GetOrCreateDataView(DataViewType::kModules),
                              SelectionType::kExtended, FontType::kDefault);
  ui->FunctionsList->Initialize(data_view_factory->GetOrCreateDataView(DataViewType::kFunctions),
                                SelectionType::kExtended, FontType::kDefault);
  ui->PresetsList->Initialize(data_view_factory->GetOrCreateDataView(DataViewType::kPresets),
                              SelectionType::kDefault, FontType::kDefault,
                              /*is_main_instance=*/true, /*uniform_row_height=*/false,
                              /*text_alignment=*/Qt::AlignTop | Qt::AlignLeft);

  std::visit([this](const auto& target) { SetTarget(target); }, target_configuration_);

  app_->PostInit(is_connected_);

  uint16_t stack_dump_size = absl::GetFlag(FLAGS_stack_dump_size);
  CHECK(stack_dump_size <= 65000 && stack_dump_size > 0);
  app_->SetStackDumpSize(stack_dump_size);

  SaveCurrentTabLayoutAsDefaultInMemory();

  UpdateCaptureStateDependentWidgets();

  LoadCaptureOptionsIntoApp();

  metrics_uploader_->SendLogEvent(orbit_metrics_uploader::OrbitLogEvent::ORBIT_MAIN_WINDOW_OPEN);

  // SymbolPaths.txt deprecation code
  // If file does not exist, do nothing. (It means the user never used an older Orbit version or
  // manually deleted the file)
  if (!fs::is_regular_file(orbit_paths::GetSymbolsFilePath())) return;

  // If it exists, check if it starts with deprecation note.
  ErrorMessageOr<bool> symbol_paths_file_has_depr_note =
      orbit_symbols::FileStartsWithDeprecationNote(orbit_paths::GetSymbolsFilePath());
  if (symbol_paths_file_has_depr_note.has_error()) {
    ERROR("Unable to check SymbolPaths.txt file for depreciation note, error: %s",
          symbol_paths_file_has_depr_note.error().message());
    return;
  }

  // If file already has the deprecation note, that means it was already added to QSettings. Dont do
  // anything else.
  if (symbol_paths_file_has_depr_note.value()) return;

  // Otherwise, read SymbolPaths.txt file and merge contents with QSettings paths

  // Note: There is no std::hash implementation for std::filesystem::path. It is also not trivial to
  // compare if 2 paths are pointing to the same target (compare: /foo/bar and /foo/bar/../bar).
  // This merging here via a hash set of the std::string representation only accomplishes that
  // paths with the same string representation are only added once.
  absl::flat_hash_set<std::string> already_seen_paths;
  std::vector<fs::path> dirs_to_save;

  for (const auto& dir : orbit_symbols::ReadSymbolsFile(orbit_paths::GetSymbolsFilePath())) {
    if (!already_seen_paths.contains(dir.string())) {
      already_seen_paths.insert(dir.string());
      dirs_to_save.push_back(dir);
    }
  }

  for (const auto& dir : orbit_symbol_paths::LoadPaths()) {
    if (!already_seen_paths.contains(dir.string())) {
      already_seen_paths.insert(dir.string());
      dirs_to_save.push_back(dir);
    }
  }

  orbit_symbol_paths::SavePaths(dirs_to_save);

  ErrorMessageOr<void> add_depr_note_result =
      orbit_symbols::AddDeprecationNoteToFile(orbit_paths::GetSymbolsFilePath());
  if (add_depr_note_result.has_error()) {
    ERROR("Unable to add deprecation note to SymbolPaths.txt, error: %s",
          add_depr_note_result.error().message());
  }
}

void OrbitMainWindow::UpdateFilePath(const std::filesystem::path& file_path) {
  target_label_->SetFile(file_path);
  setWindowTitle(QString::fromStdString(file_path.string()));
}

void OrbitMainWindow::SetupMainWindow() {
  DataViewFactory* data_view_factory = app_.get();

  ui->setupUi(this);
  RestoreMainWindowGeometry();

  ui->splitter_2->setSizes({5000, 5000});

  status_listener_ = StatusListenerImpl::Create(statusBar());

  app_->SetStatusListener(status_listener_.get());

  app_->SetCaptureStartedCallback([this](const std::optional<std::filesystem::path>& file_path) {
    // Only set it if this is not empty, we do not want to reset the label when loading from legacy
    // file format.
    if (file_path.has_value()) {
      UpdateFilePath(file_path.value());
    }

    // we want to call UpdateCaptureStateDependentWidgets after we update
    // target_label_ since the state of some actions depend on it.
    UpdateCaptureStateDependentWidgets();
    ClearCaptureFilters();
  });

  constexpr const char* kFinalizingCaptureMessage =
      "<div align=\"left\">"
      "Please wait while the capture is being finalized..."
      "<ul>"
      "<li>Waiting for the remaining capture data</li>"
      "<li>Processing callstacks</li>"
      "<li>Cleaning up dynamic instrumentation</li>"
      "</ul>"
      "</div>";
  auto* finalizing_capture_dialog =
      new QProgressDialog(kFinalizingCaptureMessage, "OK", 0, 0, this, Qt::Tool);
  finalizing_capture_dialog->setWindowTitle("Finalizing capture");
  finalizing_capture_dialog->setModal(true);
  finalizing_capture_dialog->setWindowFlags(
      (finalizing_capture_dialog->windowFlags() | Qt::CustomizeWindowHint) &
      ~Qt::WindowCloseButtonHint & ~Qt::WindowSystemMenuHint);
  finalizing_capture_dialog->setFixedSize(finalizing_capture_dialog->size());
  finalizing_capture_dialog->close();

  app_->SetCaptureStopRequestedCallback([this, finalizing_capture_dialog] {
    finalizing_capture_dialog->show();
    UpdateCaptureStateDependentWidgets();
  });
  auto capture_finished_callback = [this, finalizing_capture_dialog] {
    finalizing_capture_dialog->close();
    UpdateCaptureStateDependentWidgets();
  };
  app_->SetCaptureStoppedCallback(capture_finished_callback);
  app_->SetCaptureFailedCallback(capture_finished_callback);
  app_->SetCaptureClearedCallback([this] { OnCaptureCleared(); });

  app_->SetRefreshCallback([this](DataViewType type) {
    if (type == DataViewType::kAll || type == DataViewType::kLiveFunctions) {
      this->ui->liveFunctions->OnDataChanged();
    }
    this->OnRefreshDataViewPanels(type);
  });

  app_->SetSamplingReportCallback([this](orbit_data_views::DataView* callstack_data_view,
                                         const std::shared_ptr<SamplingReport>& report) {
    this->OnNewSamplingReport(callstack_data_view, report);
  });

  app_->SetSelectionReportCallback([this](orbit_data_views::DataView* callstack_data_view,
                                          const std::shared_ptr<SamplingReport>& report) {
    this->OnNewSelectionReport(callstack_data_view, report);
  });

  app_->SetTopDownViewCallback([this](std::unique_ptr<CallTreeView> top_down_view) {
    this->OnNewTopDownView(std::move(top_down_view));
  });

  app_->SetSelectionTopDownViewCallback(
      [this](std::unique_ptr<CallTreeView> selection_top_down_view) {
        this->OnNewSelectionTopDownView(std::move(selection_top_down_view));
      });

  app_->SetBottomUpViewCallback([this](std::unique_ptr<CallTreeView> bottom_up_view) {
    this->OnNewBottomUpView(std::move(bottom_up_view));
  });

  app_->SetSelectionBottomUpViewCallback(
      [this](std::unique_ptr<CallTreeView> selection_bottom_up_view) {
        this->OnNewSelectionBottomUpView(std::move(selection_bottom_up_view));
      });

  app_->SetSelectLiveTabCallback([this] { ui->RightTabWidget->setCurrentWidget(ui->liveTab); });
  app_->SetErrorMessageCallback([this](const std::string& title, const std::string& text) {
    QMessageBox::critical(this, QString::fromStdString(title), QString::fromStdString(text));
  });
  app_->SetWarningMessageCallback([this](const std::string& title, const std::string& text) {
    QMessageBox::warning(this, QString::fromStdString(title), QString::fromStdString(text));
  });
  app_->SetInfoMessageCallback([this](const std::string& title, const std::string& text) {
    QMessageBox::information(this, QString::fromStdString(title), QString::fromStdString(text));
  });
  app_->SetSaveFileCallback(
      [this](const std::string& extension) { return this->OnGetSaveFileName(extension); });
  app_->SetClipboardCallback([this](const std::string& text) { this->OnSetClipboard(text); });

  ui->CaptureGLWidget->Initialize(GlCanvas::CanvasType::kCaptureWindow, this, app_.get());

  app_->SetTimerSelectedCallback([this](const orbit_client_protos::TimerInfo* timer_info) {
    OnTimerSelectionChanged(timer_info);
  });

  if (absl::GetFlag(FLAGS_devmode)) {
    ui->debugOpenGLWidget->Initialize(GlCanvas::CanvasType::kDebug, this, app_.get());
    app_->SetDebugCanvas(ui->debugOpenGLWidget->GetCanvas());
  } else {
    ui->RightTabWidget->removeTab(ui->RightTabWidget->indexOf(ui->debugTab));
  }

  ui->TracepointsList->Initialize(
      data_view_factory->GetOrCreateDataView(DataViewType::kTracepoints), SelectionType::kExtended,
      FontType::kDefault);

  if (!absl::GetFlag(FLAGS_enable_tracepoint_feature)) {
    ui->RightTabWidget->removeTab(ui->RightTabWidget->indexOf(ui->tracepointsTab));
  }

  if (!absl::GetFlag(FLAGS_devmode)) {
    ui->menuDebug->menuAction()->setVisible(false);
  }

  if (absl::GetFlag(FLAGS_enable_tutorials_feature)) {
    InitTutorials(this);
  }

  SetupCaptureToolbar();
  SetupTrackConfigurationUi();

  icon_keyboard_arrow_left_ = QIcon(":/actions/keyboard_arrow_left");
  icon_keyboard_arrow_right_ = QIcon(":/actions/keyboard_arrow_right");

  StartMainTimer();

  ui->liveFunctions->Initialize(app_.get(), metrics_uploader_, SelectionType::kExtended,
                                FontType::kDefault);

  connect(ui->liveFunctions->GetFilterLineEdit(), &QLineEdit::textChanged, this,
          [this](const QString& text) { OnLiveTabFunctionsFilterTextChanged(text); });

  ui->topDownWidget->Initialize(app_.get());
  ui->selectionTopDownWidget->Initialize(app_.get());
  ui->bottomUpWidget->Initialize(app_.get());
  ui->selectionBottomUpWidget->Initialize(app_.get());

  ui->MainTabWidget->tabBar()->installEventFilter(this);
  ui->RightTabWidget->tabBar()->installEventFilter(this);

  SetupAccessibleNamesForAutomation();

  setWindowTitle({});
  std::filesystem::path icon_file_name = (orbit_base::GetExecutableDir() / "orbit.ico");
  this->setWindowIcon(QIcon(QString::fromStdString(icon_file_name.string())));

  if (!absl::GetFlag(FLAGS_devmode)) {
    ui->actionIntrospection->setVisible(false);
  }
}

static QWidget* CreateSpacer(QWidget* parent) {
  auto* spacer = new QLabel(parent);
  spacer->setText("    ");
  return spacer;
}

void OrbitMainWindow::SetupCaptureToolbar() {
  // Sizes.
  QToolBar* toolbar = ui->capture_toolbar;

  // Create missing icons
  icon_start_capture_ = QIcon(":/actions/play_arrow");
  icon_stop_capture_ = QIcon(":/actions/stop");
  icon_toolbar_extension_ = QIcon(":/actions/double_arrows");

  // Attach the filter panel to the toolbar
  toolbar->addWidget(CreateSpacer(toolbar));
  filter_panel_action_ = new FilterPanelWidgetAction(toolbar);
  connect(filter_panel_action_, &FilterPanelWidgetAction::FilterTracksTextChanged, this,
          &OrbitMainWindow::OnFilterTracksTextChanged);
  connect(filter_panel_action_, &FilterPanelWidgetAction::FilterFunctionsTextChanged, this,
          &OrbitMainWindow::OnFilterFunctionsTextChanged);
  toolbar->addAction(filter_panel_action_);
  toolbar->findChild<QToolButton*>("qt_toolbar_ext_button")->setIcon(icon_toolbar_extension_);
}

void OrbitMainWindow::SetupHintFrame() {
  hint_frame_ = new QFrame();
  hint_frame_->setStyleSheet("background: transparent");
  auto* hint_layout = new QVBoxLayout();
  hint_layout->setSpacing(0);
  hint_layout->setMargin(0);
  hint_frame_->setLayout(hint_layout);
  auto* hint_arrow = new QLabel();
  hint_arrow->setPixmap(QPixmap(":/images/tutorial/grey_arrow_up.png").scaledToHeight(12));
  hint_layout->addWidget(hint_arrow);
  auto* hint_message = new QLabel("Start a capture here");
  hint_message->setAlignment(Qt::AlignCenter);
  hint_layout->addWidget(hint_message);
  hint_message->setStyleSheet(QString("background-color: %1;"
                                      "border-top-left-radius: 1px;"
                                      "border-top-right-radius: 4px;"
                                      "border-bottom-right-radius: 4px;"
                                      "border-bottom-left-radius: 4px;")
                                  .arg(kLightGrayColor));
  hint_layout->setStretchFactor(hint_message, 1);
  hint_frame_->setParent(ui->CaptureTab);

  hint_frame_->move(kHintFramePosX, kHintFramePosY);
  hint_frame_->resize(kHintFrameWidth, kHintFrameHeight);
}

void OrbitMainWindow::SetupTargetLabel() {
  auto* target_widget = new QWidget();
  target_widget->setStyleSheet(QString("background-color: %1").arg(kMediumGrayColor));
  target_label_ = new TargetLabel{};
  target_label_->setContentsMargins(6, 0, 0, 0);
  auto* disconnect_target_button = new QPushButton("End Session");
  auto* target_layout = new QHBoxLayout();
  target_layout->addWidget(target_label_);
  target_layout->addWidget(disconnect_target_button);
  target_layout->setMargin(0);
  target_widget->setLayout(target_layout);

  ui->menuBar->setCornerWidget(target_widget, Qt::TopRightCorner);

  QObject::connect(disconnect_target_button, &QPushButton::clicked, this,
                   [this] { on_actionEnd_Session_triggered(); });

  QObject::connect(target_label_, &TargetLabel::SizeChanged, this, [this, target_widget]() {
    target_label_->adjustSize();
    target_widget->adjustSize();
    ui->menuBar->setCornerWidget(target_widget, Qt::TopRightCorner);
  });
}

void OrbitMainWindow::SetupStatusBarLogButton() {
  // The Qt Designer doesn't seem to support adding children to a StatusBar.
  auto* capture_log_widget = new QWidget(statusBar());  // NOLINT
  statusBar()->setContentsMargins(0, 0, 0, 0);
  statusBar()->addPermanentWidget(capture_log_widget);

  auto* capture_log_layout = new QHBoxLayout(capture_log_widget);  // NOLINT
  capture_log_layout->setContentsMargins(0, 0, 9, 0);
  capture_log_widget->setLayout(capture_log_layout);

  static const QIcon icon = [] {
    QIcon icon;
    QPixmap expand_up_pixmap = QPixmap{":/actions/expand_up"};
    QPixmap expand_down_pixmap = QPixmap{":/actions/expand_down"};

    // Reduce opacity for the Disabled mode.
    QPixmap expand_up_disabled_pixmap = QPixmap{expand_up_pixmap.size()};
    expand_up_disabled_pixmap.fill(Qt::transparent);
    QPainter expand_up_disabled_painter{&expand_up_disabled_pixmap};
    expand_up_disabled_painter.setOpacity(0.3);
    expand_up_disabled_painter.drawPixmap(0, 0, expand_up_pixmap);
    expand_up_disabled_painter.end();

    icon.addPixmap(expand_up_pixmap, QIcon::Normal);
    icon.addPixmap(expand_down_pixmap, QIcon::Normal, QIcon::On);
    icon.addPixmap(expand_up_disabled_pixmap, QIcon::Disabled);
    return icon;
  }();

  capture_log_button_ = new QPushButton("Capture Log", statusBar());  // NOLINT
  capture_log_button_->setEnabled(false);
  capture_log_button_->setCheckable(true);
  capture_log_button_->setIcon(icon);
  capture_log_button_->setStyleSheet(
      "padding-left: 11; padding-right: 11; padding-top: 2; padding-bottom: 2;");
  capture_log_layout->addWidget(capture_log_button_);

  QObject::connect(capture_log_button_, &QPushButton::toggled, [this](bool checked) {
    if (checked) {
      ui->captureLogWidget->show();
    } else {
      ui->captureLogWidget->hide();
    }
  });
}

void OrbitMainWindow::SetupTrackConfigurationUi() {
  QList<int> sizes;
  // Resize the splitter to force the track config UI to minimal size.
  // Usually the size policies should take care of this, but for reasons
  // unknown I can't get this to work with those two widgets...
  sizes.append(0);
  sizes.append(16777215);
  ui->captureWindowSplitter->setSizes(sizes);
  ui->trackConfig->hide();
  QObject::connect(ui->actionConfigureTracks, &QAction::toggled,
                   [this](bool checked) { ui->trackConfig->setVisible(checked); });
}

void OrbitMainWindow::SetupAccessibleNamesForAutomation() {
  for (QTabWidget* tab_widget : {ui->MainTabWidget, ui->RightTabWidget}) {
    for (int i = 0; i < tab_widget->count(); ++i) {
      tab_widget->widget(i)->setAccessibleName(tab_widget->widget(i)->objectName());
    }
  }
}

void OrbitMainWindow::SaveCurrentTabLayoutAsDefaultInMemory() {
  default_tab_layout_.clear();
  std::array<QTabWidget*, 2> tab_widgets = {ui->MainTabWidget, ui->RightTabWidget};
  for (QTabWidget* tab_widget : tab_widgets) {
    TabWidgetLayout layout = {};
    for (int i = 0; i < tab_widget->count(); ++i) {
      layout.tabs_and_titles.emplace_back(tab_widget->widget(i), tab_widget->tabText(i));
    }
    layout.current_index = tab_widget->currentIndex();
    default_tab_layout_[tab_widget] = layout;
  }
}

void OrbitMainWindow::SaveMainWindowGeometry() {
  QSettings settings;
  settings.setValue(kMainWindowGeometrySettingKey, saveGeometry());
  settings.setValue(kMainWindowStateSettingKey, saveState());
}

void OrbitMainWindow::RestoreMainWindowGeometry() {
  QSettings settings;
  restoreGeometry(settings.value(kMainWindowGeometrySettingKey).toByteArray());
  restoreState(settings.value(kMainWindowStateSettingKey).toByteArray());
}

void OrbitMainWindow::CreateTabBarContextMenu(QTabWidget* tab_widget, int tab_index,
                                              const QPoint& pos) {
  QMenu context_menu(this);
  context_menu.setAccessibleName("TabBarContextMenu");
  QAction move_action;
  QTabWidget* other_widget;

  if (tab_widget == ui->MainTabWidget) {
    move_action.setIcon(icon_keyboard_arrow_right_);
    move_action.setText(QString("Move \"") + tab_widget->tabText(tab_index) + "\" to right pane");
    other_widget = ui->RightTabWidget;
  } else if (tab_widget == ui->RightTabWidget) {
    move_action.setIcon(icon_keyboard_arrow_left_);
    move_action.setText(QString("Move \"") + tab_widget->tabText(tab_index) + "\" to left pane");
    other_widget = ui->MainTabWidget;
  } else {
    UNREACHABLE();
  }

  move_action.setEnabled(tab_widget->count() > 0);

  QObject::connect(&move_action, &QAction::triggered, [this, tab_widget, other_widget, tab_index] {
    QWidget* tab = tab_widget->widget(tab_index);
    QString text = tab_widget->tabText(tab_index);
    tab_widget->removeTab(tab_index);
    other_widget->addTab(tab, text);
    UpdateCaptureStateDependentWidgets();
    if (tab->isEnabled()) {
      other_widget->setCurrentWidget(tab);
    }
  });
  context_menu.addAction(&move_action);
  context_menu.exec(pos);
}

void OrbitMainWindow::UpdateCaptureStateDependentWidgets() {
  auto set_tab_enabled = [this](QWidget* widget, bool enabled) -> void {
    QTabWidget* tab_widget = FindParentTabWidget(widget);
    CHECK(tab_widget != nullptr);
    tab_widget->setTabEnabled(tab_widget->indexOf(widget), enabled);
  };

  const bool has_data = app_->HasCaptureData();
  const bool has_selection = has_data && app_->HasSampleSelection();
  CaptureClient::State capture_state = app_->GetCaptureState();
  const bool is_capturing = capture_state != CaptureClient::State::kStopped;
  const bool is_target_process_running = target_process_state_ == TargetProcessState::kRunning;

  set_tab_enabled(ui->SymbolsTab, true);
  set_tab_enabled(ui->CaptureTab, true);
  set_tab_enabled(ui->liveTab, has_data);
  set_tab_enabled(ui->samplingTab, has_data && !is_capturing);
  set_tab_enabled(ui->topDownTab, has_data && !is_capturing);
  set_tab_enabled(ui->bottomUpTab, has_data && !is_capturing);
  set_tab_enabled(ui->selectionSamplingTab, has_selection);
  set_tab_enabled(ui->selectionTopDownTab, has_selection);
  set_tab_enabled(ui->selectionBottomUpTab, has_selection);

  ui->actionToggle_Capture->setEnabled(
      capture_state == CaptureClient::State::kStarted ||
      (capture_state == CaptureClient::State::kStopped && is_target_process_running));
  ui->actionToggle_Capture->setIcon(is_capturing ? icon_stop_capture_ : icon_start_capture_);
  ui->actionCaptureOptions->setEnabled(!is_capturing);
  ui->actionOpen_Capture->setEnabled(!is_capturing);
  ui->actionRename_Capture_File->setEnabled(!is_capturing &&
                                            target_label_->GetFilePath().has_value());
  ui->actionOpen_Preset->setEnabled(!is_capturing && is_connected_);
  ui->actionSave_Preset_As->setEnabled(!is_capturing);
  ui->actionConfigureTracks->setEnabled(has_data);

  filter_panel_action_->setEnabled(has_data);

  hint_frame_->setVisible(!has_data);

  filter_panel_action_->SetTimerLabelText(
      QString::fromStdString(orbit_display_formats::GetDisplayTime(app_->GetCaptureTime())));

  UpdateCaptureToolbarIconOpacity();

  capture_log_button_->setEnabled(has_data);
  if (capture_state == CaptureClient::State::kStarting) {
    capture_log_button_->setChecked(true);
  } else if (capture_state == CaptureClient::State::kStopped) {
    capture_log_button_->setChecked(false);
  }

  if (has_data) {
    TrackManager* track_manager = dynamic_cast<CaptureWindow*>(ui->CaptureGLWidget->GetCanvas())
                                      ->GetTimeGraph()
                                      ->GetTrackManager();
    ui->trackConfig->SetTrackManager(track_manager);
  }
}

void OrbitMainWindow::UpdateCaptureToolbarIconOpacity() {
  // Gray out disabled actions on the capture toolbar.
  for (QAction* action : ui->capture_toolbar->actions()) {
    // setGraphicsEffect(effect) transfers the ownership of effect to the QWidget. If the effect
    // is installed on a different item, setGraphicsEffect() will remove the effect from the
    // original item and install it on this item.
    auto* effect = new QGraphicsOpacityEffect;
    effect->setOpacity(action->isEnabled() ? 1 : 0.3);
    ui->capture_toolbar->widgetForAction(action)->setGraphicsEffect(effect);
  }
}

void OrbitMainWindow::UpdateProcessConnectionStateDependentWidgets() {
  CaptureClient::State capture_state = app_->GetCaptureState();
  const bool is_capturing = capture_state != CaptureClient::State::kStopped;
  const bool is_target_process_running = target_process_state_ == TargetProcessState::kRunning;

  ui->actionToggle_Capture->setEnabled(
      capture_state == CaptureClient::State::kStarted ||
      (capture_state == CaptureClient::State::kStopped && is_target_process_running));
  ui->actionOpen_Preset->setEnabled(!is_capturing && is_connected_);

  UpdateCaptureToolbarIconOpacity();
}

void OrbitMainWindow::ClearCaptureFilters() { filter_panel_action_->ClearEdits(); }

void OrbitMainWindow::UpdateActiveTabsAfterSelection(bool selection_has_samples) {
  const QTabWidget* capture_parent = FindParentTabWidget(ui->CaptureTab);

  // Automatically switch between (complete capture) report and selection report tabs
  // if applicable
  auto show_corresponding_selection_tab = [this, capture_parent, selection_has_samples](
                                              const std::vector<QWidget*>& report_tabs,
                                              QWidget* selection_tab) {
    QTabWidget* selection_parent = FindParentTabWidget(selection_tab);

    // If the capture window is in the same tab widget as the selection, do not change anything
    if (selection_parent == capture_parent) {
      return;
    }

    if (selection_has_samples) {
      // Non-empty selection: If one of the corresponding complete reports was visible,
      // show the selection tab instead
      if (std::find(report_tabs.begin(), report_tabs.end(), selection_parent->currentWidget()) !=
          report_tabs.end()) {
        selection_parent->setCurrentWidget(selection_tab);
      }
    } else {
      // Empty selection: If the selection tab was visible, switch back to the first complete
      // report that is in the same tab widget
      if (selection_parent->currentWidget() == selection_tab) {
        for (const auto& report_tab : report_tabs) {
          QTabWidget* report_parent = FindParentTabWidget(report_tab);
          if (selection_parent == report_parent &&
              report_parent->isTabEnabled(report_parent->indexOf(report_tab))) {
            selection_parent->setCurrentWidget(report_tab);
            break;
          }
        }
      }
    }
  };

  show_corresponding_selection_tab({ui->samplingTab, ui->liveTab, ui->SymbolsTab},
                                   ui->selectionSamplingTab);
  show_corresponding_selection_tab({ui->topDownTab, ui->liveTab, ui->SymbolsTab},
                                   ui->selectionTopDownTab);
  show_corresponding_selection_tab({ui->bottomUpTab, ui->liveTab, ui->SymbolsTab},
                                   ui->selectionBottomUpTab);
}

QTabWidget* OrbitMainWindow::FindParentTabWidget(const QWidget* widget) const {
  std::array<QTabWidget*, 2> potential_parents = {ui->MainTabWidget, ui->RightTabWidget};
  for (QTabWidget* tab_widget : potential_parents) {
    for (int i = 0; i < tab_widget->count(); ++i) {
      if (tab_widget->widget(i) == widget) {
        return tab_widget;
      }
    }
  }

  return nullptr;
}

OrbitMainWindow::~OrbitMainWindow() {
  DeinitTutorials();

  ui->selectionBottomUpWidget->Deinitialize();
  ui->bottomUpWidget->Deinitialize();
  ui->selectionTopDownWidget->Deinitialize();
  ui->topDownWidget->Deinitialize();
  ui->TracepointsList->Deinitialize();
  ui->liveFunctions->Deinitialize();

  ui->samplingReport->Deinitialize();
  ui->selectionReport->Deinitialize();

  if (absl::GetFlag(FLAGS_devmode)) {
    ui->debugOpenGLWidget->Deinitialize(this);
  }

  ui->CaptureGLWidget->Deinitialize(this);
  ui->PresetsList->Deinitialize();
  ui->FunctionsList->Deinitialize();
  ui->ModulesList->Deinitialize();

  delete ui;

  // This explicitly destructs the main_thread_executor_ before all other members.
  // That ensures that all scheduled main thread tasks will be destructed before
  // we destruct all the resources these tasks might rely on.
  main_thread_executor_.reset();
}

void OrbitMainWindow::OnRefreshDataViewPanels(DataViewType type) {
  if (type == DataViewType::kAll) {
    for (int i = 0; i < static_cast<int>(DataViewType::kAll); ++i) {
      UpdatePanel(static_cast<DataViewType>(i));
    }
  } else {
    UpdatePanel(type);
  }
}

void OrbitMainWindow::UpdatePanel(DataViewType type) {
  switch (type) {
    case DataViewType::kFunctions:
      ui->FunctionsList->Refresh();
      break;
    case DataViewType::kLiveFunctions:
      ui->liveFunctions->Refresh();
      break;
    case DataViewType::kModules:
      ui->ModulesList->Refresh();
      break;
    case DataViewType::kPresets:
      ui->PresetsList->Refresh();
      break;
    case DataViewType::kSampling:
      ui->samplingReport->RefreshCallstackView();
      ui->samplingReport->RefreshTabs();
      ui->selectionReport->RefreshCallstackView();
      ui->selectionReport->RefreshTabs();
      break;
    default:
      break;
  }
}

void OrbitMainWindow::OnNewSamplingReport(orbit_data_views::DataView* callstack_data_view,
                                          const std::shared_ptr<SamplingReport>& sampling_report) {
  ui->samplingGridLayout->removeWidget(ui->samplingReport);
  delete ui->samplingReport;

  ui->samplingReport = new OrbitSamplingReport(ui->samplingTab);
  ui->samplingReport->Initialize(callstack_data_view, sampling_report);
  ui->samplingGridLayout->addWidget(ui->samplingReport, 0, 0, 1, 1);

  UpdateCaptureStateDependentWidgets();

  // Switch to sampling tab if:
  //  * Report is non-empty
  //  * Sampling-tab is not in the same widget as the capture tab
  //  * Live-tab isn't selected in the same widget as the sampling tab
  QTabWidget* sampling_tab_parent = FindParentTabWidget(ui->samplingTab);
  if (sampling_report->HasSamples() &&
      (FindParentTabWidget(ui->CaptureTab) != sampling_tab_parent) &&
      (sampling_tab_parent->currentWidget() != ui->liveTab)) {
    sampling_tab_parent->setCurrentWidget(ui->samplingTab);
  }
}

void OrbitMainWindow::OnNewSelectionReport(orbit_data_views::DataView* callstack_data_view,
                                           const std::shared_ptr<SamplingReport>& sampling_report) {
  ui->selectionGridLayout->removeWidget(ui->selectionReport);
  delete ui->selectionReport;
  bool has_samples = sampling_report->HasSamples();

  ui->selectionReport = new OrbitSamplingReport(ui->selectionSamplingTab);
  ui->selectionReport->Initialize(callstack_data_view, sampling_report);
  ui->selectionGridLayout->addWidget(ui->selectionReport, 0, 0, 1, 1);

  UpdateActiveTabsAfterSelection(has_samples);
  UpdateCaptureStateDependentWidgets();
}

void OrbitMainWindow::OnNewTopDownView(std::unique_ptr<CallTreeView> top_down_view) {
  ui->topDownWidget->SetTopDownView(std::move(top_down_view));
}

void OrbitMainWindow::OnNewSelectionTopDownView(
    std::unique_ptr<CallTreeView> selection_top_down_view) {
  ui->selectionTopDownWidget->SetTopDownView(std::move(selection_top_down_view));
}

void OrbitMainWindow::OnNewBottomUpView(std::unique_ptr<CallTreeView> bottom_up_view) {
  ui->bottomUpWidget->SetBottomUpView(std::move(bottom_up_view));
}

void OrbitMainWindow::OnNewSelectionBottomUpView(
    std::unique_ptr<CallTreeView> selection_bottom_up_view) {
  ui->selectionBottomUpWidget->SetBottomUpView(std::move(selection_bottom_up_view));
}

std::string OrbitMainWindow::OnGetSaveFileName(const std::string& extension) {
  QFileDialog dialog(this);
  dialog.setFileMode(QFileDialog::FileMode::AnyFile);
  dialog.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);
  dialog.setNameFilter(QString::fromStdString(extension + " (*" + extension + ")"));
  dialog.setWindowTitle("Specify a file to save...");
  dialog.setDirectory(nullptr);
  std::string filename;

  if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) {
    filename = dialog.selectedFiles()[0].toStdString();
  }
  if (!filename.empty() && !absl::EndsWith(filename, extension)) {
    filename += extension;
  }
  return filename;
}

void OrbitMainWindow::OnSetClipboard(const std::string& text) {
  QApplication::clipboard()->setText(QString::fromStdString(text));
}

void OrbitMainWindow::on_actionReport_Missing_Feature_triggered() {
  if (!QDesktopServices::openUrl(
          QUrl("https://community.stadia.dev/s/feature-requests", QUrl::StrictMode))) {
    QMessageBox::critical(this, "Error opening URL",
                          "Could not open community.stadia.dev/s/feature-request");
  }
}

void OrbitMainWindow::on_actionReport_Bug_triggered() {
  if (!QDesktopServices::openUrl(
          QUrl("https://community.stadia.dev/s/contactsupport", QUrl::StrictMode))) {
    QMessageBox::critical(this, "Error opening URL",
                          "Could not open community.stadia.dev/s/contactsupport");
  }
}

void OrbitMainWindow::on_actionOpenUserDataDirectory_triggered() {
  std::string user_data_dir = orbit_paths::CreateOrGetOrbitUserDataDir().string();
  QUrl user_data_url = QUrl::fromLocalFile(QString::fromStdString(user_data_dir));
  if (!QDesktopServices::openUrl(user_data_url)) {
    QMessageBox::critical(this, "Error opening directory",
                          "Could not open Orbit user data directory");
  }
}

void OrbitMainWindow::on_actionOpenAppDataDirectory_triggered() {
  std::string app_data_dir = orbit_paths::CreateOrGetOrbitAppDataDir().string();
  QUrl app_data_url = QUrl::fromLocalFile(QString::fromStdString(app_data_dir));
  if (!QDesktopServices::openUrl(app_data_url)) {
    QMessageBox::critical(this, "Error opening directory",
                          "Could not open Orbit app data directory");
  }
}

void OrbitMainWindow::on_actionAbout_triggered() {
  orbit_qt::OrbitAboutDialog dialog{this};
  dialog.setWindowTitle("About");
  dialog.SetVersionString(QCoreApplication::applicationVersion());
  dialog.SetBuildInformation(QString::fromStdString(orbit_version::GetBuildReport()));

  QOpenGLContext* const current_context = QOpenGLContext::currentContext();
  if (current_context != nullptr) {
    QOpenGLFunctions* const functions = current_context->functions();

    const auto gl_get_string = [&](GLenum value) {
      // NOLINTNEXTLINE
      return QString::fromLocal8Bit(reinterpret_cast<const char*>(functions->glGetString(value)));
    };

    auto renderer = QString{"%1 %2 %3"}.arg(gl_get_string(GL_VENDOR), gl_get_string(GL_RENDERER),
                                            gl_get_string(GL_VERSION));

    // The simplest way to detect software rendering is to match the renderer's name.
    // Unfortunately Qt does not provide a simple function for that.
    QRegularExpression llvmpipe_matcher{"llvmpipe", QRegularExpression::CaseInsensitiveOption};
    QRegularExpressionMatch match =
        llvmpipe_matcher.match(renderer, QRegularExpression::PartialPreferFirstMatch);
    const bool is_software_renderer = match.hasMatch();

    dialog.SetOpenGlRenderer(renderer, is_software_renderer);
  }

  QFile license_file{QDir{QCoreApplication::applicationDirPath()}.filePath("NOTICE")};
  if (license_file.open(QIODevice::ReadOnly)) {
    dialog.SetLicenseText(license_file.readAll());
  }
  dialog.exec();
}

void OrbitMainWindow::StartMainTimer() {
  main_timer_ = new QTimer(this);
  connect(main_timer_, SIGNAL(timeout()), this, SLOT(OnTimer()));

  // Update period set to 16ms (~60FPS)
  int msec = 16;
  main_timer_->start(msec);
}

void OrbitMainWindow::OnTimer() {
  ORBIT_SCOPE("OrbitMainWindow::OnTimer");
  app_->MainTick();

  for (OrbitGLWidget* gl_widget : gl_widgets_) {
    if (gl_widget->GetCanvas() != nullptr && gl_widget->GetCanvas()->IsRedrawNeeded()) {
      gl_widget->update();
    }
  }

  if (app_->IsCapturing()) {
    filter_panel_action_->SetTimerLabelText(
        QString::fromStdString(orbit_display_formats::GetDisplayTime(app_->GetCaptureTime())));
  }
}

void OrbitMainWindow::OnFilterFunctionsTextChanged(const QString& text) {
  // The toolbar and live tab filters are mirrored.
  ui->liveFunctions->SetFilter(text);
}

void OrbitMainWindow::OnLiveTabFunctionsFilterTextChanged(const QString& text) {
  // Set main toolbar functions filter without triggering signals.
  filter_panel_action_->SetFilterFunctionsText(text);
}

void OrbitMainWindow::OnFilterTracksTextChanged(const QString& text) {
  app_->FilterTracks(text.toStdString());
}

void OrbitMainWindow::on_actionOpen_Preset_triggered() {
  QStringList list = QFileDialog::getOpenFileNames(
      this, "Select a file to open...",
      QString::fromStdString(orbit_paths::CreateOrGetPresetDir().string()), "*.opr");
  for (const auto& file : list) {
    ErrorMessageOr<void> result = app_->OnLoadPreset(file.toStdString());
    if (result.has_error()) {
      QMessageBox::critical(this, "Error loading preset",
                            absl::StrFormat("Could not load preset from \"%s\":\n%s.",
                                            file.toStdString(), result.error().message())
                                .c_str());
    }
    break;
  }
}

void OrbitMainWindow::on_actionSave_Preset_As_triggered() {
  QString file = QFileDialog::getSaveFileName(
      this, "Specify a file to save...",
      QString::fromStdString(orbit_paths::CreateOrGetPresetDir().string()), "*.opr");
  if (file.isEmpty()) {
    return;
  }

  ErrorMessageOr<void> result = app_->OnSavePreset(file.toStdString());
  if (result.has_error()) {
    QMessageBox::critical(this, "Error saving preset",
                          absl::StrFormat("Could not save preset in \"%s\":\n%s.",
                                          file.toStdString(), result.error().message())
                              .c_str());
  }
}

void OrbitMainWindow::on_actionEnd_Session_triggered() {
  if (ConfirmExit()) Exit(kEndSessionReturnCode);
}

void OrbitMainWindow::on_actionQuit_triggered() {
  if (ConfirmExit()) Exit(kQuitOrbitReturnCode);
}

void OrbitMainWindow::on_actionToggle_Capture_triggered() { app_->ToggleCapture(); }

const QString OrbitMainWindow::kEnableCallstackSamplingSettingKey{"EnableCallstackSampling"};
const QString OrbitMainWindow::kCallstackSamplingPeriodMsSettingKey{"CallstackSamplingPeriodMs"};
const QString OrbitMainWindow::kCallstackUnwindingMethodSettingKey{"CallstackUnwindingMethod"};
const QString OrbitMainWindow::kCollectSchedulerInfoSettingKey{"CollectSchedulerInfo"};
const QString OrbitMainWindow::kCollectThreadStatesSettingKey{"CollectThreadStates"};
const QString OrbitMainWindow::kTraceGpuSubmissionsSettingKey{"TraceGpuSubmissions"};
const QString OrbitMainWindow::kCollectMemoryInfoSettingKey{"CollectMemoryInfo"};
const QString OrbitMainWindow::kEnableApiSettingKey{"EnableApi"};
const QString OrbitMainWindow::kEnableIntrospectionSettingKey{"EnableIntrospection"};
const QString OrbitMainWindow::kDynamicInstrumentationMethodSettingKey{
    "DynamicInstrumentationMethod"};
const QString OrbitMainWindow::kMemorySamplingPeriodMsSettingKey{"MemorySamplingPeriodMs"};
const QString OrbitMainWindow::kMemoryWarningThresholdKbSettingKey{"MemoryWarningThresholdKb"};
const QString OrbitMainWindow::kLimitLocalMarkerDepthPerCommandBufferSettingsKey{
    "LimitLocalMarkerDepthPerCommandBuffer"};
const QString OrbitMainWindow::kMaxLocalMarkerDepthPerCommandBufferSettingsKey{
    "MaxLocalMarkerDepthPerCommandBuffer"};
const QString OrbitMainWindow::kMainWindowGeometrySettingKey{"MainWindowGeometry"};
const QString OrbitMainWindow::kMainWindowStateSettingKey{"MainWindowState"};

constexpr double kCallstackSamplingPeriodMsDefaultValue = 1.0;
constexpr UnwindingMethod kCallstackUnwindingMethodDefaultValue = CaptureOptions::kDwarf;
constexpr uint64_t kMemorySamplingPeriodMsDefaultValue = 10;
constexpr uint64_t kMemoryWarningThresholdKbDefaultValue = 1024 * 1024 * 8;  // 8Gb
constexpr DynamicInstrumentationMethod kDynamicInstrumentationMethodDefaultValue =
    CaptureOptions::kKernelUprobes;

void OrbitMainWindow::LoadCaptureOptionsIntoApp() {
  QSettings settings;
  if (!app_->IsDevMode() || settings.value(kEnableCallstackSamplingSettingKey, true).toBool()) {
    bool conversion_succeeded = false;
    double sampling_period_ms =
        settings.value(kCallstackSamplingPeriodMsSettingKey, kCallstackSamplingPeriodMsDefaultValue)
            .toDouble(&conversion_succeeded);
    if (!conversion_succeeded || sampling_period_ms <= 0.0) {
      ERROR("Invalid value for setting \"%s\", resetting to %.1f",
            kCallstackSamplingPeriodMsSettingKey.toStdString(),
            kCallstackSamplingPeriodMsDefaultValue);
      settings.setValue(kCallstackSamplingPeriodMsSettingKey,
                        kCallstackSamplingPeriodMsDefaultValue);
      sampling_period_ms = kCallstackSamplingPeriodMsDefaultValue;
    }
    app_->SetSamplesPerSecond(1000.0 / sampling_period_ms);

    UnwindingMethod unwinding_method = static_cast<UnwindingMethod>(
        settings
            .value(kCallstackUnwindingMethodSettingKey,
                   static_cast<int>(kCallstackUnwindingMethodDefaultValue))
            .toInt());
    if (unwinding_method != CaptureOptions::kDwarf &&
        unwinding_method != CaptureOptions::kFramePointers) {
      unwinding_method = kCallstackUnwindingMethodDefaultValue;
    }
    app_->SetUnwindingMethod(unwinding_method);
  } else {
    app_->SetSamplesPerSecond(0.0);
  }

  app_->SetCollectSchedulerInfo(settings.value(kCollectSchedulerInfoSettingKey, true).toBool());
  app_->SetCollectThreadStates(settings.value(kCollectThreadStatesSettingKey, false).toBool());
  app_->SetTraceGpuSubmissions(settings.value(kTraceGpuSubmissionsSettingKey, true).toBool());
  app_->SetEnableApi(settings.value(kEnableApiSettingKey, true).toBool());
  app_->SetEnableIntrospection(settings.value(kEnableIntrospectionSettingKey, false).toBool());
  DynamicInstrumentationMethod instrumentation_method = static_cast<DynamicInstrumentationMethod>(
      settings
          .value(kDynamicInstrumentationMethodSettingKey,
                 static_cast<int>(kDynamicInstrumentationMethodDefaultValue))
          .toInt());
  if (instrumentation_method != CaptureOptions::kKernelUprobes &&
      instrumentation_method != CaptureOptions::kUserSpaceInstrumentation) {
    instrumentation_method = kDynamicInstrumentationMethodDefaultValue;
  }
  app_->SetDynamicInstrumentationMethod(instrumentation_method);

  app_->SetCollectMemoryInfo(settings.value(kCollectMemoryInfoSettingKey, false).toBool());
  uint64_t memory_sampling_period_ms = kMemorySamplingPeriodMsDefaultValue;
  uint64_t memory_warning_threshold_kb = kMemoryWarningThresholdKbDefaultValue;
  if (app_->GetCollectMemoryInfo()) {
    memory_sampling_period_ms = settings
                                    .value(kMemorySamplingPeriodMsSettingKey,
                                           QVariant::fromValue(kMemorySamplingPeriodMsDefaultValue))
                                    .toULongLong();
    memory_warning_threshold_kb =
        settings
            .value(kMemoryWarningThresholdKbSettingKey,
                   QVariant::fromValue(kMemoryWarningThresholdKbDefaultValue))
            .toULongLong();
  }
  app_->SetMemorySamplingPeriodMs(memory_sampling_period_ms);
  app_->SetMemoryWarningThresholdKb(memory_warning_threshold_kb);

  uint64_t max_local_marker_depth_per_command_buffer = std::numeric_limits<uint64_t>::max();
  if (settings.value(kLimitLocalMarkerDepthPerCommandBufferSettingsKey, false).toBool()) {
    max_local_marker_depth_per_command_buffer =
        settings.value(kMaxLocalMarkerDepthPerCommandBufferSettingsKey, 0).toULongLong();
  }
  app_->SetMaxLocalMarkerDepthPerCommandBuffer(max_local_marker_depth_per_command_buffer);
}

void OrbitMainWindow::on_actionCaptureOptions_triggered() {
  QSettings settings;

  orbit_qt::CaptureOptionsDialog dialog{this};
  dialog.SetEnableSampling(!app_->IsDevMode() ||
                           settings.value(kEnableCallstackSamplingSettingKey, true).toBool());
  dialog.SetSamplingPeriodMs(
      settings.value(kCallstackSamplingPeriodMsSettingKey, kCallstackSamplingPeriodMsDefaultValue)
          .toDouble());
  UnwindingMethod unwinding_method = static_cast<UnwindingMethod>(
      settings
          .value(kCallstackUnwindingMethodSettingKey,
                 static_cast<int>(kCallstackUnwindingMethodDefaultValue))
          .toInt());
  if (unwinding_method != CaptureOptions::kDwarf &&
      unwinding_method != CaptureOptions::kFramePointers) {
    unwinding_method = kCallstackUnwindingMethodDefaultValue;
  }
  dialog.SetUnwindingMethod(unwinding_method);
  dialog.SetCollectSchedulerInfo(settings.value(kCollectSchedulerInfoSettingKey, true).toBool());
  dialog.SetCollectThreadStates(settings.value(kCollectThreadStatesSettingKey, false).toBool());
  dialog.SetTraceGpuSubmissions(settings.value(kTraceGpuSubmissionsSettingKey, true).toBool());
  dialog.SetEnableApi(settings.value(kEnableApiSettingKey, true).toBool());
  dialog.SetEnableIntrospection(settings.value(kEnableIntrospectionSettingKey, true).toBool());
  DynamicInstrumentationMethod instrumentation_method = static_cast<DynamicInstrumentationMethod>(
      settings
          .value(kDynamicInstrumentationMethodSettingKey,
                 static_cast<int>(kDynamicInstrumentationMethodDefaultValue))
          .toInt());
  if (instrumentation_method != CaptureOptions::kKernelUprobes &&
      instrumentation_method != CaptureOptions::kUserSpaceInstrumentation) {
    instrumentation_method = kDynamicInstrumentationMethodDefaultValue;
  }
  dialog.SetDynamicInstrumentationMethod(instrumentation_method);
  dialog.SetCollectMemoryInfo(settings.value(kCollectMemoryInfoSettingKey, false).toBool());
  dialog.SetMemorySamplingPeriodMs(
      settings
          .value(kMemorySamplingPeriodMsSettingKey,
                 QVariant::fromValue(kMemorySamplingPeriodMsDefaultValue))
          .toULongLong());
  dialog.SetMemoryWarningThresholdKb(
      settings
          .value(kMemoryWarningThresholdKbSettingKey,
                 QVariant::fromValue(kMemoryWarningThresholdKbDefaultValue))
          .toULongLong());
  dialog.SetLimitLocalMarkerDepthPerCommandBuffer(
      settings.value(kLimitLocalMarkerDepthPerCommandBufferSettingsKey, false).toBool());
  dialog.SetMaxLocalMarkerDepthPerCommandBuffer(
      settings.value(kMaxLocalMarkerDepthPerCommandBufferSettingsKey, 0).toULongLong());

  int result = dialog.exec();
  if (result != QDialog::Accepted) {
    return;
  }

  settings.setValue(kEnableCallstackSamplingSettingKey, dialog.GetEnableSampling());
  settings.setValue(kCallstackSamplingPeriodMsSettingKey, dialog.GetSamplingPeriodMs());
  settings.setValue(kCallstackUnwindingMethodSettingKey,
                    static_cast<int>(dialog.GetUnwindingMethod()));
  settings.setValue(kCollectSchedulerInfoSettingKey, dialog.GetCollectSchedulerInfo());
  settings.setValue(kCollectThreadStatesSettingKey, dialog.GetCollectThreadStates());
  settings.setValue(kTraceGpuSubmissionsSettingKey, dialog.GetTraceGpuSubmissions());
  settings.setValue(kEnableApiSettingKey, dialog.GetEnableApi());
  settings.setValue(kEnableIntrospectionSettingKey, dialog.GetEnableIntrospection());
  settings.setValue(kDynamicInstrumentationMethodSettingKey,
                    static_cast<int>(dialog.GetDynamicInstrumentationMethod()));
  settings.setValue(kCollectMemoryInfoSettingKey, dialog.GetCollectMemoryInfo());
  settings.setValue(kMemorySamplingPeriodMsSettingKey,
                    QString::number(dialog.GetMemorySamplingPeriodMs()));
  settings.setValue(kMemoryWarningThresholdKbSettingKey,
                    QString::number(dialog.GetMemoryWarningThresholdKb()));
  settings.setValue(kLimitLocalMarkerDepthPerCommandBufferSettingsKey,
                    dialog.GetLimitLocalMarkerDepthPerCommandBuffer());
  settings.setValue(kMaxLocalMarkerDepthPerCommandBufferSettingsKey,
                    QString::number(dialog.GetMaxLocalMarkerDepthPerCommandBuffer()));
  LoadCaptureOptionsIntoApp();
}

void OrbitMainWindow::on_actionHelp_toggled(bool checked) {
  auto* capture_window = dynamic_cast<CaptureWindow*>(ui->CaptureGLWidget->GetCanvas());
  CHECK(capture_window != nullptr);
  capture_window->set_draw_help(checked);
}

void OrbitMainWindow::on_actionIntrospection_triggered() {
  if (introspection_widget_ == nullptr) {
    introspection_widget_ = std::make_unique<OrbitGLWidget>();
    introspection_widget_->setWindowFlags(Qt::WindowStaysOnTopHint);
    introspection_widget_->Initialize(GlCanvas::CanvasType::kIntrospectionWindow, this, app_.get());
    introspection_widget_->installEventFilter(this);
  }

  introspection_widget_->show();
}

void OrbitMainWindow::RestoreDefaultTabLayout() {
  for (auto& widget_and_layout : default_tab_layout_) {
    QTabWidget* tab_widget = widget_and_layout.first;
    tab_widget->clear();
    for (auto& tab_and_title : widget_and_layout.second.tabs_and_titles) {
      tab_widget->addTab(tab_and_title.first, tab_and_title.second);
    }
    tab_widget->setCurrentIndex(widget_and_layout.second.current_index);
  }

  UpdateCaptureStateDependentWidgets();
}

void OrbitMainWindow::OnTimerSelectionChanged(const orbit_client_protos::TimerInfo* timer_info) {
  std::optional<int> selected_row(std::nullopt);
  if (timer_info) {
    uint64_t function_id = timer_info->function_id();
    const auto live_functions_controller = ui->liveFunctions->GetLiveFunctionsController();
    CHECK(live_functions_controller.has_value());
    orbit_data_views::LiveFunctionsDataView& live_functions_data_view =
        live_functions_controller.value()->GetDataView();
    selected_row = live_functions_data_view.GetRowFromFunctionId(function_id);
    live_functions_data_view.UpdateSelectedFunctionId();
  }
  ui->liveFunctions->OnRowSelected(selected_row);
}

void OrbitMainWindow::on_actionOpen_Capture_triggered() {
  QString file = QFileDialog::getOpenFileName(
      this, "Open capture...",
      QString::fromStdString(orbit_paths::CreateOrGetCaptureDir().string()), "*.orbit");
  if (file.isEmpty()) {
    return;
  }

  QString orbit_executable =
      QString::fromStdString(orbit_base::GetExecutablePath().generic_string());
  QStringList arguments;
  QProcess::startDetached(orbit_executable, arguments << file << command_line_flags_);
}

void OrbitMainWindow::on_actionRename_Capture_File_triggered() {
  CHECK(target_label_->GetFilePath().has_value());
  const std::filesystem::path& current_file_path = target_label_->GetFilePath().value();
  QString file_path =
      QFileDialog::getSaveFileName(this, "Rename or Move capture...",
                                   QString::fromStdString(current_file_path.string()), "*.orbit");

  std::filesystem::path new_file_path{file_path.toStdString()};

  if (new_file_path == current_file_path) return;

  auto* progress_dialog = new QProgressDialog(
      QString("Moving file to \"%1\"...").arg(QString::fromStdString(new_file_path.string())), "",
      0, 0, this);
  progress_dialog->setWindowModality(Qt::WindowModal);
  progress_dialog->show();

  orbit_base::Future<ErrorMessageOr<void>> rename_future =
      app_->MoveCaptureFile(current_file_path, new_file_path);

  rename_future.Then(main_thread_executor_.get(), [this, progress_dialog, current_file_path,
                                                   new_file_path](ErrorMessageOr<void> result) {
    progress_dialog->close();
    if (result.has_error()) {
      QMessageBox::critical(
          this, QString::fromStdString("Unable to Rename File"),
          QString::fromStdString(absl::StrFormat(R"(Unable to rename/move file "%s" -> "%s": %s)",
                                                 current_file_path.string(), new_file_path.string(),
                                                 result.error().message())));
      return;
    }

    UpdateFilePath(new_file_path);
  });
}

void OrbitMainWindow::OpenCapture(const std::string& filepath) {
  auto* loading_capture_dialog =
      new QProgressDialog("Waiting for the capture to be loaded...", nullptr, 0, 0, this, Qt::Tool);
  loading_capture_dialog->setWindowTitle("Loading capture");
  loading_capture_dialog->setModal(true);
  loading_capture_dialog->setWindowFlags(
      (loading_capture_dialog->windowFlags() | Qt::CustomizeWindowHint) &
      ~Qt::WindowCloseButtonHint & ~Qt::WindowSystemMenuHint);
  loading_capture_dialog->setFixedSize(loading_capture_dialog->size());

  auto loading_capture_cancel_button = QPointer<QPushButton>{new QPushButton{this}};
  loading_capture_cancel_button->setText("Cancel");
  QObject::connect(loading_capture_dialog, &QProgressDialog::canceled, this,
                   [this]() { app_->OnLoadCaptureCancelRequested(); });
  loading_capture_dialog->setCancelButton(loading_capture_cancel_button);
  loading_capture_dialog->show();

  app_->LoadCaptureFromFile(filepath).Then(
      main_thread_executor_.get(),
      [this, loading_capture_dialog](ErrorMessageOr<CaptureListener::CaptureOutcome> result) {
        loading_capture_dialog->close();
        if (!result.has_value()) {
          QMessageBox::critical(this, "Error while loading capture",
                                QString::fromStdString(result.error().message()));
          Exit(kEndSessionReturnCode);
          return;
        }

        switch (result.value()) {
          case CaptureListener::CaptureOutcome::kCancelled:
            Exit(kEndSessionReturnCode);
            return;
          case CaptureListener::CaptureOutcome::kComplete:
            UpdateCaptureStateDependentWidgets();
            return;
        }
      });

  setWindowTitle(QString::fromStdString(filepath));
  UpdateCaptureStateDependentWidgets();
  FindParentTabWidget(ui->CaptureTab)->setCurrentWidget(ui->CaptureTab);
}

void OrbitMainWindow::on_actionCheckFalse_triggered() { CHECK(false); }

void InfiniteRecursion(int num) {
  if (num != 1) {
    InfiniteRecursion(num);
  }

  LOG("num=%d", num);
}

void OrbitMainWindow::on_actionStackOverflow_triggered() { InfiniteRecursion(0); }

void OrbitMainWindow::on_actionServiceCheckFalse_triggered() {
  app_->CrashOrbitService(CrashOrbitServiceRequest_CrashType_CHECK_FALSE);
}

void OrbitMainWindow::on_actionServiceStackOverflow_triggered() {
  app_->CrashOrbitService(CrashOrbitServiceRequest_CrashType_STACK_OVERFLOW);
}

void OrbitMainWindow::on_actionSourcePathMappings_triggered() {
  orbit_source_paths_mapping::MappingManager manager{};

  orbit_config_widgets::SourcePathsMappingDialog dialog{this};
  dialog.SetMappings(manager.GetMappings());
  const int result_code = dialog.exec();

  if (result_code == QDialog::Accepted) {
    manager.SetMappings(dialog.GetMappings());
  }
}

void OrbitMainWindow::on_actionSymbolsDialog_triggered() {
  orbit_config_widgets::SymbolsDialog dialog{this};
  dialog.SetSymbolPaths(orbit_symbol_paths::LoadPaths());
  const int result_code = dialog.exec();

  if (result_code == QDialog::Accepted) {
    orbit_symbol_paths::SavePaths(dialog.GetSymbolPaths());
  }
}

void OrbitMainWindow::OnCaptureCleared() {
  ui->liveFunctions->Reset();
  UpdateCaptureStateDependentWidgets();
  ui->captureLogTextEdit->clear();
}

bool OrbitMainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == ui->MainTabWidget->tabBar() || watched == ui->RightTabWidget->tabBar()) {
    if (event->type() == QEvent::MouseButtonRelease) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::MouseButton::RightButton) {
        int index = static_cast<QTabBar*>(watched)->tabAt(mouse_event->pos());
        if (index >= 0) {
          auto* tab_widget = static_cast<QTabWidget*>(watched->parent());
          if (tab_widget->isTabEnabled(index)) {
            tab_widget->setCurrentIndex(index);
          }
          CreateTabBarContextMenu(tab_widget, index, mouse_event->globalPos());
        }
      }
    }
  } else if (watched == introspection_widget_.get()) {
    if (event->type() == QEvent::Close) {
      app_->StopIntrospection();
    }
  }

  return QObject::eventFilter(watched, event);
}

bool OrbitMainWindow::ConfirmExit() {
  if (app_->IsCapturing() || app_->IsLoadingCapture()) {
    return QMessageBox::question(this, "Capture in progress",
                                 "A capture is currently in progress. Do you want to abort the "
                                 "capture and exit Orbit?") == QMessageBox::Yes;
  }

  return true;
}

void OrbitMainWindow::Exit(int return_code) {
  SaveMainWindowGeometry();

  if (app_->IsCapturing() || app_->IsLoadingCapture()) {
    // We need for the capture to clean up - exit as soon as this is done
    app_->SetCaptureFailedCallback([this, return_code] { Exit(return_code); });
    app_->AbortCapture();
  }

  if (main_thread_executor_ != nullptr) {
    main_thread_executor_->AbortWaitingJobs();
  }
  if (introspection_widget_ != nullptr) {
    introspection_widget_->close();
  }

  metrics_uploader_->SendLogEvent(orbit_metrics_uploader::OrbitLogEvent::ORBIT_MAIN_WINDOW_CLOSE);

  QApplication::exit(return_code);
}

void OrbitMainWindow::closeEvent(QCloseEvent* event) {
  if (!ConfirmExit()) {
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
  Exit(kQuitOrbitReturnCode);
}

void OrbitMainWindow::OnStadiaConnectionError(std::error_code error) {
  CHECK(std::holds_alternative<StadiaTarget>(target_configuration_));
  const StadiaTarget& target = std::get<StadiaTarget>(target_configuration_);

  target.GetProcessManager()->SetProcessListUpdateListener(nullptr);

  is_connected_ = false;
  target_process_state_ = TargetProcessState::kEnded;
  UpdateProcessConnectionStateDependentWidgets();

  QString error_message = QString("The connection to instance \"%1\" failed with error message: %2")
                              .arg(target.GetConnection()->GetInstance().display_name)
                              .arg(QString::fromStdString(error.message()));

  target_label_->SetConnectionDead(error_message);

  QMessageBox::critical(this, "Connection error", error_message, QMessageBox::Ok);
}

void OrbitMainWindow::SetTarget(const StadiaTarget& target) {
  const orbit_session_setup::StadiaConnection* connection = target.GetConnection();
  ServiceDeployManager* service_deploy_manager = connection->GetServiceDeployManager();
  app_->SetSecureCopyCallback([service_deploy_manager](std::string_view source,
                                                       std::string_view destination) {
    CHECK(service_deploy_manager != nullptr);
    return service_deploy_manager->CopyFileToLocal(std::string{source}, std::string{destination});
  });

  QObject::connect(service_deploy_manager, &ServiceDeployManager::socketErrorOccurred, this,
                   &OrbitMainWindow::OnStadiaConnectionError, Qt::UniqueConnection);

  app_->SetGrpcChannel(connection->GetGrpcChannel());
  app_->SetProcessManager(target.GetProcessManager());
  app_->SetTargetProcess(target.GetProcess());

  target_label_->ChangeToStadiaTarget(target);

  using ProcessInfo = orbit_grpc_protos::ProcessInfo;
  target.GetProcessManager()->SetProcessListUpdateListener([&](std::vector<ProcessInfo> processes) {
    // This lambda is called from a background-thread, so we use QMetaObject::invokeMethod
    // to execute our logic on the main thread.
    QMetaObject::invokeMethod(
        this, [&, processes = std::move(processes)]() { OnProcessListUpdated(processes); });
  });

  is_connected_ = true;
}

void OrbitMainWindow::SetTarget(const LocalTarget& target) {
  const orbit_session_setup::LocalConnection* connection = target.GetConnection();
  app_->SetGrpcChannel(connection->GetGrpcChannel());
  app_->SetProcessManager(target.GetProcessManager());
  app_->SetTargetProcess(target.GetProcess());

  target_label_->ChangeToLocalTarget(target);

  using ProcessInfo = orbit_grpc_protos::ProcessInfo;
  target.GetProcessManager()->SetProcessListUpdateListener([&](std::vector<ProcessInfo> processes) {
    // This lambda is called from a background-thread, so we use QMetaObject::invokeMethod
    // to execute our logic on the main thread.
    QMetaObject::invokeMethod(
        this, [&, processes = std::move(processes)]() { OnProcessListUpdated(processes); });
  });

  is_connected_ = true;
}

void OrbitMainWindow::SetTarget(const orbit_session_setup::FileTarget& target) {
  target_label_->ChangeToFileTarget(target);
  OpenCapture(target.GetCaptureFilePath().string());
}

void OrbitMainWindow::OnProcessListUpdated(
    const std::vector<orbit_grpc_protos::ProcessInfo>& processes) {
  const auto is_current_process = [this](const auto& process) {
    const orbit_client_data::ProcessData* const target_process = app_->GetTargetProcess();
    return target_process != nullptr && process.pid() == app_->GetTargetProcess()->pid();
  };
  const auto current_process = std::find_if(processes.begin(), processes.end(), is_current_process);
  const bool process_ended = current_process == processes.end();

  if (process_ended) {
    target_process_state_ = TargetProcessState::kEnded;
    target_label_->SetProcessEnded();
  } else {
    target_process_state_ = TargetProcessState::kRunning;
    target_label_->SetProcessCpuUsageInPercent(current_process->cpu_usage());
  }
  UpdateProcessConnectionStateDependentWidgets();
}

TargetConfiguration OrbitMainWindow::ClearTargetConfiguration() {
  if (std::holds_alternative<StadiaTarget>(target_configuration_)) {
    std::get<StadiaTarget>(target_configuration_)
        .GetProcessManager()
        ->SetProcessListUpdateListener(nullptr);
  } else if (std::holds_alternative<LocalTarget>(target_configuration_)) {
    std::get<LocalTarget>(target_configuration_)
        .GetProcessManager()
        ->SetProcessListUpdateListener(nullptr);
  }
  return std::move(target_configuration_);
}

void OrbitMainWindow::ShowTooltip(std::string_view message) {
  QToolTip::showText(QCursor::pos(),
                     QString::fromUtf8(message.data(), static_cast<int>(message.size())), this);
}

void OrbitMainWindow::ShowWarningWithDontShowAgainCheckboxIfNeeded(
    std::string_view title, std::string_view text, std::string_view dont_show_again_setting_key) {
  QSettings settings;
  QString setting_key = QString::fromStdString(std::string{dont_show_again_setting_key});
  if (settings.value(setting_key, false).toBool()) {
    return;
  }

  QMessageBox message_box{QMessageBox::Icon::Warning, QString::fromStdString(std::string{title}),
                          QString::fromStdString(std::string{text}),
                          QMessageBox::StandardButton::Ok, this};

  QCheckBox check_box("Don't show this message again.");
  message_box.setCheckBox(&check_box);
  QObject::connect(&check_box, &QCheckBox::stateChanged, [&settings, &setting_key](int state) {
    settings.setValue(setting_key, static_cast<bool>(state));
  });

  message_box.exec();
}

static std::optional<QString> TryApplyMappingAndReadSourceFile(
    const std::filesystem::path& file_path) {
  orbit_source_paths_mapping::MappingManager mapping_manager{};
  const auto maybe_mapping_file_path = mapping_manager.MapToFirstExistingTarget(file_path);
  if (maybe_mapping_file_path.has_value()) {
    ErrorMessageOr<std::string> result = orbit_base::ReadFileToString(*maybe_mapping_file_path);
    if (result.has_error()) return std::nullopt;
    return QString::fromStdString(result.value());
  }

  return std::nullopt;
}

std::optional<QString> OrbitMainWindow::LoadSourceCode(const std::filesystem::path& file_path) {
  {
    ErrorMessageOr<std::string> source_code_or_error = orbit_base::ReadFileToString(file_path);
    if (source_code_or_error.has_value()) {
      return QString::fromStdString(source_code_or_error.value());
    }
  }

  {
    std::optional<QString> maybe_source_code = TryApplyMappingAndReadSourceFile(file_path);
    if (maybe_source_code.has_value()) return maybe_source_code.value();
  }

  {
    std::optional<orbit_source_paths_mapping_ui::UserAnswers> maybe_user_answers =
        orbit_source_paths_mapping_ui::AskUserForSourceFilePath(this, file_path);
    if (!maybe_user_answers.has_value()) return std::nullopt;

    ErrorMessageOr<std::string> file_contents_or_error =
        orbit_base::ReadFileToString(maybe_user_answers->local_file_path);

    if (file_contents_or_error.has_error()) {
      QMessageBox::critical(this, "Could not open source file",
                            QString::fromStdString(file_contents_or_error.error().message()));
      return std::nullopt;
    }

    if (maybe_user_answers->infer_source_paths_mapping) {
      orbit_source_paths_mapping::InferAndAppendSourcePathsMapping(
          file_path, maybe_user_answers->local_file_path);
    }

    return QString::fromStdString(file_contents_or_error.value());
  }

  return std::nullopt;
}

void OrbitMainWindow::ShowSourceCode(
    const std::filesystem::path& file_path, size_t line_number,
    std::optional<std::unique_ptr<orbit_code_report::CodeReport>> maybe_code_report) {
  auto code_viewer_dialog = std::make_unique<orbit_code_viewer::OwningDialog>();

  code_viewer_dialog->SetLineNumberTypes(
      orbit_code_viewer::Dialog::LineNumberTypes::kOnlyMainContent);
  code_viewer_dialog->SetHighlightCurrentLine(true);
  code_viewer_dialog->setWindowTitle(QString::fromStdString(file_path.filename().string()));

  const auto source_code = LoadSourceCode(file_path.lexically_normal());

  if (!source_code.has_value()) return;

  auto syntax_highlighter = std::make_unique<orbit_syntax_highlighter::Cpp>();
  code_viewer_dialog->SetMainContent(source_code.value(), std::move(syntax_highlighter));
  constexpr orbit_code_viewer::FontSizeInEm kHeatmapAreaWidth{1.3f};

  if (maybe_code_report.has_value()) {
    CHECK(maybe_code_report->get() != nullptr);
    code_viewer_dialog->SetEnableSampleCounters(true);
    code_viewer_dialog->SetOwningHeatmap(kHeatmapAreaWidth, std::move(*maybe_code_report));
  }

  // This ensure the dialog will be closed at the latest when the session ends.
  QObject::connect(this, &QObject::destroyed, code_viewer_dialog.get(), &QDialog::close);

  code_viewer_dialog->GoToLineNumber(line_number);
  orbit_code_viewer::OpenAndDeleteOnClose(std::move(code_viewer_dialog));
}

void OrbitMainWindow::ShowDisassembly(const orbit_client_protos::FunctionInfo& function_info,
                                      const std::string& assembly,
                                      orbit_code_report::DisassemblyReport report) {
  auto dialog = std::make_unique<orbit_qt::AnnotatingSourceCodeDialog>();
  dialog->setWindowTitle("Orbit Disassembly");
  dialog->SetLineNumberTypes(orbit_code_viewer::Dialog::LineNumberTypes::kOnlyAnnotatingLines);
  dialog->SetHighlightCurrentLine(true);

  auto syntax_highlighter = std::make_unique<orbit_syntax_highlighter::X86Assembly>();
  dialog->SetMainContent(QString::fromStdString(assembly), std::move(syntax_highlighter));
  uint32_t num_samples = report.GetNumSamples();
  dialog->SetDisassemblyCodeReport(std::move(report));

  if (num_samples > 0) {
    constexpr orbit_code_viewer::FontSizeInEm kHeatmapAreaWidth{1.3f};
    dialog->EnableHeatmap(kHeatmapAreaWidth);
    dialog->SetEnableSampleCounters(true);
  }

  // This ensure the dialog will be closed at the latest when the session ends.
  QObject::connect(this, &QObject::destroyed, dialog.get(), &QDialog::close);

  QPointer<orbit_qt::AnnotatingSourceCodeDialog> dialog_ptr =
      OpenAndDeleteOnClose(std::move(dialog));

  dialog_ptr->AddAnnotatingSourceCode(
      function_info, [this](const std::string& module_path, const std::string& build_id) {
        return app_->RetrieveModuleWithDebugInfo(module_path, build_id);
      });
}

void OrbitMainWindow::AppendToCaptureLog(CaptureLogSeverity severity, absl::Duration capture_time,
                                         std::string_view message) {
  QColor message_color;
  std::string severity_name;
  switch (severity) {
    case CaptureLogSeverity::kInfo:
      message_color = Qt::white;
      severity_name = "kInfo";
      break;
    case CaptureLogSeverity::kWarning:
      message_color = Qt::yellow;
      severity_name = "kWarning";
      break;
    case CaptureLogSeverity::kSevereWarning:
      message_color = QColor{255, 128, 0};
      severity_name = "kSevereWarning";
      break;
    case CaptureLogSeverity::kError:
      message_color = Qt::darkRed;
      severity_name = "kError";
      break;
  }
  ui->captureLogTextEdit->setTextColor(message_color);
  std::string pretty_time = orbit_display_formats::GetDisplayTime(capture_time);
  ui->captureLogTextEdit->append(
      QString::fromStdString(absl::StrFormat("%s\t%s", pretty_time, message)));
  LOG("\"%s  %s\" with severity %s added to the capture log", pretty_time, message, severity_name);
}
