#pragma once

#include "Common/File/Path.h"
#include "Common/Math/lin/vec3.h"

class UIContext;

extern Path boot_filename;

void UIBackgroundInit(UIContext &dc);
void UIBackgroundShutdown();
void DrawGameBackground(UIContext &dc, const Path &gamePath, Lin::Vec3 focus, float alpha);
void DrawBackground(UIContext &dc, float alpha, Lin::Vec3 focus);

uint32_t GetBackgroundColorWithAlpha(const UIContext &dc);
