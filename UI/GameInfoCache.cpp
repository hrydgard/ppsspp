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

#include <string>
#include <map>

#include "base/timeutil.h"
#include "base/stringutil.h"
#include "image/png_load.h"
#include "GameInfoCache.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"

GameInfoCache g_gameInfoCache;

GameInfoCache::~GameInfoCache()
{
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		delete iter->second;
	}
}

static bool ReadFileToString(IFileSystem *fs, const char *filename, std::string *contents)
{
	PSPFileInfo info = fs->GetFileInfo(filename);
	if (!info.exists) {
		contents->clear();
		return false;
	}

	int handle = fs->OpenFile(filename, FILEACCESS_READ);
	if (!handle) {
		contents->clear();
		return false;
	}

	contents->resize(info.size);
	fs->ReadFile(handle, (u8 *)contents->data(), info.size);
	fs->CloseFile(handle);
	return true;
}

void GameInfoCache::Save()
{
	// TODO
}

void GameInfoCache::Load() {
	// TODO
}

void GameInfoCache::Decimate() {
	// TODO
}

void GameInfoCache::FlushBGs() {
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		lock_guard lock(iter->second->lock);
		if (!iter->second->pic0TextureData.empty()) {
			iter->second->pic0TextureData.clear();
		}
		if (iter->second->pic0Texture) {
			delete iter->second->pic0Texture;
			iter->second->pic0Texture = 0;
		}
		if (!iter->second->pic1TextureData.empty()) {
			iter->second->pic1TextureData.clear();
		}
		if (iter->second->pic1Texture) {
			delete iter->second->pic1Texture;
			iter->second->pic1Texture = 0;
		}
	}
}

// This may run off-main-thread and we thus can't use the global
// pspFileSystem (well, we could with synchronization but there might not
// even be a game running).
GameInfo *GameInfoCache::GetInfo(const std::string &gamePath, bool wantBG) {
	auto iter = info_.find(gamePath);
	if (iter != info_.end()) {
		GameInfo *info = iter->second;
		if (!info->wantBG && wantBG) {
			// Need to start over.
			delete info;
			goto again;
		}
		if (info->iconTextureData.size()) {
			info->iconTexture = new Texture();
			// TODO: We could actually do the PNG decoding as well on the async thread.
			// We'd have to split up Texture->LoadPNG though, creating some intermediate Image class maybe.
			if (info->iconTexture->LoadPNG((const u8 *)info->iconTextureData.data(), info->iconTextureData.size(), false)) {
				info->timeIconWasLoaded = time_now_d();
			}
			info->iconTextureData.clear();
		}
		if (info->pic0TextureData.size()) {
			info->pic0Texture = new Texture();
			if (info->pic0Texture->LoadPNG((const u8 *)info->pic0TextureData.data(), info->pic0TextureData.size(), false)) {
				info->timePic0WasLoaded = time_now_d();
			}
			info->pic0TextureData.clear();
		}
		if (info->pic1TextureData.size()) {
			info->pic1Texture = new Texture();
			if (info->pic1Texture->LoadPNG((const u8 *)info->pic1TextureData.data(), info->pic1TextureData.size(), false)) {
				info->timePic1WasLoaded = time_now_d();
			}
			info->pic1TextureData.clear();
		}
		iter->second->lastAccessedTime = time_now_d();
		return iter->second;
	}

again:

	// return info;

	// TODO: Everything below here should be asynchronous and run on a thread,
	// filling in the info as it goes.

	// A game can be either an UMD or a directory under ms0:/PSP/GAME .
	if (startsWith(gamePath, "ms0:/PSP/GAME")) {
		return 0;
	// TODO: The case of these extensions is not perfect.
	} else if (endsWith(gamePath, ".PBP") || endsWith(gamePath, ".elf")) {
		return 0;
	} else {
		SequentialHandleAllocator handles;
		// Let's assume it's an ISO.
		// TODO: This will currently read in the whole directory tree. Not really necessary for just a
		// few files.
		BlockDevice *bd = constructBlockDevice(gamePath.c_str());
		if (!bd)
			return 0;  // nothing to do here..
		ISOFileSystem umd(&handles, bd, "/PSP_GAME");

		GameInfo *info = new GameInfo();
		info->wantBG = wantBG;

		// Alright, let's fetch the PARAM.SFO.
		std::string paramSFOcontents;
		if (ReadFileToString(&umd, "/PSP_GAME/PARAM.SFO", &paramSFOcontents)) {
			lock_guard lock(info->lock);
			info->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
			info->title = info->paramSFO.GetValueString("TITLE");
		}

		ReadFileToString(&umd, "/PSP_GAME/ICON0.PNG", &info->iconTextureData);
		if (wantBG) {
			{
				lock_guard lock(info->lock);
				ReadFileToString(&umd, "/PSP_GAME/PIC0.PNG", &info->pic0TextureData);
			}
			{
				lock_guard lock(info->lock);
				ReadFileToString(&umd, "/PSP_GAME/PIC1.PNG", &info->pic1TextureData);
			}
		}
		info_[gamePath] = info;
		return info;
	}

	return 0;
}
