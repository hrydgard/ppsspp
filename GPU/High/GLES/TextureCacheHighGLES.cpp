#include "GPU/High/GLES/TextureCacheHighGLES.h"

namespace HighGpu {

int TextureCacheGLES::NumLoadedTextures() {
	return 0;
}

bool TextureCacheGLES::Clear(bool x) {
	return false;
}

void TextureCacheGLES::StartFrame() {

}

void TextureCacheGLES::Invalidate(u32 addr, int size, GPUInvalidationType type) {

}

void TextureCacheGLES::InvalidateAll(GPUInvalidationType type) {

}

void TextureCacheGLES::ForgetLastTexture() {

}

void TextureCacheGLES::NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg) {

}

u32 TextureCacheGLES::AllocTextureName() {
	return 0;
}

CachedTexture *TextureCacheGLES::GetTexture(TextureState *texState, ClutState *clutState) {
	return nullptr;
}

}  // namespace
