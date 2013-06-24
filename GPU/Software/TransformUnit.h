// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "CommonTypes.h"
#include "../Math3D.h"

typedef u16 fixed16;
typedef u16 u10; // TODO: erm... :/

typedef Vec3<float> ModelCoords;
typedef Vec3<float> WorldCoords;
typedef Vec3<float> ViewCoords;
typedef Vec4<float> ClipCoords; // Range: -w <= x/y/z <= w

struct ScreenCoords
{
	fixed16 x;
	fixed16 y;
	u16 z;
};

typedef Vec2<u10> DrawingCoords; // TODO: Keep z component?

class TransformUnit
{
	WorldCoords ModelToWorld(const ModelCoords& coords);
	ViewCoords WorldToView(const WorldCoords& coords);
	ClipCoords ViewToClip(const ViewCoords& coords);
	ScreenCoords ClipToScreen(const ClipCoords& coords);
	DrawingCoords ScreenToDrawing(const ScreenCoords& coords);
};
