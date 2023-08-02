#pragma once

#include "Common/UI/Context.h"
#include "Core/ConfigValues.h"
#include "Core/ControlMapper.h"

void DrawDebugOverlay(UIContext *ctx, const Bounds &bounds, const ControlMapper &controlMapper, DebugOverlay overlay);
