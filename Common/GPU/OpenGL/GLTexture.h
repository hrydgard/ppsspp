#pragma once

// Textures
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/MiscTypes.h"
#include "Common/GPU/thin3d.h"

class GLRTexturePool;

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

private:
	GLRTexturePool *texPool_ = nullptr;

	// Ugh, but the best option here.
	friend class GLRTexturePool;
};

struct PooledTexture {
	GLuint texture;
	uint16_t w;
	uint16_t h;
};

// Managed by queuerunner, everything is done from GL submit thread.
class GLRTexturePool {
public:
	~GLRTexturePool();

	// Called from GL submit thread
	void Allocate(GLRTexture *texture);
	void MoveToPoolFromDestructor(GLRTexture *texture);

	void Clear();

private:
	std::vector<PooledTexture> pool_;
};
