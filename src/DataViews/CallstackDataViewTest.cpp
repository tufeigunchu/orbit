// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ClientData/CaptureData.h"
#include "ClientData/FunctionUtils.h"
#include "ClientData/ProcessData.h"
#include "ClientProtos/capture_data.pb.h"
#include "DataViewTestUtils.h"
#include "DataViews/CallstackDataView.h"
#include "DataViews/DataView.h"
#include "DisplayFormats/DisplayFormats.h"
#include "GrpcProtos/capture.pb.h"
#include "MockAppInterface.h"
#include "OrbitBase/File.h"
#include "OrbitBase/ReadFileToString.h"
#include "OrbitBase/TemporaryFile.h"
#include "TestUtils/TestUtils.h"

using orbit_client_data::CaptureData;
using orbit_client_data::ModuleData;
using orbit_client_data::ProcessData;
using orbit_client_data::function_utils::GetLoadedModuleNameByPath;
using orbit_client_protos::CallstackInfo;
using orbit_client_protos::FunctionInfo;
using orbit_data_views::CallstackDataView;
using orbit_data_views::CheckCopySelectionIsInvoked;
using orbit_data_views::CheckExportToCsvIsInvoked;
using orbit_data_views::ContextMenuEntry;
using orbit_data_views::FlattenContextMenuWithGrouping;
using orbit_data_views::kMenuActionCopySelection;
using orbit_data_views::kMenuActionDisassembly;
using orbit_data_views::kMenuActionExportToCsv;
using orbit_data_views::kMenuActionLoadSymbols;
using orbit_data_views::kMenuActionSelect;
using orbit_data_views::kMenuActionSourceCode;
using orbit_data_views::kMenuActionUnselect;
using orbit_data_views::MockAppInterface;
using orbit_grpc_protos::ModuleInfo;

namespace {

constexpr int kColumnSelected = 0;
constexpr int kColumnName = 1;
constexpr int kColumnSize = 2;
constexpr int kColumnModule = 3;
constexpr int kColumnAddress = 4;

constexpr size_t kNumFunctions = 4;
const std::array<std::string, kNumFunctions> kFunctionNames{"foo", "main", "ffind", "bar"};
const std::array<std::string, kNumFunctions> kFunctionPrettyNames{"void foo()", "main(int, char**)",
                                                                  "ffind(int)", "bar(const char*)"};
constexpr std::array<uint64_t, kNumFunctions> kFunctionAddresses{0x5100, 0x7250, 0x6700, 0x4450};
constexpr std::array<uint64_t, kNumFunctions> kFunctionSizes{0x50, 0x70, 0x60, 0x40};

constexpr size_t kNumModules = 5;
constexpr std::array<uint64_t, kNumModules> kModuleIsLoaded{true, true, true, true, false};
const std::array<std::string, kNumModules> kModuleNames{"foo_module", "some_module", "ffind_module",
                                                        "bar_module", "not_loaded_module"};
const std::array<std::string, kNumModules> kModulePaths{
    "/path/to/foomodule", "/path/to/somemodule", "/path/to/ffindmodule", "/path/to/barmodule",
    "/path/to/notloadedmodule"};
const std::array<std::string, kNumModules> kModuleBuildIds{"build_id_0", "build_id_1", "build_id_2",
                                                           "build_id_3", "build_id_4"};
constexpr std::array<uint64_t, kNumModules> kModuleStartAddresses{0x3000, 0x9000, 0x7000, 0x5000,
                                                                  0x2000};
constexpr std::array<uint64_t, kNumModules> kModuleEndAddresses{0x3900, 0x9500, 0x8900, 0x5500,
                                                                0x2700};
constexpr std::array<uint64_t, kNumModules> kModuleExecutableSegmentOffsets{0x123, 0x234, 0x135,
                                                                            0x246, 0x150};
constexpr std::array<uint64_t, kNumModules> kModuleLoadBiases{0x5000, 0x7000, 0x6000, 0x4000,
                                                              0x3000};

std::string GetExpectedDisplaySize(uint64_t size) { return absl::StrFormat("%lu", size); }

std::string GetExpectedDisplayAddress(uint64_t address) {
  return absl::StrFormat("%#llx", address);
}

std::unique_ptr<CaptureData> GenerateTestCaptureData(
    orbit_client_data::ModuleManager* module_manager) {
  std::vector<ModuleInfo> modules;

  for (size_t i = 0; i < kNumModules; i++) {
    ModuleInfo module_info{};
    module_info.set_name(kModuleNames[i]);
    module_info.set_file_path(kModulePaths[i]);
    module_info.set_build_id(kModuleBuildIds[i]);
    module_info.set_address_start(kModuleStartAddresses[i]);
    module_info.set_address_end(kModuleEndAddresses[i]);
    module_info.set_executable_segment_offset(kModuleExecutableSegmentOffsets[i]);
    module_info.set_load_bias(kModuleLoadBiases[i]);
    (void)module_manager->AddOrUpdateModules({module_info});

    modules.push_back(module_info);

    if (kModuleIsLoaded[i]) {
      orbit_grpc_protos::SymbolInfo symbol_info;
      symbol_info.set_name(kFunctionNames[i]);
      symbol_info.set_demangled_name(kFunctionPrettyNames[i]);
      symbol_info.set_address(kFunctionAddresses[i]);
      symbol_info.set_size(kFunctionSizes[i]);

      orbit_grpc_protos::ModuleSymbols module_symbols;
      module_symbols.set_load_bias(kModuleLoadBiases[i]);
      module_symbols.set_symbols_file_path(kModulePaths[i]);
      module_symbols.mutable_symbol_infos()->Add(std::move(symbol_info));

      orbit_client_data::ModuleData* module_data =
          module_manager->GetMutableModuleByPathAndBuildId(kModulePaths[i], kModuleBuildIds[i]);
      module_data->AddSymbols(module_symbols);
    }
  }

  constexpr int32_t kProcessId = 42;
  const std::string kExecutablePath = "/path/to/text.exe";
  orbit_grpc_protos::CaptureStarted capture_started{};
  capture_started.set_process_id(kProcessId);
  capture_started.set_executable_path(kExecutablePath);

  auto capture_data = std::make_unique<CaptureData>(module_manager, capture_started, std::nullopt,
                                                    absl::flat_hash_set<uint64_t>{},
                                                    CaptureData::DataSource::kLiveCapture);
  ProcessData* process = capture_data.get()->mutable_process();
  process->UpdateModuleInfos(modules);

  return capture_data;
}

class CallstackDataViewTest : public testing::Test {
 public:
  explicit CallstackDataViewTest()
      : view_{&app_}, capture_data_(GenerateTestCaptureData(&module_manager_)) {}

  void SetCallstackFromFrames(std::vector<uint64_t> callstack_frames) {
    orbit_client_protos::CallstackInfo callstack_info;
    *callstack_info.mutable_frames() = {callstack_frames.begin(), callstack_frames.end()};
    callstack_info.set_type(orbit_client_protos::CallstackInfo_CallstackType_kComplete);
    view_.SetCallstack(callstack_info);
  }

  std::string GetModulePathByAddressFromCaptureData(uint64_t frame_address) {
    return std::filesystem::path(capture_data_.get()->GetModulePathByAddress(frame_address))
        .filename()
        .string();
  }

  std::string GetFunctionFallbackNameByAddress(uint64_t frame_address) {
    return capture_data_.get()->GetFunctionNameByAddress(frame_address);
  }

 protected:
  MockAppInterface app_;
  CallstackDataView view_;
  orbit_client_data::ModuleManager module_manager_;
  std::unique_ptr<CaptureData> capture_data_;
};

}  // namespace

TEST_F(CallstackDataViewTest, ColumnHeadersNotEmpty) {
  EXPECT_GE(view_.GetColumns().size(), 1);
  for (const auto& column : view_.GetColumns()) {
    EXPECT_FALSE(column.header.empty());
  }
}

TEST_F(CallstackDataViewTest, HasValidDefaultSortingColumn) {
  EXPECT_GE(view_.GetDefaultSortingColumn(), kColumnAddress);
  EXPECT_LT(view_.GetDefaultSortingColumn(), view_.GetColumns().size());
}

TEST_F(CallstackDataViewTest, ColumnValuesAreCorrect) {
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, GetMutableCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));

  // Test the case that ProcessData::FindModuleByAddress has no finding for input frame address.
  // In this case, both frame.module and frame.function are nullptr.
  {
    constexpr uint64_t kNoFindingInProcessData = 0x2000;
    SetCallstackFromFrames({kNoFindingInProcessData});

    EXPECT_EQ(view_.GetValue(0, kColumnName),
              absl::StrCat(view_.kHighlightedFunctionBlankString,
                           GetFunctionFallbackNameByAddress(kNoFindingInProcessData)));
    EXPECT_EQ(view_.GetValue(0, kColumnSize), "");
    EXPECT_EQ(view_.GetValue(0, kColumnModule),
              GetModulePathByAddressFromCaptureData(kNoFindingInProcessData));
    EXPECT_EQ(view_.GetValue(0, kColumnAddress),
              GetExpectedDisplayAddress(kNoFindingInProcessData));
  }

  // Test the case that ProcessData::FindModuleByAddress has finding but
  // ModuleManager::GetModuleByModuleInMemoryAndAbsoluteAddress has no finding for the input frame
  // address. In this case, both frame.module and frame.function are nullptr.
  {
    constexpr uint64_t kNoFindingInModuleManager = 0x3100;
    SetCallstackFromFrames({kNoFindingInModuleManager});

    EXPECT_EQ(view_.GetValue(0, kColumnName),
              absl::StrCat(view_.kHighlightedFunctionBlankString,
                           GetFunctionFallbackNameByAddress(kNoFindingInModuleManager)));
    EXPECT_EQ(view_.GetValue(0, kColumnSize), "");
    EXPECT_EQ(view_.GetValue(0, kColumnModule),
              GetModulePathByAddressFromCaptureData(kNoFindingInModuleManager));
    EXPECT_EQ(view_.GetValue(0, kColumnAddress),
              GetExpectedDisplayAddress(kNoFindingInModuleManager));
  }

  // Test the case that both ProcessData::FindModuleByAddress and
  // ModuleManager::GetModuleByModuleInMemoryAndAbsoluteAddress have finding but
  // ModuleData::FindFunctionByOffset has no finding. In this case, frame.module is not nullptr but
  // frame.function is nullptr.
  {
    constexpr uint64_t kNoFindingInModuleData = 0x3200;
    SetCallstackFromFrames({kNoFindingInModuleData});

    EXPECT_EQ(view_.GetValue(0, kColumnName),
              absl::StrCat(view_.kHighlightedFunctionBlankString,
                           GetFunctionFallbackNameByAddress(kNoFindingInModuleData)));
    EXPECT_EQ(view_.GetValue(0, kColumnSize), "");
    EXPECT_EQ(view_.GetValue(0, kColumnModule), kModuleNames[0]);
    EXPECT_EQ(view_.GetValue(0, kColumnAddress), GetExpectedDisplayAddress(kNoFindingInModuleData));
  }

  // Test the case that ProcessData::FindModuleByAddress,
  // ModuleManager::GetModuleByModuleInMemoryAndAbsoluteAddress, and
  // ModuleData::FindFunctionByOffset all have finding. In this case, both frame.module and
  // frame.function are not nullptr.
  {
    constexpr uint64_t kAllHaveFindings = 0x3140;
    SetCallstackFromFrames({kAllHaveFindings});

    EXPECT_EQ(view_.GetValue(0, kColumnName),
              absl::StrCat(view_.kHighlightedFunctionBlankString, kFunctionPrettyNames[0]));
    EXPECT_EQ(view_.GetValue(0, kColumnSize), GetExpectedDisplaySize(kFunctionSizes[0]));
    EXPECT_EQ(view_.GetValue(0, kColumnModule), GetLoadedModuleNameByPath(kModulePaths[0]));
    EXPECT_EQ(view_.GetValue(0, kColumnAddress), GetExpectedDisplayAddress(kAllHaveFindings));

    constexpr uint64_t kSymbolAddress =
        kFunctionAddresses[0] + kModuleStartAddresses[0] - kModuleLoadBiases[0];
    view_.SetFunctionsToHighlight({kSymbolAddress});
    EXPECT_EQ(view_.GetValue(0, kColumnName),
              absl::StrCat(view_.kHighlightedFunctionString, kFunctionPrettyNames[0]));
  }
}

TEST_F(CallstackDataViewTest, ColumnSelectedShowsRightResults) {
  bool function_selected;
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetMutableCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, IsFunctionSelected(testing::A<const orbit_client_protos::FunctionInfo&>()))
      .WillRepeatedly(testing::ReturnPointee(&function_selected));

  // Test the case that frame.function == nullptr.
  {
    constexpr uint64_t kNoFindingInModuleManager = 0x3100;
    SetCallstackFromFrames({kNoFindingInModuleManager});

    function_selected = false;
    EXPECT_EQ(view_.GetValue(0, kColumnSelected), "");

    function_selected = true;
    EXPECT_EQ(view_.GetValue(0, kColumnSelected), "");
  }

  // Test the case that frame.function != nullptr.
  {
    constexpr uint64_t kAllHaveFindings = 0x3140;
    SetCallstackFromFrames({kAllHaveFindings});

    function_selected = false;
    EXPECT_EQ(view_.GetValue(0, kColumnSelected), "");

    function_selected = true;
    EXPECT_EQ(view_.GetValue(0, kColumnSelected), "✓");
  }
}

TEST_F(CallstackDataViewTest, ContextMenuEntriesArePresentCorrectly) {
  const std::vector<uint64_t> kCallstackFrameAddresses{
      // Corresponding CallstackDataViewFrame: frame.module             frame.function
      0x3140,  //                                module 0 (loaded)        function 0 (selected)
      0x9260,  //                                module 1 (loaded)        function 1 (not selected)
      0x6900,  //                                nullptr                  nullptr
      0x5250,  //                                module 3 (loaded)        nullptr
      0x2200,  //                                module 4 (not loaded)    nullptr
  };
  const std::vector<bool> kFrameModuleNotNull{true, true, false, true, true};
  const std::vector<bool> kFrameFunctionNotNull{true, true, false, false, false};

  bool capture_connected;
  std::vector<bool> functions_selected{true, false, true, true, false};

  auto get_index_from_function_info = [&](const FunctionInfo& function) -> std::optional<size_t> {
    for (size_t i = 0; i < kNumFunctions; i++) {
      if (kFunctionNames[i] == function.name()) return i;
    }
    return std::nullopt;
  };

  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, GetMutableCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, IsCaptureConnected).WillRepeatedly(testing::ReturnPointee(&capture_connected));
  EXPECT_CALL(app_, IsFunctionSelected(testing::A<const orbit_client_protos::FunctionInfo&>()))
      .WillRepeatedly([&](const FunctionInfo& function) -> bool {
        std::optional<size_t> index = get_index_from_function_info(function);
        EXPECT_TRUE(index.has_value());
        return functions_selected.at(index.value());
      });

  auto verify_context_menu_action_availability = [&](std::vector<int> selected_indices) {
    std::vector<std::string> context_menu =
        FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(0, selected_indices));

    // Common actions should always be available.
    CheckSingleAction(context_menu, kMenuActionCopySelection, ContextMenuEntry::kEnabled);
    CheckSingleAction(context_menu, kMenuActionExportToCsv, ContextMenuEntry::kEnabled);

    ContextMenuEntry source_code_or_disassembly = ContextMenuEntry::kDisabled;
    ContextMenuEntry load_symbols = ContextMenuEntry::kDisabled;
    ContextMenuEntry select = ContextMenuEntry::kDisabled;
    ContextMenuEntry unselect = ContextMenuEntry::kDisabled;
    for (int selected_index : selected_indices) {
      if (kFrameFunctionNotNull[selected_index] && capture_connected) {
        // Source code and disassembly actions are availble if and only if: 1) capture is connected
        // and 2) there exists a function that is not null.
        source_code_or_disassembly = ContextMenuEntry::kEnabled;

        // Hook action is availble if and only if: 1) capture is connected
        // and 2) there exists a function that is not null and also not yet selected.
        // Unhook action is availble if and only if: 1) capture is connected
        // and 2) there exists a function that is not null and also already selected.
        if (!functions_selected[selected_index]) {
          select = ContextMenuEntry::kEnabled;
        } else {
          unselect = ContextMenuEntry::kEnabled;
        }
      } else if (kFrameModuleNotNull[selected_index] && !kModuleIsLoaded[selected_index]) {
        // Load symbols action is available if and only if existing a module that is not null and
        // not yet loaded.
        load_symbols = ContextMenuEntry::kEnabled;
      }
    }
    CheckSingleAction(context_menu, kMenuActionDisassembly, source_code_or_disassembly);
    CheckSingleAction(context_menu, kMenuActionSourceCode, source_code_or_disassembly);
    CheckSingleAction(context_menu, kMenuActionLoadSymbols, load_symbols);
    CheckSingleAction(context_menu, kMenuActionSelect, select);
    CheckSingleAction(context_menu, kMenuActionUnselect, unselect);
  };

  SetCallstackFromFrames(kCallstackFrameAddresses);

  capture_connected = false;
  verify_context_menu_action_availability({0});
  verify_context_menu_action_availability({1});
  verify_context_menu_action_availability({2});
  verify_context_menu_action_availability({3});
  verify_context_menu_action_availability({4});
  verify_context_menu_action_availability({0, 1, 2, 3, 4});

  capture_connected = true;
  verify_context_menu_action_availability({0});
  verify_context_menu_action_availability({1});
  verify_context_menu_action_availability({2});
  verify_context_menu_action_availability({3});
  verify_context_menu_action_availability({4});
  verify_context_menu_action_availability({0, 1, 2, 3, 4});
}

TEST_F(CallstackDataViewTest, ContextMenuActionsAreInvoked) {
  bool function_selected = false;

  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, GetMutableCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, IsCaptureConnected).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, IsFunctionSelected(testing::A<const orbit_client_protos::FunctionInfo&>()))
      .WillRepeatedly(testing::ReturnPointee(&function_selected));

  constexpr uint64_t kFrameAddress = 0x3140;
  SetCallstackFromFrames({kFrameAddress});
  std::vector<std::string> context_menu =
      FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(0, {0}));
  ASSERT_FALSE(context_menu.empty());

  // Copy Selection
  {
    std::string expected_clipboard = absl::StrFormat(
        "Hooked\tFunction\tSize\tModule\tSampled Address\n"
        "\t%s\t%s\t%s\t%s\n",
        absl::StrCat(view_.kHighlightedFunctionBlankString, kFunctionPrettyNames[0]),
        GetExpectedDisplaySize(kFunctionSizes[0]), GetLoadedModuleNameByPath(kModulePaths[0]),
        GetExpectedDisplayAddress(kFrameAddress));
    CheckCopySelectionIsInvoked(context_menu, app_, view_, expected_clipboard);
  }

  // Export to CSV
  {
    std::string expected_contents = absl::StrFormat(
        R"("Hooked","Function","Size","Module","Sampled Address")"
        "\r\n"
        R"("","%s","%s","%s","%s")"
        "\r\n",
        absl::StrCat(view_.kHighlightedFunctionBlankString, kFunctionPrettyNames[0]),
        GetExpectedDisplaySize(kFunctionSizes[0]), GetLoadedModuleNameByPath(kModulePaths[0]),
        GetExpectedDisplayAddress(kFrameAddress));
    CheckExportToCsvIsInvoked(context_menu, app_, view_, expected_contents);
  }

  // Go to Disassembly
  {
    const auto disassembly_index =
        std::find(context_menu.begin(), context_menu.end(), kMenuActionDisassembly) -
        context_menu.begin();
    ASSERT_LT(disassembly_index, context_menu.size());

    EXPECT_CALL(app_, Disassemble)
        .Times(1)
        .WillOnce([&](int32_t /*pid*/, const FunctionInfo& function) {
          EXPECT_EQ(function.name(), kFunctionNames[0]);
        });
    view_.OnContextMenu(std::string{kMenuActionDisassembly}, static_cast<int>(disassembly_index),
                        {0});
  }

  // Go to Source code
  {
    const auto source_code_index =
        std::find(context_menu.begin(), context_menu.end(), kMenuActionSourceCode) -
        context_menu.begin();
    ASSERT_LT(source_code_index, context_menu.size());

    EXPECT_CALL(app_, ShowSourceCode).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kFunctionNames[0]);
    });
    view_.OnContextMenu(std::string{kMenuActionSourceCode}, static_cast<int>(source_code_index),
                        {0});
  }

  // Hook
  {
    const auto hook_index = std::find(context_menu.begin(), context_menu.end(), kMenuActionSelect) -
                            context_menu.begin();
    ASSERT_LT(hook_index, context_menu.size());

    EXPECT_CALL(app_, SelectFunction).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kFunctionNames[0]);
    });
    view_.OnContextMenu(std::string{kMenuActionSelect}, static_cast<int>(hook_index), {0});
  }

  function_selected = true;
  context_menu = FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(0, {0}));
  ASSERT_FALSE(context_menu.empty());

  // Unhook
  {
    const auto unhook_index =
        std::find(context_menu.begin(), context_menu.end(), kMenuActionUnselect) -
        context_menu.begin();
    ASSERT_LT(unhook_index, context_menu.size());

    EXPECT_CALL(app_, DeselectFunction).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kFunctionNames[0]);
    });
    view_.OnContextMenu(std::string{kMenuActionUnselect}, static_cast<int>(unhook_index), {0});
  }
}

TEST_F(CallstackDataViewTest, FilteringShowsRightResults) {
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, GetMutableCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));

  SetCallstackFromFrames({
      // CallstackDataViewFrame: frame.module  frame.function
      0x9260,  //                 module 1      function 1 (displayed name: "main(int, char**)")
      0x7720,  //                 module 2      function 2 (displayed name: "ffind(int)")
      0x5250   //                 module 3      nullptr (displayed name: "???")
  });

  // Filtering by function displayed name with single token
  {
    view_.OnFilter("int");
    EXPECT_EQ(view_.GetNumElements(), 2);
    EXPECT_THAT((std::array{view_.GetValue(0, kColumnName), view_.GetValue(1, kColumnName)}),
                testing::UnorderedElementsAre(
                    absl::StrCat(view_.kHighlightedFunctionBlankString, kFunctionPrettyNames[1]),
                    absl::StrCat(view_.kHighlightedFunctionBlankString, kFunctionPrettyNames[2])));
  }

  {
    view_.OnFilter("???");
    EXPECT_EQ(view_.GetNumElements(), 1);
    EXPECT_THAT(view_.GetValue(0, kColumnName),
                absl::StrCat(view_.kHighlightedFunctionBlankString, "???"));
  }

  // Filtering by function displayed name with multiple tokens separated by " "
  {
    view_.OnFilter("int main");
    EXPECT_EQ(view_.GetNumElements(), 1);
    EXPECT_EQ(view_.GetValue(0, kColumnName),
              absl::StrCat(view_.kHighlightedFunctionBlankString, kFunctionPrettyNames[1]));
  }

  // No matching result
  {
    view_.OnFilter("int module");
    EXPECT_EQ(view_.GetNumElements(), 0);
  }
}