// Copyright (c) 2013- PPSSPP Project.

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

#include "Common/CommonTypes.h"

enum TextureFiltering {
	TEX_FILTER_AUTO = 1,
	TEX_FILTER_NEAREST = 2,
	TEX_FILTER_LINEAR = 3,
	TEX_FILTER_LINEAR_VIDEO = 4,
};

struct VirtualFramebuffer;

class TextureCacheCommon {
public:
	virtual ~TextureCacheCommon();

	virtual bool SetOffsetTexture(u32 offset);

	int AttachedDrawingHeight();

	// Wow this is starting to grow big. Soon need to start looking at resizing it.
	// Must stay a POD.
	struct TexCacheEntry {
		// After marking STATUS_UNRELIABLE, if it stays the same this many frames we'll trust it again.
		const static int FRAMES_REGAIN_TRUST = 1000;

		enum Status {
			STATUS_HASHING = 0x00,
			STATUS_RELIABLE = 0x01,        // Don't bother rehashing.
			STATUS_UNRELIABLE = 0x02,      // Always recheck hash.
			STATUS_MASK = 0x03,

			STATUS_ALPHA_UNKNOWN = 0x04,
			STATUS_ALPHA_FULL = 0x00,      // Has no alpha channel, or always full alpha.
			STATUS_ALPHA_SIMPLE = 0x08,    // Like above, but also has 0 alpha (e.g. 5551.)
			STATUS_ALPHA_MASK = 0x0c,

			STATUS_CHANGE_FREQUENT = 0x10, // Changes often (less than 15 frames in between.)
			STATUS_CLUT_RECHECK = 0x20,    // Another texture with same addr had a hashfail.
			STATUS_DEPALETTIZE = 0x40,     // Needs to go through a depalettize pass.
			STATUS_TO_SCALE = 0x80,        // Pending texture scaling in a later frame.
		};

		// Status, but int so we can zero initialize.
		int status;
		u32 addr;
		u32 hash;
		VirtualFramebuffer *framebuffer;  // if null, not sourced from an FBO.
		u32 sizeInRAM;
		int lastFrame;
		int numFrames;
		int numInvalidated;
		u32 framesUntilNextFullHash;
		u8 format;
		u8 maxLevel;
		u16 dim;
		u16 bufw;
		union {
			u32 textureName;
			void *texturePtr;
		};
		int invalidHint;
		u32 fullhash;
		u32 cluthash;
		float lodBias;
		u16 maxSeenV;

		// Cache the current filter settings so we can avoid setting it again.
		// (OpenGL madness where filter settings are attached to each texture).
		u8 magFilt;
		u8 minFilt;
		bool sClamp;
		bool tClamp;

		Status GetHashStatus() {
			return Status(status & STATUS_MASK);
		}
		void SetHashStatus(Status newStatus) {
			status = (status & ~STATUS_MASK) | newStatus;
		}
		Status GetAlphaStatus() {
			return Status(status & STATUS_ALPHA_MASK);
		}
		void SetAlphaStatus(Status newStatus) {
			status = (status & ~STATUS_ALPHA_MASK) | newStatus;
		}
		void SetAlphaStatus(Status newStatus, int level) {
			// For non-level zero, only set more restrictive.
			if (newStatus == STATUS_ALPHA_UNKNOWN || level == 0) {
				SetAlphaStatus(newStatus);
			} else if (newStatus == STATUS_ALPHA_SIMPLE && GetAlphaStatus() == STATUS_ALPHA_FULL) {
				SetAlphaStatus(STATUS_ALPHA_SIMPLE);
			}
		}
		bool Matches(u16 dim2, u8 format2, u8 maxLevel2);
	};

protected:
	void GetSamplingParams(int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, u8 maxLevel);

	TexCacheEntry *nextTexture_;
};

inline bool TextureCacheCommon::TexCacheEntry::Matches(u16 dim2, u8 format2, u8 maxLevel2) {
	return dim == dim2 && format == format2 && maxLevel == maxLevel2;
}
