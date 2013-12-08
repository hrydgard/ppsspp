// Copyright (c) 2012- PPSSPP Project.

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

#include "../Globals.h"
#include "helper/global.h"
#include "helper/fbo.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/Directx9/TextureScalerDX9.h"

namespace DX9 {

struct VirtualFramebufferDX9;

enum TextureFiltering {
	AUTO = 1,
	NEAREST = 2,
	LINEAR = 3,   
	LINEARFMV = 4,
};

enum FramebufferNotification {
	NOTIFY_FB_CREATED,
	NOTIFY_FB_UPDATED,
	NOTIFY_FB_DESTROYED,
};

class TextureCacheDX9 
{
public:
	TextureCacheDX9();
	~TextureCacheDX9();

	void SetTexture(bool t = false);

	void Clear(bool delete_them);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();
	void LoadClut();

	// FramebufferManager keeps TextureCache updated about what regions of memory
	// are being rendered to. This is barebones so far.
	void NotifyFramebuffer(u32 address, VirtualFramebufferDX9 *framebuffer, FramebufferNotification msg);

	size_t NumLoadedTextures() const {
		return cache.size();
	}

	// Only used by Qt UI?
	bool DecodeTexture(u8 *output, GPUgstate state);

private:
	// Wow this is starting to grow big. Soon need to start looking at resizing it.
	// Must stay a POD.
	struct TexCacheEntry {
		// After marking STATUS_UNRELIABLE, if it stays the same this many frames we'll trust it again.
		const static int FRAMES_REGAIN_TRUST = 1000;

		enum Status {
			STATUS_HASHING = 0x00,
			STATUS_RELIABLE = 0x01,  // cache, don't hash
			STATUS_UNRELIABLE = 0x02,  // never cache
			STATUS_MASK = 0x03,

			STATUS_ALPHA_UNKNOWN = 0x04,
			STATUS_ALPHA_FULL = 0x00,  // Has no alpha channel, or always full alpha.
			STATUS_ALPHA_SIMPLE = 0x08,  // Like above, but also has 0 alpha (e.g. 5551.)
			STATUS_ALPHA_MASK = 0x0c,
		};

		// Status, but int so we can zero initialize.
		int status;
		u32 addr;
		u32 hash;
		VirtualFramebufferDX9 *framebuffer;  // if null, not sourced from an FBO.
		u32 sizeInRAM;
		int lastFrame;
		int numFrames;
		int numInvalidated;
		u32 framesUntilNextFullHash;
		u8 format;
		u16 dim;
		u16 bufw;
		LPDIRECT3DTEXTURE9 texture;  //GLuint
		int invalidHint;
		u32 fullhash;
		u32 cluthash;
		int maxLevel;
		float lodBias;

		// Cache the current filter settings so we can avoid setting it again.
		// (OpenGL madness where filter settings are attached to each texture).
		u8 magFilt;
		u8 minFilt;
		bool sClamp;
		bool tClamp;

		bool Matches(u16 dim2, u8 format2, int maxLevel2);
	};

	void Decimate();  // Run this once per frame to get rid of old textures.
	void *UnswizzleFromMem(u32 texaddr, u32 bufw, u32 bytesPerPixel, u32 level);
	void *ReadIndexedTex(int level, u32 texaddr, int bytesPerIndex, u32 dstFmt, int bufw);
	void UpdateSamplingParams(TexCacheEntry &entry, bool force);
	void LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages);
	void *DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, u32 &dstFmt);
	void CheckAlpha(TexCacheEntry &entry, u32 *pixelData, u32 dstFmt, int w, int h);
	template <typename T>
	const T *GetCurrentClut();
	u32 GetCurrentClutHash();
	void UpdateCurrentClut();
	void AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebufferDX9 *framebuffer, bool exactMatch);
	void DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebufferDX9 *framebuffer);
	void SetTextureFramebuffer(TexCacheEntry *entry);

	TexCacheEntry *GetEntryAt(u32 texaddr);

	typedef std::map<u64, TexCacheEntry> TexCache;
	TexCache cache;
	TexCache secondCache;
	std::vector<VirtualFramebufferDX9 *> fbCache_;

	bool clearCacheNextFrame_;
	bool lowMemoryMode_;
	TextureScalerDX9 scaler;

	SimpleBuf<u32> tmpTexBuf32;
	SimpleBuf<u16> tmpTexBuf16;

	SimpleBuf<u32> tmpTexBufRearrange;

	u32 clutLastFormat_;
	u32 *clutBufRaw_;
	u32 *clutBufConverted_;
	u32 *clutBuf_;
	u32 clutHash_;
	u32 clutTotalBytes_;
	// True if the clut is just alpha values in the same order (RGBA4444-bit only.)
	bool clutAlphaLinear_;
	u16 clutAlphaLinearColor_;

	LPDIRECT3DTEXTURE9 lastBoundTexture;
	float maxAnisotropyLevel;

	int decimationCounter_;
};

};
