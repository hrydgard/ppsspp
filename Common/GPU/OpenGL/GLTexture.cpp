#include "Common/GPU/OpenGL/GLTexture.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/math_util.h"
#include "Common/Log.h"

GLRTexture::GLRTexture(const Draw::DeviceCaps &caps, int width, int height, int depth, int _numMips)
	: w(width), h(height), d(depth), numMips(_numMips) {
	if (caps.textureNPOTFullySupported) {
		canWrap = true;
	} else {
		canWrap = isPowerOf2(width) && isPowerOf2(height);
	}
}

GLRTexture::~GLRTexture() {
	if (texPool_) {
		texPool_->MoveToPoolFromDestructor(this);
	} else {
		glDeleteTextures(1, &texture);
	}
}

#define SMALL_TEX_SIDE 32

GLRTexturePool::~GLRTexturePool() {
	_dbg_assert_(pool_.empty());
}

void GLRTexturePool::Create(GLRTexture *newTexture) {
	_dbg_assert_(!newTexture->texture);
	if (newTexture->w * newTexture->h <= SMALL_TEX_SIDE * SMALL_TEX_SIDE) {
		// Mark for recycling later.
		newTexture->texPool_ = this;

		// We'll grab a same-sized handle from the pool if available.
		// Loop backwards to make the erase cheaper.
		for (auto p = pool_.rbegin(), end = pool_.rend(); p != end; p++) {
			if (p->w == newTexture->w && p->h == newTexture->h) {
				// Match, perfect. Remove from pool.
				newTexture->texture = p->texture;
				// Strange idiom, but that's what you have to do to erase a reverse iterator.
				pool_.erase(std::next(p).base());
				return;
			}
		}

		// Didn't find a perfect match. Try for imperfect.
		if (!newTexture->texture && !pool_.empty()) {
			// Just grab the last entry in the pool.
			auto p = pool_.back();
			newTexture->texture = p.texture;
			pool_.pop_back();
			return;
		}

		// Didn't find anything to reuse, fall through.
	}

	// We simply need a new handle.
	glGenTextures(1, &newTexture->texture);
}

void GLRTexturePool::MoveToPoolFromDestructor(GLRTexture *texture) {
	pool_.push_back(PooledTexture{
		texture->texture,
		texture->w,
		texture->h,
	});
}

void GLRTexturePool::Clear() {
	for (auto &p : pool_) {
		glDeleteTextures(1, &p.texture);
	}
	pool_.clear();
}
