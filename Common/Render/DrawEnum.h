#pragma once

#include <cstdint>
#include "Common/Common.h"

enum {
	ALIGN_LEFT = 0,
	ALIGN_RIGHT = 16,
	ALIGN_TOP = 0,
	ALIGN_BOTTOM = 1,
	ALIGN_HCENTER = 4,
	ALIGN_VCENTER = 8,
	ALIGN_VBASELINE = 32,	// text only, possibly not yet working

	ALIGN_CENTER = ALIGN_HCENTER | ALIGN_VCENTER,
	ALIGN_TOPLEFT = ALIGN_TOP | ALIGN_LEFT,
	ALIGN_TOPRIGHT = ALIGN_TOP | ALIGN_RIGHT,
	ALIGN_BOTTOMLEFT = ALIGN_BOTTOM | ALIGN_LEFT,
	ALIGN_BOTTOMRIGHT = ALIGN_BOTTOM | ALIGN_RIGHT,

	// For "uncachable" text like debug log.
	// Avoids using system font drawing as it's too slow.
	// Not actually used here but is reserved for whatever system wraps DrawBuffer.
	FLAG_DYNAMIC_ASCII = 2048,
	FLAG_NO_PREFIX = 4096,  // means to not process ampersands
	FLAG_WRAP_TEXT = 8192,
	FLAG_ELLIPSIZE_TEXT = 16384,
	FLAG_DROP_SHADOW = 32768,  // soft shadow under text (system font drawing only)
};

// Rounded rectangles are drawn from antialiased corner images that UI/UIAtlas.cpp generates
// into the UI atlas, one per radius and style.
enum class RoundRectStyle : uint8_t {
	Filled,
	OutlineThin,
	OutlineMedium,
	OutlineThick,
	COUNT,
};

// Corner radii in UI units, each of which gets an image per style. Other radii scale the nearest one.
// Keep them larger than the thickest outline.
constexpr float ROUND_RECT_RADII[] = {8.0f, 16.0f, 24.0f, 32.0f};
constexpr int ROUND_RECT_RADIUS_COUNT = ARRAY_SIZE(ROUND_RECT_RADII);
// Outline thickness in UI units, per RoundRectStyle.
constexpr float ROUND_RECT_THICKNESS[(int)RoundRectStyle::COUNT] = {0.0f, 1.0f, 2.0f, 3.0f};

// Which corners of a round rect to round. The others are drawn square.
enum class RoundCorners : uint8_t {
	None = 0,
	TopLeft = 1,
	TopRight = 2,
	BottomLeft = 4,
	BottomRight = 8,
	Top = 3,  // TopLeft | TopRight
	Bottom = 12,  // BottomLeft | BottomRight
	Left = 5,  // TopLeft | BottomLeft
	Right = 10,  // TopRight | BottomRight
	All = 15,
};
ENUM_CLASS_BITOPS(RoundCorners);
