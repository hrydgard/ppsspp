// Copyright (c) 2016- PPSSPP Project.

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

#include "GPU/Common/TextureReplacer.h"

TextureReplacer::TextureReplacer() : enabled_(false) {
}

TextureReplacer::~TextureReplacer() {
}


void TextureReplacer::Init() {
}

void TextureReplacer::NotifyConfigChanged() {
}

u32 TextureReplacer::ComputeHash(u32 addr, int bufw, int w, int h, GETextureFormat fmt, u16 maxSeenV) {
	return 0;
}

ReplacedTexture TextureReplacer::FindReplacement(u32 hash) {
	ReplacedTexture result;
	result.alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
	return result;
}

void TextureReplacer::NotifyTextureDecoded(u32 hash, const void *data, int pitch, int w, int h, ReplacedTextureFormat fmt) {
}

void ReplacedTexture::Load(int level, void *out, int rowPitch) {
}
