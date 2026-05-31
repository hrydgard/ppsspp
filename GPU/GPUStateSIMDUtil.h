#pragma once

#include "Common/Math/CrossSIMD.h"
#include "GPU/GPUState.h"

inline Vec4F32 GetViewportOffsetVec(const GPUgstate &gstate) {
	return Vec4F32::LoadF24x3_DontCare(&gstate.viewportxcenter);
}
inline Vec4F32 GetViewportScaleVec(const GPUgstate &gstate) {
	return Vec4F32::LoadF24x3_DontCare(&gstate.viewportxscale);
}
