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

}  // namespace
