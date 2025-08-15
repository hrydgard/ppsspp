#pragma once

#include "ext/imgui/imgui.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Input/InputState.h"
#include "Common/Math/geom2d.h"

class Path;

ImGuiKey KeyCodeToImGui(InputKeyCode keyCode);

void ImGui_ImplPlatform_Init(const Path &configPath);
void ImGui_ImplPlatform_NewFrame();

void ImGui_ImplPlatform_KeyEvent(const KeyInput &key);
void ImGui_ImplPlatform_TouchEvent(const TouchInput &touch);
void ImGui_ImplPlatform_AxisEvent(const AxisInput &axis);

extern Bounds g_imguiCentralNodeBounds;

ImGuiMouseCursor ImGui_ImplPlatform_GetCursor();
