#include "Common/GPU/OpenGL/GLTexture.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/math_util.h"

GLRTexture::GLRTexture(const Draw::DeviceCaps &caps, int width, int height, int depth, int numMips) {
	if (caps.textureNPOTFullySupported) {
		canWrap = true;
	} else {
		canWrap = isPowerOf2(width) && isPowerOf2(height);
	}
	w = width;
	h = height;
	d = depth;
	this->numMips = numMips;
}

GLRTexture::~GLRTexture() {
	if (texture) {
		glDeleteTextures(1, &texture);
	}
}
