// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "TranslationStack.h"

#include "OrbitBase/Logging.h"

void orbit_gl::TranslationStack::PushTranslation(float x, float y, float z) {
  const Vec3 translation(x, y, z);
  translation_stack_.push_back(current_translation_);
  current_translation_ += translation;
}

void orbit_gl::TranslationStack::PopTranslation() {
  CHECK(!translation_stack_.empty());
  current_translation_ = translation_stack_.back();
  translation_stack_.pop_back();
}
