// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "TextRenderer.h"

#include <float.h>
#include <freetype-gl/shader.h>
#include <freetype-gl/vector.h>
#include <freetype-gl/vertex-buffer.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <utility>

#include "GlCanvas.h"
#include "Introspection/Introspection.h"
#include "OrbitBase/ExecutablePath.h"
#include "OrbitBase/Logging.h"

namespace {

struct vertex_t {
  float x, y, z;     // position
  float s, t;        // texture
  float r, g, b, a;  // color
};

int GetStringLineCount(const char* string) {
  int result = 1;
  for (size_t i = 0; i < strlen(string); ++i) {
    if (string[i] == '\n') {
      ++result;
    }
  }
  return result;
}

}  // namespace

bool TextRenderer::draw_outline_ = false;

TextRenderer::TextRenderer()
    : texture_atlas_(nullptr),
      texture_atlas_changed_(false),
      viewport_(nullptr),
      initialized_(false) {}

TextRenderer::~TextRenderer() {
  for (const auto& pair : fonts_by_size_) {
    texture_font_delete(pair.second);
  }
  fonts_by_size_.clear();

  for (auto& [unused_layer, buffer] : vertex_buffers_by_layer_) {
    vertex_buffer_delete(buffer);
  }
  vertex_buffers_by_layer_.clear();

  if (texture_atlas_) {
    texture_atlas_delete(texture_atlas_);
  }
}

void TextRenderer::Init() {
  if (initialized_) return;

  int atlas_size = 2 * 1024;
  texture_atlas_ = ftgl::texture_atlas_new(atlas_size, atlas_size, 1);

  const auto exe_dir = orbit_base::GetExecutableDir();
  const auto font_file_name = (exe_dir / "fonts" / "Vera.ttf").string();

  for (int i = 1; i <= 100; i += 1) {
    fonts_by_size_[i] = texture_font_new_from_file(texture_atlas_, i, font_file_name.c_str());
  }

  pen_.x = 0;
  pen_.y = 0;

  glGenTextures(1, &texture_atlas_->id);
  glBindTexture(GL_TEXTURE_2D, texture_atlas_->id);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, static_cast<GLsizei>(texture_atlas_->width),
               static_cast<GLsizei>(texture_atlas_->height), 0, GL_RED, GL_UNSIGNED_BYTE,
               texture_atlas_->data);
  texture_atlas_changed_ = false;

  glBindTexture(GL_TEXTURE_2D, 0);

  const auto vert_shader_file_name = (exe_dir / "shaders" / "v3f-t2f-c4f.vert").string();
  const auto frag_shader_file_name = (exe_dir / "shaders" / "v3f-t2f-c4f.frag").string();
  shader_ = ftgl::shader_load(vert_shader_file_name.c_str(), frag_shader_file_name.c_str());

  mat4_set_identity(&projection_);
  mat4_set_identity(&model_);
  mat4_set_identity(&view_);

  initialized_ = true;
}

ftgl::texture_font_t* TextRenderer::GetFont(uint32_t size) {
  CHECK(!fonts_by_size_.empty());
  if (fonts_by_size_.count(size) == 0) {
    auto iterator_next = fonts_by_size_.upper_bound(size);
    // If there isn't that font_size in the map, we will search for the next one or previous one
    if (iterator_next != fonts_by_size_.end()) {
      size = iterator_next->first;
    } else if (iterator_next != fonts_by_size_.begin()) {
      size = (--iterator_next)->first;
    }
  }
  return fonts_by_size_[size];
}

// Always use this method when you need to get a glyph, we need to make sure we know when the
// texture atlas has been updated. One way to ensure that is to check if we can find the glyph
// already and, if not, load it explicitly (in which case the texture atlas is updated). Note that
// texture_font_get_glyph internally may load the glyph if it does not find it. We do not want
// that as in that case, we do not know that the atlas has actually changed.
ftgl::texture_glyph_t* TextRenderer::MaybeLoadAndGetGlyph(ftgl::texture_font_t* font,
                                                          const char* character) {
  if (!texture_font_find_glyph(font, character)) {
    texture_font_load_glyph(font, character);
    texture_atlas_changed_ = true;
  }

  return texture_font_get_glyph(font, character);
}

void TextRenderer::RenderLayer(float layer) {
  ORBIT_SCOPE_FUNCTION;
  if (vertex_buffers_by_layer_.count(layer) == 0) return;
  auto& buffer = vertex_buffers_by_layer_.at(layer);

  // Lazy init
  if (!initialized_) {
    Init();
  }

  glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_BLEND);
  glDepthMask(GL_FALSE);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindTexture(GL_TEXTURE_2D, texture_atlas_->id);

  if (texture_atlas_changed_) {
    ORBIT_SCOPE("glTexSubImage2D");
    // Whenever the font texture atlas has changed, we need to update the texture data by
    // uploading the pixels via glTexSubImage2D. We do not need to call glTexImage2D as
    // we have already called that in Init() (and hence texture memory is already allocated).
    // While in theory we would only need to update the area of the atlas that was actually
    // modified (which happens when a glyph is loaded), but there is no immediate way to
    // know about this here.
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(texture_atlas_->width),
                    static_cast<GLsizei>(texture_atlas_->height), GL_RED, GL_UNSIGNED_BYTE,
                    texture_atlas_->data);
    texture_atlas_changed_ = false;
  }

  // Get current projection matrix
  GLfloat matrix[16];
  glGetFloatv(GL_PROJECTION_MATRIX, matrix);
  ftgl::mat4* projection = reinterpret_cast<ftgl::mat4*>(&matrix[0]);
  projection_ = *projection;

  glUseProgram(shader_);
  {
    glUniform1i(glGetUniformLocation(shader_, "texture"), 0);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "model"), 1, 0, model_.data);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "view"), 1, 0, view_.data);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "projection"), 1, 0, projection_.data);
    {
      ORBIT_SCOPE("vertex_buffer_render");
      vertex_buffer_render(buffer, GL_TRIANGLES);
    }
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);

  glPopAttrib();
}

void TextRenderer::RenderDebug(Batcher* batcher) {
  if (!draw_outline_) return;
  for (auto& [unused_layer, buffer] : vertex_buffers_by_layer_) {
    DrawOutline(batcher, buffer);
  }
}

void TextRenderer::DrawOutline(Batcher* batcher, ftgl::vertex_buffer_t* vertex_buffer) {
  if (vertex_buffer == nullptr) return;
  const Color color(255, 255, 255, 255);

  for (size_t i = 0; i < vertex_buffer->indices->size; i += 3) {
    GLuint i0 = *static_cast<const GLuint*>(vector_get(vertex_buffer->indices, i + 0));
    GLuint i1 = *static_cast<const GLuint*>(vector_get(vertex_buffer->indices, i + 1));
    GLuint i2 = *static_cast<const GLuint*>(vector_get(vertex_buffer->indices, i + 2));

    vertex_t v0 = *static_cast<const vertex_t*>(vector_get(vertex_buffer->vertices, i0));
    vertex_t v1 = *static_cast<const vertex_t*>(vector_get(vertex_buffer->vertices, i1));
    vertex_t v2 = *static_cast<const vertex_t*>(vector_get(vertex_buffer->vertices, i2));

    // TODO: This should be pickable??
    batcher->AddLine(Vec2(v0.x, v0.y), Vec2(v1.x, v1.y), GlCanvas::kZValueSlider, color);
    batcher->AddLine(Vec2(v1.x, v1.y), Vec2(v2.x, v2.y), GlCanvas::kZValueSlider, color);
    batcher->AddLine(Vec2(v2.x, v2.y), Vec2(v0.x, v0.y), GlCanvas::kZValueSlider, color);
  }
}

void TextRenderer::AddTextInternal(const char* text, ftgl::vec2* pen,
                                   const TextFormatting& formatting, float z,
                                   ftgl::vec2* out_text_pos, ftgl::vec2* out_text_size) {
  ftgl::texture_font_t* font = GetFont(formatting.font_size);
  ftgl::vec4 color = ColorToVec4(formatting.color);
  float r = color.red;
  float g = color.green;
  float b = color.blue;
  float a = color.alpha;

  float max_width =
      formatting.max_size == -1.f ? FLT_MAX : viewport_->WorldToScreen({formatting.max_size, 0})[0];
  float str_width = 0.f;
  float min_x = FLT_MAX;
  float max_x = -FLT_MAX;
  float min_y = FLT_MAX;
  float max_y = -FLT_MAX;
  constexpr std::array<GLuint, 6> kIndices = {0, 1, 2, 0, 2, 3};
  ftgl::vec2 initial_pen = *pen;

  for (size_t i = 0; i < strlen(text); ++i) {
    if (text[i] == '\n') {
      pen->x = initial_pen.x;
      pen->y += font->height;
      continue;
    }

    ftgl::texture_glyph_t* glyph = MaybeLoadAndGetGlyph(font, text + i);
    if (glyph != nullptr) {
      float kerning = (i == 0) ? 0.0f : texture_glyph_get_kerning(glyph, text + i - 1);
      pen->x += kerning;

      Vec3 pos0 = translations_.TranslateAndFloorVertex(
          Vec3(pen->x + glyph->offset_x, pen->y - glyph->offset_y, z));
      Vec2 pos1 = Vec2(pos0[0] + glyph->width, pos0[1] + glyph->height);
      const float transformed_z = pos0[2];

      float s0 = glyph->s0;
      float t0 = glyph->t0;
      float s1 = glyph->s1;
      float t1 = glyph->t1;

      vertex_t vertices[4] = {{pos0[0], pos0[1], transformed_z, s0, t0, r, g, b, a},
                              {pos0[0], pos1[1], transformed_z, s0, t1, r, g, b, a},
                              {pos1[0], pos1[1], transformed_z, s1, t1, r, g, b, a},
                              {pos1[0], pos0[1], transformed_z, s1, t0, r, g, b, a}};

      min_x = std::min(min_x, pos0[0]);
      max_x = std::max(max_x, pos1[0]);
      min_y = std::min(min_y, pos0[1]);
      max_y = std::max(max_y, pos1[1]);

      str_width = max_x - min_x;

      if (str_width > max_width) {
        break;
      }

      ftgl::vertex_buffer_t*& vertex_buffer = vertex_buffers_by_layer_[transformed_z];
      if (vertex_buffer == nullptr) {
        vertex_buffer = ftgl::vertex_buffer_new("vertex:3f,tex_coord:2f,color:4f");
      }
      vertex_buffer_push_back(vertex_buffer, vertices, 4, kIndices.data(), 6);
      pen->x += glyph->advance_x;
    }
  }

  if (out_text_pos) {
    out_text_pos->x = min_x;
    out_text_pos->y = min_y;
  }

  if (out_text_size) {
    out_text_size->x = max_x - min_x;
    out_text_size->y = max_y - min_y;
  }
}

void TextRenderer::AddText(const char* text, float x, float y, float z, TextFormatting formatting,
                           Vec2* out_text_pos, Vec2* out_text_size) {
  if (strlen(text) == 0) {
    return;
  }

  Vec2i pen_pos = viewport_->WorldToScreen(Vec2(x, y));
  pen_.x = pen_pos[0];
  pen_.y = pen_pos[1];

  float min_width = GetStringWidth(".", formatting.font_size);
  if (formatting.max_size >= 0 && min_width > formatting.max_size) {
    return;
  }

  if (formatting.halign == HAlign::Right) {
    float string_width = GetStringWidth(text, formatting.font_size);
    pen_.x -= std::min(string_width, formatting.max_size > 0 ? formatting.max_size : FLT_MAX);
  }

  ftgl::vec2 out_screen_pos;
  ftgl::vec2 out_screen_size;
  ftgl::texture_font_t* font = GetFont(formatting.font_size);
  if (font == nullptr) {
    return;
  }

  int line_count = GetStringLineCount(text);
  int first_line_height = GetStringHeightScreenSpace(text, formatting.font_size);
  float total_height = line_count == 1 ? first_line_height : font->height * line_count;

  switch (formatting.valign) {
    case VAlign::Top:
      pen_.y += GetStringHeightScreenSpace(text, formatting.font_size);
      break;
    case VAlign::Bottom:
      pen_.y = pen_.y + total_height - first_line_height;
      break;
    case VAlign::Middle:
      pen_.y += total_height / 2;
      break;
  }
  AddTextInternal(text, &pen_, formatting, z, &out_screen_pos, &out_screen_size);

  if (out_text_pos) {
    (*out_text_pos) = viewport_->ScreenToWorld(
        Vec2i(static_cast<int>(out_screen_pos.x), static_cast<int>(out_screen_pos.y)));
  }

  if (out_text_size) {
    *out_text_size = viewport_->ScreenToWorld(
        Vec2i(static_cast<int>(out_screen_size.x), static_cast<int>(out_screen_size.y)));
  }
}

float TextRenderer::AddTextTrailingCharsPrioritized(const char* text, float x, float y, float z,
                                                    TextFormatting formatting,
                                                    size_t trailing_chars_length) {
  if (!initialized_) {
    Init();
  }

  const size_t text_length = strlen(text);
  if (text_length == 0) {
    return 0.f;
  }

  // Early-out: If we can't fit a single char, there's no use to do all the expensive
  // calculations below - this is a major bottleneck in some cases
  if (formatting.max_size >= 0 && GetStringWidth(".", formatting.font_size) > formatting.max_size) {
    return 0.f;
  }

  float temp_pen_x = viewport_->WorldToScreen(Vec2(x, y))[0];
  float max_width =
      formatting.max_size == -1.f ? FLT_MAX : viewport_->WorldToScreen({formatting.max_size, 0})[0];
  float string_width = 0.f;
  int min_x = INT_MAX;
  int max_x = -INT_MAX;

  ftgl::texture_font_t* font = GetFont(formatting.font_size);
  size_t i;
  for (i = 0; i < text_length; ++i) {
    ftgl::texture_glyph_t* glyph = MaybeLoadAndGetGlyph(font, text + i);
    if (glyph != nullptr) {
      float kerning = 0.0f;
      if (i > 0) {
        kerning = texture_glyph_get_kerning(glyph, text + i - 1);
      }
      temp_pen_x += kerning;
      int x0 = static_cast<int>(temp_pen_x + glyph->offset_x);
      int x1 = static_cast<int>(x0 + glyph->width);

      min_x = std::min(min_x, x0);
      max_x = std::max(max_x, x1);
      string_width = float(max_x - min_x);

      if (string_width > max_width) {
        break;
      }

      temp_pen_x += glyph->advance_x;
    }
  }

  // TODO: Technically, we'd want the size of "... <TIME>" + remaining
  // characters

  auto fitting_chars_count = i;

  static const char* kEllipsisText = "... ";
  static const size_t kEllipsisTextLen = strlen(kEllipsisText);
  static const size_t kLeadingCharsCount = 1;
  static const size_t kEllipsisBufferSize = kEllipsisTextLen + kLeadingCharsCount;

  bool use_ellipsis_text = (fitting_chars_count < text_length) &&
                           (fitting_chars_count > (trailing_chars_length + kEllipsisBufferSize));

  if (!use_ellipsis_text) {
    AddText(text, x, y, z, formatting);
    return GetStringWidth(text, formatting.font_size);
  }

  auto leading_char_count = fitting_chars_count - (trailing_chars_length + kEllipsisTextLen);

  std::string modified_text(text, leading_char_count);
  modified_text.append(kEllipsisText);

  auto time_position = text_length - trailing_chars_length;
  modified_text.append(&text[time_position], trailing_chars_length);

  AddText(modified_text.c_str(), x, y, z, formatting);
  return GetStringWidth(modified_text.c_str(), formatting.font_size);
}

float TextRenderer::GetStringWidth(const char* text, uint32_t font_size) {
  return viewport_->ScreenToWorld({GetStringWidthScreenSpace(text, font_size), 0})[0];
}

float TextRenderer::GetStringHeight(const char* text, uint32_t font_size) {
  return viewport_->ScreenToWorld({0, GetStringHeightScreenSpace(text, font_size)})[1];
}

int TextRenderer::GetStringWidthScreenSpace(const char* text, uint32_t font_size) {
  float string_width = 0;

  std::size_t len = strlen(text);
  for (std::size_t i = 0; i < len; ++i) {
    ftgl::texture_font_t* font = GetFont(font_size);
    ftgl::texture_glyph_t* glyph = MaybeLoadAndGetGlyph(font, text + i);
    if (glyph != nullptr) {
      float kerning = 0.0f;
      if (i > 0) {
        kerning = texture_glyph_get_kerning(glyph, text + i - 1);
      }

      string_width += kerning;
      string_width += glyph->advance_x;
    }

    // Only return width of first line.
    if (text[i] == '\n') break;
  }

  return static_cast<int>(ceil(string_width));
}

int TextRenderer::GetStringHeightScreenSpace(const char* text, uint32_t font_size) {
  int max_height = 0.f;
  ftgl::texture_font_t* font = GetFont(font_size);
  for (std::size_t i = 0; i < strlen(text); ++i) {
    ftgl::texture_glyph_t* glyph = MaybeLoadAndGetGlyph(font, text + i);
    if (glyph != nullptr) {
      max_height = std::max(max_height, glyph->offset_y);
    }

    // Only return height of first line.
    if (text[i] == '\n') break;
  }
  return max_height;
}

std::vector<float> TextRenderer::GetLayers() const {
  std::vector<float> layers;
  for (auto& [layer, unused_buffer] : vertex_buffers_by_layer_) {
    layers.push_back(layer);
  }
  return layers;
};

void TextRenderer::Clear() {
  pen_.x = 0.f;
  pen_.y = 0.f;
  for (auto& [unused_layer, buffer] : vertex_buffers_by_layer_) {
    vertex_buffer_clear(buffer);
  }
}
