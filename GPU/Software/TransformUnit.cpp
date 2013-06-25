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

#include "TransformUnit.h"
#include "../GPUState.h"

WorldCoords TransformUnit::ModelToWorld(const ModelCoords& coords)
{
	Mat3x3<float> world_matrix(gstate.worldMatrix);
	return WorldCoords(world_matrix * coords) + Vec3<float>(gstate.worldMatrix[9], gstate.worldMatrix[10], gstate.worldMatrix[11]);
}

ViewCoords TransformUnit::WorldToView(const WorldCoords& coords)
{
	Mat3x3<float> view_matrix(gstate.viewMatrix);
	return ViewCoords(view_matrix * coords) + Vec3<float>(gstate.viewMatrix[9], gstate.viewMatrix[10], gstate.viewMatrix[11]);
}

ClipCoords TransformUnit::ViewToClip(const ViewCoords& coords)
{
	Vec4<float> coords4(coords.x, coords.y, coords.z, 1.0f);
	Mat4x4<float> projection_matrix(gstate.projMatrix);
	return ClipCoords(projection_matrix * coords4);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords& coords)
{
	ScreenCoords ret;
	float vpx1 = getFloat24(gstate.viewportx1);
	float vpx2 = getFloat24(gstate.viewportx2);
	float vpy1 = getFloat24(gstate.viewporty1);
	float vpy2 = getFloat24(gstate.viewporty2);
	float vpz1 = getFloat24(gstate.viewportz1);
	float vpz2 = getFloat24(gstate.viewportz2);
	float viewport_dx = vpx2 - vpx1; // TODO: -1?
	float viewport_dy = vpy2 - vpy1; // TODO: -1?
	float viewport_dz = vpz2 - vpz1; // TODO: -1?
	// TODO: Check for invalid parameters (x2 < x1, etc)

	ret.x = (coords.x * viewport_dx / coords.w + vpx1) * 0xFFFF;
	ret.y = (coords.y * viewport_dy / coords.w + vpy1) * 0xFFFF;
	ret.z = (coords.z * viewport_dz / coords.w + vpz1) * 0xFFFF;
	return ret;
}

DrawingCoords TransformUnit::ScreenToDrawing(const ScreenCoords& coords)
{
	DrawingCoords ret;
	ret.x = (coords.x - gstate.offsetx) & 0x3ff;
	ret.y = (coords.y - gstate.offsety) & 0x3ff;
	return ret;
}
