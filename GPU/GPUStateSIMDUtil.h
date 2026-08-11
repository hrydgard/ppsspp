#pragma once

#include "Common/Math/CrossSIMD.h"
#include "GPU/GPUState.h"

inline Vec4F32 LoadViewportOffsetVec(const GEState &gstate) {
	// We ignore the last member.
	return Vec4F32::LoadF24x4(&gstate.viewportxcenter);
}
inline Vec4F32 LoadViewportScaleVec(const GEState &gstate) {
	// We ignore the last member.
	return Vec4F32::LoadF24x4(&gstate.viewportxscale);
}
inline UVScale LoadUVScaleOffset(const GEState &gstate) {
	UVScale uvScale;
	Vec4F32::LoadF24x4(&gstate.texscaleu).Store(&uvScale.uScale);
	return uvScale;
}
