/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _LIBUNWINDSTACK_OBJECT_H
#define _LIBUNWINDSTACK_OBJECT_H

#include <string>

#include <memory>
#include <mutex>
#include <unordered_map>

#include <unwindstack/Arch.h>
#include <unwindstack/Error.h>
#include <unwindstack/SharedString.h>

namespace unwindstack {

class MapInfo;
class Regs;
class Memory;
struct ErrorData;

class Object {
 public:
  Object() = default;
  virtual ~Object() = default;

  virtual bool Init() = 0;
  virtual bool valid() = 0;
  virtual void Invalidate() = 0;

  virtual int64_t GetLoadBias() = 0;
  virtual std::string GetBuildID() = 0;

  virtual std::string GetSoname() = 0;
  virtual bool GetFunctionName(uint64_t addr, SharedString* name, uint64_t* func_offset) = 0;
  virtual bool GetGlobalVariableOffset(const std::string& name, uint64_t* memory_address) = 0;

  virtual ArchEnum arch() = 0;

  virtual uint64_t GetRelPc(uint64_t pc, MapInfo* map_info) = 0;

  virtual bool StepIfSignalHandler(uint64_t rel_pc, Regs* regs, Memory* process_memory) = 0;
  virtual bool Step(uint64_t rel_pc, Regs* regs, Memory* process_memory, bool* finished,
                    bool* is_signal_frame) = 0;

  virtual Memory* memory() = 0;

  virtual void GetLastError(ErrorData* data) = 0;
  virtual ErrorCode GetLastErrorCode() = 0;
  virtual uint64_t GetLastErrorAddress() = 0;

  static void SetCachingEnabled(bool enable);
  static bool CachingEnabled() { return cache_enabled_; }

  static void CacheLock();
  static void CacheUnlock();
  static void CacheAdd(MapInfo* info);
  static bool CacheGet(MapInfo* info);
  static bool CacheAfterCreateMemory(MapInfo* info);

 protected:
  static bool cache_enabled_;
  static std::unordered_map<std::string, std::pair<std::shared_ptr<Object>, bool>>* cache_;
  static std::mutex* cache_lock_;
};

}  // namespace unwindstack

#endif  // _LIBUNWINDSTACK_OBJECT_H