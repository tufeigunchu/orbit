/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef _LIBUNWINDSTACK_LOG_H
#define _LIBUNWINDSTACK_LOG_H

#include <stdarg.h>
#include <stdint.h>

#if !defined(__printflike)
#define __printflike(x, y) __attribute__((__format__(printf, x, y)))
#endif

namespace unwindstack {

namespace Log {

void Error(const char* format, ...) __printflike(1, 2);
void Info(const char* format, ...) __printflike(1, 2);
void Info(uint8_t indent, const char* format, ...) __printflike(2, 3);
void AsyncSafe(const char* format, ...) __printflike(1, 2);

}  // namespace Log

}  // namespace unwindstack

#endif  // _LIBUNWINDSTACK_LOG_H
