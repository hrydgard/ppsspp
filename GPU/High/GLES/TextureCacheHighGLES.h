#pragma once

#include "GPU/ge_constants.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/GPUInterface.h"

namespace HighGpu {

struct TextureState;
struct ClutState;

class CachedTexture {

};

class TextureCacheGLES : public TextureCacheCommon {
public:
	int NumLoadedTextures();
	bool Clear(bool x);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ForgetLastTexture() override;
	void NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg) override;
	u32 AllocTextureName() override;

	CachedTexture *GetTexture(TextureState *texState, ClutState *clutState);
};

}  // namespace
