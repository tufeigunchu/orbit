// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// ImGui GLFW binding with OpenGL
// You can copy and use unmodified imgui_impl_* files in your project. See
// main.cpp for an example of using this. If you use this binding you'll need to
// call 4 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXXX_NewFrame(),
// ImGui::Render() and ImGui_ImplXXXX_Shutdown(). If you are new to ImGui, see
// examples/README.txt and documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

#include <imgui.h>
#include <stdint.h>

#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#define IMGUI_VAR_TO_TEXT(var) orbit_gl::VariableToImGuiText(#var, var)
#define IMGUI_VARN_TO_TEXT(var, name) orbit_gl::VariableToImGuiText(name, var)

#define IMGUI_FLOAT_SLIDER(x) IMGUI_FLOAT_SLIDER_MIN_MAX(x, 0, 100.f)
#define IMGUI_FLOAT_SLIDER_MIN_MAX(x, min, max) ImGui::SliderFloat(#x, &x, min, max)

namespace orbit_gl {

template <class T>
inline void VariableToImGuiText(std::string_view name, const T& value) {
  std::stringstream string_stream{};
  string_stream << name << " = " << value;
  ImGui::Text("%s", string_stream.str().c_str());
}

}  // namespace orbit_gl

class GlCanvas;

extern ImFont* GOrbitImguiFont;

IMGUI_API bool Orbit_ImGui_Init(uint32_t font_size);
IMGUI_API void Orbit_ImGui_Shutdown();
IMGUI_API void Orbit_ImGui_NewFrame(GlCanvas* a_Canvas);

// GLFW callbacks (installed by default if you enable 'install_callbacks' during
// initialization) Provided here if you want to chain callbacks. You can also
// handle inputs yourself and use those as a reference.
IMGUI_API void Orbit_ImGui_MouseButtonCallback(ImGuiContext* context, int button, bool down);
IMGUI_API void Orbit_ImGui_ScrollCallback(ImGuiContext* context, int scroll);
IMGUI_API void Orbit_ImGui_KeyCallback(ImGuiContext* context, int key, bool down, bool ctrl,
                                       bool shift, bool alt);
IMGUI_API void Orbit_ImGui_CharCallback(ImGuiContext* context, unsigned int c);

void Orbit_ImGui_RenderDrawLists(ImDrawData* draw_data);

struct ScopeImguiContext {
  explicit ScopeImguiContext(ImGuiContext* a_State) : m_ImGuiContext(nullptr) {
    ImGuiContext* state = static_cast<ImGuiContext*>(ImGui::GetCurrentContext());
    if (state != a_State) {
      m_ImGuiContext = state;
      ImGui::SetCurrentContext(a_State);
    }
  }
  ~ScopeImguiContext() {
    if (m_ImGuiContext) {
      ImGui::SetCurrentContext(m_ImGuiContext);
    }
  }

  ImGuiContext* m_ImGuiContext;
};

// Usage:
//  static ExampleAppLog my_log;
//  my_log.AddLine(absl::StrFormat("Hello %d world\n", 123));
//  my_log.Draw("title");
struct DebugWindow {
  ImGuiTextBuffer Buf;
  ImGuiTextFilter Filter;
  ImVector<int> LineOffsets;  // Index to lines offset
  bool ScrollToBottom;

  void Clear() {
    Buf.clear();
    LineOffsets.clear();
  }

  void AddLine(const std::string& str) {
    int old_size = Buf.size();
    Buf.append(str.c_str());
    Buf.append("\n");
    for (int new_size = Buf.size(); old_size < new_size; old_size++)
      if (Buf[old_size] == '\n') LineOffsets.push_back(old_size);
  }

  void Draw(const char* title, bool* p_opened = nullptr) {
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin(title, p_opened);
    if (ImGui::Button("Clear")) Clear();
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy");
    ImGui::SameLine();
    Filter.Draw("Filter", -100.0f);
    ImGui::Separator();
    ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (copy) ImGui::LogToClipboard();

    if (Filter.IsActive()) {
      const char* buf_begin = Buf.begin();
      const char* line = buf_begin;
      for (int line_no = 0; line != nullptr; line_no++) {
        const char* line_end =
            (line_no < LineOffsets.Size) ? buf_begin + LineOffsets[line_no] : nullptr;
        if (Filter.PassFilter(line, line_end)) ImGui::TextUnformatted(line, line_end);
        line = line_end && line_end[1] ? line_end + 1 : nullptr;
      }
    } else {
      ImGui::TextUnformatted(Buf.begin());
    }

    if (ScrollToBottom) ImGui::SetScrollHereY(1.0f);
    ScrollToBottom = false;

    ImGui::EndChild();
    ImGui::End();
  }
};

class LogWindow {
 public:
  LogWindow() {}
  ImGuiTextFilter Filter;
  bool ScrollToBottom = false;
  bool m_Open = false;

  void Draw(const char* title, const std::vector<std::string>& lines, bool* p_opened = nullptr) {
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin(title, p_opened);

    bool copy = ImGui::Button("Copy");
    ImGui::SameLine();
    Filter.Draw("Filter", -100.0f);
    ImGui::Separator();
    ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (copy) ImGui::LogToClipboard();

    if (Filter.IsActive()) {
      for (const std::string& line : lines) {
        if (Filter.PassFilter(line.c_str())) ImGui::TextUnformatted(line.c_str());
      }
    } else {
      for (const std::string& line : lines) {
        ImGui::TextUnformatted(line.c_str());
      }
    }

    if (ScrollToBottom) ImGui::SetScrollHereY(1.0f);
    ScrollToBottom = false;
    ImGui::EndChild();
    ImGui::End();
  }
};

struct VizWindow {
  ImGuiTextBuffer Buf;
  ImGuiTextFilter Filter;
  ImVector<int> LineOffsets;  // Index to lines offset
  bool ScrollToBottom;
  ImGuiWindowFlags WindowFlags;

  VizWindow() : ScrollToBottom(false), WindowFlags(0) {}

  void Clear() {
    Buf.clear();
    LineOffsets.clear();
  }

  void FitCanvas() {
    WindowFlags |= ImGuiWindowFlags_NoTitleBar;
    // WindowFlags |= ImGuiWindowFlags_ShowBorders;
    WindowFlags |= ImGuiWindowFlags_NoResize;
    WindowFlags |= ImGuiWindowFlags_NoMove;
    // WindowFlags |= ImGuiWindowFlags_NoScrollbar;
    WindowFlags |= ImGuiWindowFlags_NoCollapse;
    // WindowFlags |= ImGuiWindowFlags_MenuBar;
  }

  void Draw(const char* title, bool* p_opened = nullptr, ImVec2* a_Size = nullptr) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (a_Size) {
      ImGui::SetNextWindowPos(ImVec2(10, 10));
      ImVec2 CanvasSize = *a_Size;
      CanvasSize.x -= 20;
      CanvasSize.y -= 20;
      ImGui::SetNextWindowSize(CanvasSize, ImGuiCond_Always);
      ImGui::Begin(title, p_opened, WindowFlags);
    } else {
      ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
      ImGui::Begin(title, p_opened, WindowFlags);
    }

    if (ImGui::Button("Clear")) Clear();
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy");
    ImGui::SameLine();
    Filter.Draw("Filter", -100.0f);
    ImGui::Separator();
    ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (copy) ImGui::LogToClipboard();

    if (Filter.IsActive()) {
      const char* buf_begin = Buf.begin();
      const char* line = buf_begin;
      for (int line_no = 0; line != nullptr; line_no++) {
        const char* line_end =
            (line_no < LineOffsets.Size) ? buf_begin + LineOffsets[line_no] : nullptr;
        if (Filter.PassFilter(line, line_end)) ImGui::TextUnformatted(line, line_end);
        line = line_end && line_end[1] ? line_end + 1 : nullptr;
      }
    } else {
      ImGui::TextUnformatted(Buf.begin());
    }

    if (ScrollToBottom) ImGui::SetScrollHereY(1.0f);
    ScrollToBottom = false;

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar();
  }
};

struct OutputWindow {
  ImGuiTextBuffer Buf;
  ImVector<int> LineOffsets;  // Index to lines offset
  ImGuiWindowFlags WindowFlags;

  OutputWindow() : WindowFlags(0) {}

  void Clear() {
    Buf.clear();
    LineOffsets.clear();
  }
  void AddLine(const std::string& a_String);
  void Draw(const char* title, bool* p_opened = nullptr, ImVec2* a_Size = nullptr);
};
