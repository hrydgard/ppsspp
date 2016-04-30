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

#include "ext/xxhash.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/TextureReplacer.h"

TextureReplacer::TextureReplacer() : enabled_(false) {
}

TextureReplacer::~TextureReplacer() {
}

void TextureReplacer::Init() {
	NotifyConfigChanged();
}

void TextureReplacer::NotifyConfigChanged() {
	gameID_ = g_paramSFO.GetValueString("DISC_ID");

	enabled_ = !gameID_.empty() && (g_Config.bReplaceTextures || g_Config.bSaveNewTextures);
	if (enabled_) {
		basePath_ = GetSysDirectory(DIRECTORY_TEXTURES) + gameID_ + "/";

		// If we're saving, auto-create the directory.
		if (g_Config.bSaveNewTextures && !File::Exists(basePath_)) {
			File::CreateFullPath(basePath_);
		}

		enabled_ = File::Exists(basePath_) && File::IsDirectory(basePath_);
	}

	// TODO: Load ini file.
}

u32 TextureReplacer::ComputeHash(u32 addr, int bufw, int w, int h, GETextureFormat fmt, u16 maxSeenV) {
	_dbg_assert_msg_(G3D, enabled_, "Replacement not enabled");

	if (!LookupHashRange(addr, w, h)) {
		// There wasn't any hash range, let's fall back to maxSeenV logic.
		if (h == 512 && maxSeenV < 512 && maxSeenV != 0) {
			h = (int)maxSeenV;
		}
	}

	// TODO: In order to have the most stable hash possible, skip space between w/bufw?
	// TODO: Use hash based on ini file, or always crc32c, or etc.
	const u32 sizeInRAM = (textureBitsPerPixel[fmt] * bufw * h) / 8;
	const u32 *checkp = (const u32 *)Memory::GetPointer(addr);
	return DoQuickTexHash(checkp, sizeInRAM);
}

ReplacedTexture TextureReplacer::FindReplacement(u32 hash) {
	_assert_msg_(G3D, enabled_, "Replacement not enabled");

	ReplacedTexture result;
	result.alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;

	// Only actually replace if we're replacing.  We might just be saving.
	if (g_Config.bReplaceTextures) {
		// TODO
	}
	return result;
}

void TextureReplacer::NotifyTextureDecoded(u32 hash, const void *data, int pitch, int w, int h, ReplacedTextureFormat fmt) {
	_assert_msg_(G3D, enabled_, "Replacement not enabled");
	if (!g_Config.bSaveNewTextures) {
		// Ignore.
		return;
	}

	// TODO
}

bool TextureReplacer::LookupHashRange(u32 addr, int &w, int &h) {
	// TODO: Pull from table loaded via ini.
	return false;
}

void ReplacedTexture::Load(int level, void *out, int rowPitch) {
	_assert_msg_(G3D, (size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(G3D, out != nullptr && rowPitch > 0, "Invalid out/pitch");

	// TODO
}
