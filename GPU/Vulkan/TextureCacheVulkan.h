// Copyright (c) 2015- PPSSPP Project.

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

#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/GPUState.h"

class TextureCacheVulkan : public TextureCacheCommon {
public:
	bool SetOffsetTexture(u32 offset) override {
		return false;
	}
	bool DecodeTexture(u8 *dest, const GPUgstate &state) {
		return false;
	}

	bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0) override {
		return false;
	}
	void DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer) override {

	}

	void DownloadFramebufferForClut(u32 clutAddr, u32 bytes) override {

	}
};
