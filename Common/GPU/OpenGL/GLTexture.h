#pragma once

// Textures

#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/MiscTypes.h"
#include "Common/GPU/thin3d.h"

class GLRTexture {
public:
	GLRTexture(const Draw::DeviceCaps &caps, int width, int height, int depth, int numMips);
	~GLRTexture();

	GLuint texture = 0;
	uint16_t w;
	uint16_t h;
	uint16_t d;

	// We don't trust OpenGL defaults - setting wildly off values ensures that we'll end up overwriting these parameters.
	GLenum target = 0xFFFF;
	GLenum wrapS = 0xFFFF;
	GLenum wrapT = 0xFFFF;
	GLenum magFilter = 0xFFFF;
	GLenum minFilter = 0xFFFF;
	uint8_t numMips = 0;
	bool canWrap = true;
	float anisotropy = -100000.0f;
	float minLod = -1000.0f;
	float maxLod = 1000.0f;
	float lodBias = 0.0f;
};
