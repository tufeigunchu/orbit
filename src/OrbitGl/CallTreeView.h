// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_GL_CALL_TREE_VIEW_H_
#define ORBIT_GL_CALL_TREE_VIEW_H_

#include <absl/container/node_hash_map.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ClientData/CaptureData.h"
#include "ClientData/PostProcessedSamplingData.h"

class CallTreeThread;
class CallTreeFunction;
class CallTreeUnwindErrors;

class CallTreeNode {
 public:
  explicit CallTreeNode(CallTreeNode* parent) : parent_{parent} {}
  virtual ~CallTreeNode() = default;

  // parent(), child_count(), children() are needed by CallTreeViewItemModel.
  [[nodiscard]] const CallTreeNode* parent() const { return parent_; }

  [[nodiscard]] uint64_t child_count() const {
    return thread_children_.size() + function_children_.size() +
           (unwind_errors_child_ != nullptr ? 1 : 0);
  }

  [[nodiscard]] const std::vector<const CallTreeNode*>& children() const;

  [[nodiscard]] CallTreeThread* GetThreadOrNull(uint32_t thread_id);

  [[nodiscard]] CallTreeThread* AddAndGetThread(uint32_t thread_id, std::string thread_name);

  [[nodiscard]] CallTreeFunction* GetFunctionOrNull(uint64_t function_absolute_address);

  [[nodiscard]] CallTreeFunction* AddAndGetFunction(uint64_t function_absolute_address,
                                                    std::string function_name,
                                                    std::string module_path,
                                                    std::string module_build_id);

  [[nodiscard]] CallTreeUnwindErrors* GetUnwindErrorsOrNull();

  [[nodiscard]] CallTreeUnwindErrors* AddAndGetUnwindErrors();

  [[nodiscard]] uint64_t sample_count() const { return sample_count_; }

  void IncreaseSampleCount(uint64_t sample_count_increase) {
    sample_count_ += sample_count_increase;
  }

  [[nodiscard]] float GetInclusivePercent(uint64_t total_sample_count) const {
    return 100.0f * sample_count() / total_sample_count;
  }

  [[nodiscard]] float GetPercentOfParent() const {
    if (parent_ == nullptr) {
      return 100.0f;
    }
    return 100.0f * sample_count() / parent_->sample_count();
  }

  [[nodiscard]] uint64_t GetExclusiveSampleCount() const;

  [[nodiscard]] float GetExclusivePercent(uint64_t total_sample_count) const {
    return 100.0f * GetExclusiveSampleCount() / total_sample_count;
  }

 private:
  // absl::node_hash_map instead of absl::flat_hash_map as pointer stability is
  // needed for the CallTreeNode::parent_ field.
  absl::node_hash_map<uint32_t, CallTreeThread> thread_children_;
  absl::node_hash_map<uint64_t, CallTreeFunction> function_children_;
  // std::shared_ptr instead of std::unique_ptr because absl::node_hash_map
  // needs the copy constructor (even for try_emplace).
  std::shared_ptr<CallTreeUnwindErrors> unwind_errors_child_;

  CallTreeNode* parent_;
  uint64_t sample_count_ = 0;

  // Filled lazily when children() is called, invalidated when children are invalidated.
  mutable std::optional<std::vector<const CallTreeNode*>> children_cache_;
};

class CallTreeFunction : public CallTreeNode {
 public:
  explicit CallTreeFunction(uint64_t function_absolute_address, std::string function_name,
                            std::string module_path, std::string module_build_id,
                            CallTreeNode* parent)
      : CallTreeNode{parent},
        function_absolute_address_{function_absolute_address},
        function_name_{std::move(function_name)},
        module_path_{std::move(module_path)},
        module_build_id_{std::move(module_build_id)} {}

  [[nodiscard]] uint64_t function_absolute_address() const { return function_absolute_address_; }

  [[nodiscard]] const std::string& function_name() const { return function_name_; }

  [[nodiscard]] const std::string& module_path() const { return module_path_; }

  [[nodiscard]] const std::string& module_build_id() const { return module_build_id_; }

  [[nodiscard]] std::string GetModuleName() const {
    return std::filesystem::path(module_path()).filename().string();
  }

 private:
  uint64_t function_absolute_address_;
  std::string function_name_;
  std::string module_path_;
  std::string module_build_id_;
};

class CallTreeThread : public CallTreeNode {
 public:
  explicit CallTreeThread(uint32_t thread_id, std::string thread_name, CallTreeNode* parent)
      : CallTreeNode{parent}, thread_id_{thread_id}, thread_name_{std::move(thread_name)} {}

  [[nodiscard]] uint32_t thread_id() const { return thread_id_; }

  [[nodiscard]] const std::string& thread_name() const { return thread_name_; }

 private:
  uint32_t thread_id_;
  std::string thread_name_;
};

class CallTreeUnwindErrors : public CallTreeNode {
 public:
  explicit CallTreeUnwindErrors(CallTreeNode* parent) : CallTreeNode{parent} {}
};

class CallTreeView : public CallTreeNode {
 public:
  [[nodiscard]] static std::unique_ptr<CallTreeView> CreateTopDownViewFromPostProcessedSamplingData(
      const orbit_client_data::PostProcessedSamplingData& post_processed_sampling_data,
      const orbit_client_data::CaptureData& capture_data);

  [[nodiscard]] static std::unique_ptr<CallTreeView>
  CreateBottomUpViewFromPostProcessedSamplingData(
      const orbit_client_data::PostProcessedSamplingData& post_processed_sampling_data,
      const orbit_client_data::CaptureData& capture_data);

  CallTreeView() : CallTreeNode{nullptr} {}
};

#endif  // ORBIT_GL_CALL_TREE_VIEW_H_
