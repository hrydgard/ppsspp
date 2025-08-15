#pragma once

#include "Common/File/Path.h"
#include "Common/UI/Context.h"
#include "Core/ConfigValues.h"
#include "Core/ControlMapper.h"

void DrawControlMapperOverlay(UIContext *ctx, const Bounds &bounds, const ControlMapper &controlMapper);
void DrawDebugOverlay(UIContext *ctx, const Bounds &bounds, DebugOverlay overlay);
void DrawCrashDump(UIContext *ctx, const Path &gamePath);
void DrawFPS(UIContext *ctx, const Bounds &bounds);
