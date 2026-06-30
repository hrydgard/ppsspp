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
inline Vec4F32 LoadUVScaleOffsetVec(const GEState &gstate) {
	return Vec4F32::LoadF24x4(&gstate.texscaleu);
}
