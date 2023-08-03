#pragma once

#include "Common/UI/Context.h"
#include "Core/ConfigValues.h"
#include "Core/ControlMapper.h"

void DrawControlMapperOverlay(UIContext *ctx, const Bounds &bounds, const ControlMapper &controlMapper);
void DrawDebugOverlay(UIContext *ctx, const Bounds &bounds, DebugOverlay overlay);
