// Copyright (c) 2014- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "thin3d/thin3d.h"
#include "gfx/gl_debug_log.h"
#include "Core/Config.h"
#include "GPU/GLES/FragmentTestCacheGLES.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"

// These are small, let's give them plenty of frames.
static const int FRAGTEST_TEXTURE_OLD_AGE = 307;
static const int FRAGTEST_DECIMATION_INTERVAL = 113;

FragmentTestCacheGLES::FragmentTestCacheGLES(Draw::DrawContext *draw) {
	render_ = (GLRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
}

FragmentTestCacheGLES::~FragmentTestCacheGLES() {
	Clear();
}

void FragmentTestCacheGLES::BindTestTexture(int slot) {
	if (!g_Config.bFragmentTestCache) {
		return;
	}

	bool alphaNeedsTexture = gstate.isAlphaTestEnabled() && !IsAlphaTestAgainstZero() && !IsAlphaTestTriviallyTrue();
	bool colorNeedsTexture = gstate.isColorTestEnabled() && !IsColorTestAgainstZero() && !IsColorTestTriviallyTrue();
	if (!alphaNeedsTexture && !colorNeedsTexture) {
		// Common case: testing against zero.  Just skip it, faster not to bind anything.
		return;
	}

	const FragmentTestID id = GenerateTestID();
	const auto cached = cache_.find(id);
	if (cached != cache_.end()) {
		cached->second.lastFrame = gpuStats.numFlips;
		GLRTexture *tex = cached->second.texture;
		if (tex == lastTexture_) {
			// Already bound, hurray.
			return;
		}
		render_->BindTexture(slot, tex);
		lastTexture_ = tex;
		return;
	}

	const u8 rRef = (gstate.getColorTestRef() >> 0) & 0xFF;
	const u8 rMask = (gstate.getColorTestMask() >> 0) & 0xFF;
	const u8 gRef = (gstate.getColorTestRef() >> 8) & 0xFF;
	const u8 gMask = (gstate.getColorTestMask() >> 8) & 0xFF;
	const u8 bRef = (gstate.getColorTestRef() >> 16) & 0xFF;
	const u8 bMask = (gstate.getColorTestMask() >> 16) & 0xFF;
	const u8 aRef = gstate.getAlphaTestRef();
	const u8 aMask = gstate.getAlphaTestMask();
	const u8 refs[4] = {rRef, gRef, bRef, aRef};
	const u8 masks[4] = {rMask, gMask, bMask, aMask};
	const GEComparison funcs[4] = {gstate.getColorTestFunction(), gstate.getColorTestFunction(), gstate.getColorTestFunction(), gstate.getAlphaTestFunction()};
	const bool valid[4] = {gstate.isColorTestEnabled(), gstate.isColorTestEnabled(), gstate.isColorTestEnabled(), gstate.isAlphaTestEnabled()};

	GLRTexture *tex = CreateTestTexture(funcs, refs, masks, valid);
	lastTexture_ = tex;
	render_->BindTexture(slot, tex);
	FragmentTestTexture item;
	item.lastFrame = gpuStats.numFlips;
	item.texture = tex;
	cache_[id] = item;
}

FragmentTestID FragmentTestCacheGLES::GenerateTestID() const {
	FragmentTestID id;
	// Let's just keep it simple, it's all in here.
	id.alpha = gstate.isAlphaTestEnabled() ? gstate.alphatest : 0;
	if (gstate.isColorTestEnabled()) {
		id.colorRefFunc = gstate.getColorTestFunction() | (gstate.getColorTestRef() << 8);
		id.colorMask = gstate.getColorTestMask();
	} else {
		id.colorRefFunc = 0;
		id.colorMask = 0;
	}
	return id;
}

GLRTexture *FragmentTestCacheGLES::CreateTestTexture(const GEComparison funcs[4], const u8 refs[4], const u8 masks[4], const bool valid[4]) {
	u8 *data = new u8[256 * 4];
	// TODO: Might it be better to use GL_ALPHA for simple textures?
	// TODO: Experiment with 4-bit/etc. textures.

	// Build the logic map.
	for (int color = 0; color < 256; ++color) {
		for (int i = 0; i < 4; ++i) {
			bool res = true;
			if (valid[i]) {
				switch (funcs[i]) {
				case GE_COMP_NEVER:
					res = false;
					break;
				case GE_COMP_ALWAYS:
					res = true;
					break;
				case GE_COMP_EQUAL:
					res = (color & masks[i]) == (refs[i] & masks[i]);
					break;
				case GE_COMP_NOTEQUAL:
					res = (color & masks[i]) != (refs[i] & masks[i]);
					break;
				case GE_COMP_LESS:
					res = (color & masks[i]) < (refs[i] & masks[i]);
					break;
				case GE_COMP_LEQUAL:
					res = (color & masks[i]) <= (refs[i] & masks[i]);
					break;
				case GE_COMP_GREATER:
					res = (color & masks[i]) > (refs[i] & masks[i]);
					break;
				case GE_COMP_GEQUAL:
					res = (color & masks[i]) >= (refs[i] & masks[i]);
					break;
				}
			}
			data[color * 4 + i] = res ? 0xFF : 0;
		}
	}

	GLRTexture *tex = render_->CreateTexture(GL_TEXTURE_2D);
	render_->TextureImage(tex, 0, 256, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, data);
	return tex;
}

void FragmentTestCacheGLES::Clear(bool deleteThem) {
	if (deleteThem) {
		for (auto tex = cache_.begin(); tex != cache_.end(); ++tex) {
			render_->DeleteTexture(tex->second.texture);
		}
	}
	cache_.clear();
	lastTexture_ = 0;
}

void FragmentTestCacheGLES::Decimate() {
	if (--decimationCounter_ <= 0) {
		for (auto tex = cache_.begin(); tex != cache_.end(); ) {
			if (tex->second.lastFrame + FRAGTEST_TEXTURE_OLD_AGE < gpuStats.numFlips) {
				render_->DeleteTexture(tex->second.texture);
				cache_.erase(tex++);
			} else {
				++tex;
			}
		}

		decimationCounter_ = FRAGTEST_DECIMATION_INTERVAL;
	}

	lastTexture_ = 0;
}
