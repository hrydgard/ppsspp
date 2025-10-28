#pragma once

#include "Common/File/Path.h"

class UIContext;

extern Path boot_filename;

void UIBackgroundInit(UIContext &dc);
void UIBackgroundShutdown();
void DrawGameBackground(UIContext &dc, const Path &gamePath, float x, float y, float z);
void DrawBackground(UIContext &dc, float alpha, float x, float y, float z);

uint32_t GetBackgroundColorWithAlpha(const UIContext &dc);
