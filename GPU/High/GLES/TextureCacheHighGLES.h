#pragma once

#include "GPU/ge_constants.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/GPUInterface.h"

namespace HighGpu {

class TextureCacheGLES : public TextureCacheCommon {
public:
	int NumLoadedTextures();
	bool Clear(bool x);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
};

}  // namespace
