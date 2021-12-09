// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/strings/str_split.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

#include "ClientProtos/preset.pb.h"
#include "DataViewTestUtils.h"
#include "DataViewUtils.h"
#include "DataViews/DataView.h"
#include "DataViews/PresetLoadState.h"
#include "DataViews/PresetsDataView.h"
#include "MetricsUploader/MetricsUploaderStub.h"
#include "MockAppInterface.h"
#include "OrbitBase/File.h"
#include "OrbitBase/ReadFileToString.h"
#include "OrbitBase/TemporaryFile.h"
#include "PresetFile/PresetFile.h"
#include "TestUtils/TestUtils.h"

using orbit_data_views::CheckCopySelectionIsInvoked;
using orbit_data_views::CheckExportToCsvIsInvoked;
using orbit_data_views::FlattenContextMenuWithGrouping;
using orbit_data_views::kMenuActionCopySelection;
using orbit_data_views::kMenuActionDeletePreset;
using orbit_data_views::kMenuActionExportToCsv;
using orbit_data_views::kMenuActionLoadPreset;
using orbit_data_views::kMenuActionShowInExplorer;

namespace {
// This is just a helper type to handle colors. Note that Color from `OrbitGl/CoreMath.h` is not
// available in this module.
struct Color {
  uint8_t r, g, b;
  friend bool operator==(const Color& lhs, const Color& rhs) {
    return std::tie(lhs.r, lhs.g, lhs.b) == std::tie(rhs.r, rhs.g, rhs.b);
  }
  friend bool operator!=(const Color& lhs, const Color& rhs) { return !(lhs == rhs); }
};
}  // namespace

namespace orbit_data_views {

class PresetsDataViewTest : public testing::Test {
 public:
  explicit PresetsDataViewTest() : view_{&app_, &metrics_uploader_} {}

 protected:
  orbit_metrics_uploader::MetricsUploaderStub metrics_uploader_;
  MockAppInterface app_;
  PresetsDataView view_;
};

TEST_F(PresetsDataViewTest, ColumnHeadersNotEmpty) {
  EXPECT_GE(view_.GetColumns().size(), 1);
  for (const auto& column : view_.GetColumns()) {
    EXPECT_FALSE(column.header.empty());
  }
}

TEST_F(PresetsDataViewTest, Empty) {
  EXPECT_EQ(view_.GetNumElements(), 0);
  EXPECT_EQ(view_.GetLabel(), "Presets");
}

TEST_F(PresetsDataViewTest, CheckLabelAndColorForLoadStates) {
  // GetPresetLoadState is called once per `GetValue` and `GetToolTip` call.
  auto load_state = PresetLoadState::kLoadable;
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(9)
      .WillRepeatedly(testing::ReturnPointee(&load_state));

  orbit_client_protos::PresetInfo preset_info0{};
  orbit_preset_file::PresetFile preset_file0{std::filesystem::path{}, preset_info0};
  view_.SetPresets({preset_file0});

  EXPECT_EQ(view_.GetNumElements(), 1);

  load_state = PresetLoadState::kLoadable;
  EXPECT_EQ(view_.GetValue(0, 0), "Yes");
  EXPECT_TRUE(view_.GetToolTip(0, 0).empty());
  Color color_loadable_state{};
  view_.GetDisplayColor(0, 0, color_loadable_state.r, color_loadable_state.g,
                        color_loadable_state.b);

  load_state = PresetLoadState::kNotLoadable;
  EXPECT_EQ(view_.GetValue(0, 0), "No");
  EXPECT_FALSE(view_.GetToolTip(0, 0).empty());
  Color color_not_loadable_state{};
  view_.GetDisplayColor(0, 0, color_not_loadable_state.r, color_not_loadable_state.g,
                        color_not_loadable_state.b);

  load_state = PresetLoadState::kPartiallyLoadable;
  EXPECT_EQ(view_.GetValue(0, 0), "Partially");
  EXPECT_TRUE(view_.GetToolTip(0, 0).empty());
  Color color_partially_loadable_state{};
  view_.GetDisplayColor(0, 0, color_partially_loadable_state.r, color_partially_loadable_state.g,
                        color_partially_loadable_state.b);

  // We don't test for specific color values here, but we will ensure that the colors are different,
  // hence that the load state is indicated by color.
  EXPECT_TRUE(view_.WantsDisplayColor());
  EXPECT_NE(color_loadable_state, color_partially_loadable_state);
  EXPECT_NE(color_loadable_state, color_not_loadable_state);
  EXPECT_NE(color_partially_loadable_state, color_not_loadable_state);
}

TEST_F(PresetsDataViewTest, PresetNameIsFileName) {
  orbit_client_protos::PresetInfo preset_info0{};
  const std::filesystem::path preset_filename0{"/path/filename.xyz"};
  orbit_preset_file::PresetFile preset_file0{preset_filename0, preset_info0};
  view_.SetPresets({preset_file0});

  EXPECT_EQ(view_.GetNumElements(), 1);
  EXPECT_EQ(view_.GetValue(0, 1), preset_filename0.filename().string());
}

TEST_F(PresetsDataViewTest, ViewIsUpdatedAfterSetPresets) {
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(PresetLoadState::kLoadable));

  orbit_client_protos::PresetInfo preset_info0{};
  const std::filesystem::path preset_filename0{"/path/filename.xyz"};
  orbit_preset_file::PresetFile preset_file0{preset_filename0, preset_info0};

  orbit_client_protos::PresetInfo preset_info1{};
  const std::filesystem::path preset_filename1{"/path/other.xyz"};
  orbit_preset_file::PresetFile preset_file1{preset_filename1, preset_info1};

  view_.SetPresets({preset_file0, preset_file1});

  EXPECT_EQ(view_.GetNumElements(), 2);
  EXPECT_EQ(view_.GetValue(0, 1), preset_filename0.filename().string());
  EXPECT_EQ(view_.GetValue(1, 1), preset_filename1.filename().string());

  view_.SetPresets({});
  EXPECT_EQ(view_.GetNumElements(), 0);

  view_.SetPresets({preset_file1, preset_file0});
  EXPECT_EQ(view_.GetNumElements(), 2);
  EXPECT_EQ(view_.GetValue(0, 1), preset_filename1.filename().string());
  EXPECT_EQ(view_.GetValue(1, 1), preset_filename0.filename().string());
}

TEST_F(PresetsDataViewTest, CheckListingOfModulesPerPreset) {
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(PresetLoadState::kLoadable));

  orbit_client_protos::PresetModule module0{};
  module0.mutable_function_names()->Add("main");
  module0.mutable_function_names()->Add("foo");
  module0.mutable_function_names()->Add("bar");

  orbit_client_protos::PresetModule module1{};
  module1.mutable_function_names()->Add("execute_order66");
  module1.mutable_frame_track_function_names()->Add("execute_order66");

  orbit_client_protos::PresetInfo preset_info0{};
  (*preset_info0.mutable_modules())["main_module"] = module0;
  (*preset_info0.mutable_modules())["other_module"] = module1;

  const std::filesystem::path preset_filename0{"/path/filename.xyz"};
  orbit_preset_file::PresetFile preset_file0{preset_filename0, preset_info0};

  view_.SetPresets({preset_file0});

  EXPECT_EQ(view_.GetNumElements(), 1);

  // We don't enforce an order here. That might change in the future though.
  EXPECT_THAT(absl::StrSplit(view_.GetValue(0, 2), '\n'),
              testing::UnorderedElementsAre("main_module", "other_module"));
  // Column 3 lists the number of functions in each module.
  EXPECT_THAT(absl::StrSplit(view_.GetValue(0, 3), '\n'),
              testing::UnorderedElementsAre(std::to_string(module0.function_names_size()),
                                            std::to_string(module1.function_names_size())));
}

TEST_F(PresetsDataViewTest, CheckPresenceOfContextMenuEntries) {
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(testing::AnyNumber())
      .WillRepeatedly([](const orbit_preset_file::PresetFile& preset) {
        if (preset.file_path().filename().string() == "loadable.preset") {
          return PresetLoadState::kLoadable;
        }

        if (preset.file_path().filename().string() == "not_loadable.preset") {
          return PresetLoadState::kNotLoadable;
        }

        return PresetLoadState::kPartiallyLoadable;
      });

  const std::filesystem::path preset_filename0{"/path/loadable.preset"};
  orbit_preset_file::PresetFile preset_file0{preset_filename0, orbit_client_protos::PresetInfo{}};

  const std::filesystem::path preset_filename1{"/path/not_loadable.preset"};
  orbit_preset_file::PresetFile preset_file1{preset_filename1, orbit_client_protos::PresetInfo{}};

  const std::filesystem::path preset_filename2{"/path/partially_loadable.preset"};
  orbit_preset_file::PresetFile preset_file2{preset_filename2, orbit_client_protos::PresetInfo{}};

  view_.SetPresets({preset_file0, preset_file1, preset_file2});
  view_.OnSort(1, DataView::SortingOrder::kAscending);

  // Loadable preset
  EXPECT_THAT(FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(0, {0})),
              testing::ElementsAre(kMenuActionLoadPreset, kMenuActionDeletePreset,
                                   kMenuActionShowInExplorer, kMenuActionCopySelection,
                                   kMenuActionExportToCsv))
      << view_.GetValue(0, 1);

  // Not loadable preset
  EXPECT_THAT(FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(1, {1})),
              testing::ElementsAre(kMenuActionDeletePreset, kMenuActionShowInExplorer,
                                   kMenuActionCopySelection, kMenuActionExportToCsv))
      << view_.GetValue(1, 1);

  // Partially loadable preset
  EXPECT_THAT(FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(2, {2})),
              testing::ElementsAre(kMenuActionLoadPreset, kMenuActionDeletePreset,
                                   kMenuActionShowInExplorer, kMenuActionCopySelection,
                                   kMenuActionExportToCsv))
      << view_.GetValue(2, 1);
}

TEST_F(PresetsDataViewTest, CheckInvokedContextMenuActions) {
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(PresetLoadState::kLoadable));

  auto temporary_preset_file = orbit_base::TemporaryFile::Create();
  ASSERT_THAT(temporary_preset_file, orbit_test_utils::HasNoError());
  temporary_preset_file.value().CloseAndRemove();

  const std::filesystem::path preset_filename0 = temporary_preset_file.value().file_path();
  orbit_preset_file::PresetFile preset_file0{preset_filename0, orbit_client_protos::PresetInfo{}};
  ASSERT_THAT(preset_file0.SaveToFile(), orbit_test_utils::HasNoError());
  auto date_modified = orbit_base::GetFileDateModified(preset_filename0);
  ASSERT_THAT(date_modified, orbit_test_utils::HasNoError());

  view_.SetPresets({preset_file0});
  std::vector<std::string> context_menu =
      FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(0, {0}));
  ASSERT_FALSE(context_menu.empty());

  // Copy Selection
  {
    std::string expected_clipboard = absl::StrFormat(
        "Loadable\tPreset\tModules\tHooked Functions\tDate Modified\n"
        "Yes\t%s\t\t\t%s\n",
        preset_filename0.filename().string(), FormatShortDatetime(date_modified.value()));
    CheckCopySelectionIsInvoked(context_menu, app_, view_, expected_clipboard);
  }

  // Export to CSV
  {
    std::string expected_contents = absl::StrFormat(
        R"("Loadable","Preset","Modules","Hooked Functions","Date Modified")"
        "\r\n"
        R"("Yes","%s","","","%s")"
        "\r\n",
        preset_filename0.filename().string(), FormatShortDatetime(date_modified.value()));
    CheckExportToCsvIsInvoked(context_menu, app_, view_, expected_contents);
  }

  // Load Preset
  {
    const auto load_preset_idx =
        std::find(context_menu.begin(), context_menu.end(), kMenuActionLoadPreset) -
        context_menu.begin();
    ASSERT_LT(load_preset_idx, context_menu.size());

    EXPECT_CALL(app_, LoadPreset)
        .Times(1)
        .WillOnce([&](const orbit_preset_file::PresetFile& preset_file) {
          EXPECT_EQ(preset_file.file_path(), preset_filename0);
        });
    view_.OnContextMenu(std::string{kMenuActionLoadPreset}, static_cast<int>(load_preset_idx), {0});
  }

  // Show In Explorer
  {
    const auto show_in_explorer_idx =
        std::find(context_menu.begin(), context_menu.end(), kMenuActionShowInExplorer) -
        context_menu.begin();
    ASSERT_LT(show_in_explorer_idx, context_menu.size());

    EXPECT_CALL(app_, ShowPresetInExplorer)
        .Times(1)
        .WillOnce([&](const orbit_preset_file::PresetFile& preset_file) {
          EXPECT_EQ(preset_file.file_path(), preset_filename0);
        });
    view_.OnContextMenu(std::string{kMenuActionShowInExplorer},
                        static_cast<int>(show_in_explorer_idx), {0});
  }

  // Delete Preset
  {
    const auto delete_preset_idx =
        std::find(context_menu.begin(), context_menu.end(), kMenuActionDeletePreset) -
        context_menu.begin();
    ASSERT_LT(delete_preset_idx, context_menu.size());

    view_.OnContextMenu(std::string{kMenuActionDeletePreset}, static_cast<int>(delete_preset_idx),
                        {0});

    const auto file_exists = orbit_base::FileExists(preset_filename0);
    ASSERT_THAT(file_exists, orbit_test_utils::HasNoError());
    EXPECT_FALSE(file_exists.value());

    EXPECT_EQ(view_.GetNumElements(), 0);

    // Now let's try to delete a non-existing preset. This should fail and we should get an error
    // message (call to SendErrorToUi).
    const std::filesystem::path preset_filename1{"/path/filename.preset"};
    orbit_preset_file::PresetFile preset_file1{preset_filename1, orbit_client_protos::PresetInfo{}};
    view_.SetPresets({preset_file1});

    EXPECT_CALL(app_, SendErrorToUi).Times(1);
    view_.OnContextMenu(std::string{kMenuActionDeletePreset}, static_cast<int>(delete_preset_idx),
                        {0});

    EXPECT_EQ(view_.GetNumElements(), 1);
  }
}

TEST_F(PresetsDataViewTest, CheckLoadPresetOnDoubleClick) {
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(PresetLoadState::kLoadable));

  const std::filesystem::path preset_filename0{"/path/loadable.preset"};
  orbit_preset_file::PresetFile preset_file0{preset_filename0, orbit_client_protos::PresetInfo{}};

  view_.SetPresets({preset_file0});
  std::vector<std::string> context_menu =
      FlattenContextMenuWithGrouping(view_.GetContextMenuWithGrouping(0, {0}));
  ASSERT_FALSE(context_menu.empty());

  EXPECT_CALL(app_, LoadPreset)
      .Times(1)
      .WillOnce([&](const orbit_preset_file::PresetFile& preset_file) {
        EXPECT_EQ(preset_file.file_path(), preset_filename0);
      });
  view_.OnDoubleClicked(0);
}

TEST_F(PresetsDataViewTest, CheckSortingByPresetName) {
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(PresetLoadState::kLoadable));

  const std::filesystem::path preset_filename0{"/path/a.preset"};
  orbit_preset_file::PresetFile preset_file0{preset_filename0, orbit_client_protos::PresetInfo{}};

  const std::filesystem::path preset_filename1{"/path/b.preset"};
  orbit_preset_file::PresetFile preset_file1{preset_filename1, orbit_client_protos::PresetInfo{}};

  const std::filesystem::path preset_filename2{"/path/c.preset"};
  orbit_preset_file::PresetFile preset_file2{preset_filename2, orbit_client_protos::PresetInfo{}};

  view_.SetPresets({preset_file0, preset_file1, preset_file2});

  view_.OnSort(1, DataView::SortingOrder::kAscending);
  EXPECT_EQ(view_.GetValue(0, 1), "a.preset");
  EXPECT_EQ(view_.GetValue(1, 1), "b.preset");
  EXPECT_EQ(view_.GetValue(2, 1), "c.preset");

  view_.OnSort(1, DataView::SortingOrder::kDescending);
  EXPECT_EQ(view_.GetValue(0, 1), "c.preset");
  EXPECT_EQ(view_.GetValue(1, 1), "b.preset");
  EXPECT_EQ(view_.GetValue(2, 1), "a.preset");
}

TEST_F(PresetsDataViewTest, Filter) {
  EXPECT_CALL(app_, GetPresetLoadState)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(PresetLoadState::kLoadable));

  const std::filesystem::path preset_filename0{"/path/a.preset"};
  orbit_preset_file::PresetFile preset_file0{preset_filename0, orbit_client_protos::PresetInfo{}};

  const std::filesystem::path preset_filename1{"/path/b.preset"};
  orbit_preset_file::PresetFile preset_file1{preset_filename1, orbit_client_protos::PresetInfo{}};

  const std::filesystem::path preset_filename2{"/path/c.preset"};
  orbit_preset_file::PresetFile preset_file2{preset_filename2, orbit_client_protos::PresetInfo{}};

  view_.SetPresets({preset_file0, preset_file1, preset_file2});

  view_.OnFilter("a");
  ASSERT_EQ(view_.GetNumElements(), 1);
  EXPECT_EQ(view_.GetValue(0, 1), "a.preset");

  view_.OnFilter("b");
  ASSERT_EQ(view_.GetNumElements(), 1);
  EXPECT_EQ(view_.GetValue(0, 1), "b.preset");

  view_.OnFilter("c");
  ASSERT_EQ(view_.GetNumElements(), 1);
  EXPECT_EQ(view_.GetValue(0, 1), "c.preset");

  view_.OnFilter("preset");
  ASSERT_EQ(view_.GetNumElements(), 3);

  view_.OnFilter("");
  ASSERT_EQ(view_.GetNumElements(), 3);
}

}  // namespace orbit_data_views