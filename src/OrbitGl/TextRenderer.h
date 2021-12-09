// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_GL_TEXT_RENDERER_H_
#define ORBIT_GL_TEXT_RENDERER_H_

#include <GteVector.h>
#include <freetype-gl/mat4.h>
#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>
#include <freetype-gl/vec234.h>
#include <glad/glad.h>
#include <stddef.h>
#include <stdint.h>

#include <map>
#include <unordered_map>
#include <vector>

#include "Batcher.h"
#include "CoreMath.h"
#include "PickingManager.h"
#include "Viewport.h"

namespace ftgl {
struct vertex_buffer_t;
struct texture_font_t;
}  // namespace ftgl

class TextRenderer {
 public:
  enum class HAlign { Left, Right };
  enum class VAlign { Top, Middle, Bottom };

  struct TextFormatting {
    uint32_t font_size = 14;
    Color color = Color(255, 255, 255, 255);
    float max_size = -1.f;
    HAlign halign = HAlign::Left;
    VAlign valign = VAlign::Top;
  };

  explicit TextRenderer();
  ~TextRenderer();

  void Init();
  void Clear();
  void SetViewport(orbit_gl::Viewport* viewport) { viewport_ = viewport; }

  void RenderLayer(float layer);
  void RenderDebug(Batcher* batcher);
  [[nodiscard]] std::vector<float> GetLayers() const;

  void AddText(const char* text, float x, float y, float z, TextFormatting formatting,
               Vec2* out_text_pos = nullptr, Vec2* out_text_size = nullptr);

  float AddTextTrailingCharsPrioritized(const char* text, float x, float y, float z,
                                        TextFormatting formatting, size_t trailing_chars_length);

  [[nodiscard]] float GetStringWidth(const char* text, uint32_t font_size);
  [[nodiscard]] float GetStringHeight(const char* text, uint32_t font_size);

  void PushTranslation(float x, float y, float z = 0.f) { translations_.PushTranslation(x, y, z); }
  void PopTranslation() { translations_.PopTranslation(); }

  static void SetDrawOutline(bool value) { draw_outline_ = value; }

 protected:
  void AddTextInternal(const char* text, ftgl::vec2* pen, const TextFormatting& formatting, float z,
                       ftgl::vec2* out_text_pos = nullptr, ftgl::vec2* out_text_size = nullptr);

  [[nodiscard]] int GetStringWidthScreenSpace(const char* text, uint32_t font_size);
  [[nodiscard]] int GetStringHeightScreenSpace(const char* text, uint32_t font_size);
  [[nodiscard]] ftgl::texture_font_t* GetFont(uint32_t size);
  [[nodiscard]] ftgl::texture_glyph_t* MaybeLoadAndGetGlyph(ftgl::texture_font_t* self,
                                                            const char* character);

  void DrawOutline(Batcher* batcher, ftgl::vertex_buffer_t* buffer);

 private:
  ftgl::texture_atlas_t* texture_atlas_;
  // Indicates when a change to the texture atlas occurred so that we have to reupload the
  // texture data. Only freetype-gl's texture_font_load_glyph modifies the texture atlas,
  // so we need to set this to true when and only when we call that function.
  bool texture_atlas_changed_;
  std::unordered_map<float, ftgl::vertex_buffer_t*> vertex_buffers_by_layer_;
  std::map<uint32_t, ftgl::texture_font_t*> fonts_by_size_;
  orbit_gl::Viewport* viewport_;
  GLuint shader_;
  ftgl::mat4 model_;
  ftgl::mat4 view_;
  ftgl::mat4 projection_;
  ftgl::vec2 pen_;
  bool initialized_;
  static bool draw_outline_;

  orbit_gl::TranslationStack translations_;
};

inline ftgl::vec4 ColorToVec4(const Color& color) {
  const float coeff = 1.f / 255.f;
  ftgl::vec4 vec;
  vec.r = color[0] * coeff;
  vec.g = color[1] * coeff;
  vec.b = color[2] * coeff;
  vec.a = color[3] * coeff;
  return vec;
}

#endif  // ORBIT_GL_TEXT_RENDERER_H_
