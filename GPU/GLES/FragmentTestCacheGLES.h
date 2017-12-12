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

#pragma once

#include <map>
#include "Common/CommonTypes.h"
#include "GPU/GLES/TextureCacheGLES.h"

#include "GPU/ge_constants.h"

struct FragmentTestID {
	union {
		struct {
			u32 alpha;
			u32 colorRefFunc;
			u32 colorMask;
		};
		u32 d[3];
	};

	bool operator < (const FragmentTestID &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++) {
			if (d[i] < other.d[i])
				return true;
			if (d[i] > other.d[i])
				return false;
		}
		return false;
	}
	bool operator == (const FragmentTestID &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++) {
			if (d[i] != other.d[i])
				return false;
		}
		return true;
	}
};

struct FragmentTestTexture {
	GLRTexture *texture;
	int lastFrame;
};

class FragmentTestCacheGLES {
public:
	FragmentTestCacheGLES(Draw::DrawContext *draw);
	~FragmentTestCacheGLES();

	void SetTextureCache(TextureCacheGLES *tc) {
		textureCache_ = tc;
	}

	void BindTestTexture(GLenum unit);

	void Clear(bool deleteThem = true);
	void Decimate();

private:

	GLRTexture *CreateTestTexture(const GEComparison funcs[4], const u8 refs[4], const u8 masks[4], const bool valid[4]);
	FragmentTestID GenerateTestID() const;

	GLRenderManager *render_;
	TextureCacheGLES *textureCache_;

	std::map<FragmentTestID, FragmentTestTexture> cache_;
	u8 *scratchpad_ = nullptr;
	GLRTexture *lastTexture_ = nullptr;
	int decimationCounter_ = 0;
};
