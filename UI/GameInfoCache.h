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

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>

#include "Common/Thread/Event.h"
#include "Core/ELF/ParamSFO.h"
#include "Common/File/Path.h"

namespace Draw {
	class DrawContext;
	class Texture;
}

// A GameInfo holds information about a game, and also lets you do things that the VSH
// does on the PSP, namely checking for and deleting savedata, and similar things.
// Only cares about games that are installed on the current device.

// A GameInfo object can also represent a piece of savedata.

// Guessed from GameID, not necessarily accurate
enum GameRegion {
	GAMEREGION_JAPAN,
	GAMEREGION_USA,
	GAMEREGION_EUROPE,
	GAMEREGION_HONGKONG,
	GAMEREGION_ASIA,
	GAMEREGION_KOREA,
	GAMEREGION_OTHER,
	GAMEREGION_MAX,
};

enum class GameInfoFlags {
	FILE_TYPE = 0x01,  // Don't need to specify this, always included.
	PARAM_SFO = 0x02,
	ICON = 0x04,
	BG = 0x08,
	SND = 0x10,
	SIZE = 0x20,
	UNCOMPRESSED_SIZE = 0x40,
};
ENUM_CLASS_BITOPS(GameInfoFlags);

class FileLoader;
enum class IdentifiedFileType;

struct GameInfoTex {
	std::string data;
	Draw::Texture *texture = nullptr;
	// The time at which the Icon and the BG were loaded.
	// Can be useful to fade them in smoothly once they appear.
	// Also, timeLoaded != 0 && texture == nullptr means that the load failed.
	double timeLoaded = 0.0;
	std::atomic<bool> dataLoaded{};

	// Can ONLY be called from the main thread!
	void Clear();
	bool Failed() const {
		return timeLoaded != 0.0 && !texture;
	}
};

class GameInfo {
public:
	GameInfo(const Path &gamePath);
	~GameInfo();

	bool Delete();  // Better be sure what you're doing when calling this.
	bool DeleteAllSaveData();
	bool CreateLoader();

	bool HasFileLoader() const {
		return fileLoader.get() != nullptr;
	}

	std::shared_ptr<FileLoader> GetFileLoader();
	void DisposeFileLoader();

	u64 GetSizeUncompressedInBytes();  // NOTE: More expensive than GetGameSizeOnDiskInBytes().
	u64 GetSizeOnDiskInBytes();
	u64 GetGameSavedataSizeInBytes();  // For games
	u64 GetInstallDataSizeInBytes();

	// For various kinds of savedata, mainly.
	// NOTE: This one actually performs I/O directly, not cached.
	std::string GetMTime() const;

	void ParseParamSFO();
	const ParamSFOData &GetParamSFO() const {
		_dbg_assert_(hasFlags & GameInfoFlags::PARAM_SFO);
		return paramSFO;
	}
	void FinishPendingTextureLoads(Draw::DrawContext *draw);

	std::vector<Path> GetSaveDataDirectories();

	std::string GetTitle();
	void SetTitle(const std::string &newTitle);

	const Path &GetFilePath() const {
		return filePath_;
	}

	bool Ready(GameInfoFlags flags) {
		std::unique_lock<std::mutex> guard(lock);
		// Avoid the operator, we want to check all the bits.
		return ((int)hasFlags & (int)flags) == (int)flags;
	}

	void MarkReadyNoLock(GameInfoFlags flags) {
		hasFlags |= flags;
		pendingFlags &= ~flags;
	}

	GameInfoTex *GetBGPic() {
		if (pic1.texture)
			return &pic1;
		if (pic0.texture)
			return &pic0;
		return nullptr;
	}

	// Hold this when reading or writing from the GameInfo.
	// Don't need to hold it when just passing around the pointer,
	// and obviously also not when creating it and holding the only pointer
	// to it.
	std::mutex lock;

	// Controls access to the fileLoader pointer.
	std::mutex loaderLock;

	// Keep track of what we have, or what we're processing.
	// These are protected by the mutex. While pendingFlags != 0, something is being loaded.
	GameInfoFlags hasFlags{};
	GameInfoFlags pendingFlags{};

	std::string id;
	std::string id_version;
	int disc_total = 0;
	int disc_number = 0;
	int region = -1;
	IdentifiedFileType fileType;
	bool hasConfig = false;

	// Pre read the data, create a texture the next time (GL thread..)
	GameInfoTex icon;
	GameInfoTex pic0;
	GameInfoTex pic1;

	std::string sndFileData;
	std::atomic<bool> sndDataLoaded{};

	double lastAccessedTime = 0.0;

	u64 gameSizeUncompressed = 0;
	u64 gameSizeOnDisk = 0;  // compressed size, in case of CSO
	u64 saveDataSize = 0;
	u64 installDataSize = 0;

protected:
	ParamSFOData paramSFO;
	// Note: this can change while loading, use GetTitle().
	std::string title;

	// TODO: Get rid of this shared_ptr and managae lifetime better instead.
	std::shared_ptr<FileLoader> fileLoader;
	Path filePath_;

	void SetupTexture(Draw::DrawContext *draw, GameInfoTex &tex);

private:
	DISALLOW_COPY_AND_ASSIGN(GameInfo);
	friend class GameInfoWorkItem;
};

class GameInfoCache {
public:
	GameInfoCache();
	~GameInfoCache();

	// This creates a background worker thread!
	void Clear();
	void PurgeType(IdentifiedFileType fileType);

	// All data in GameInfo including icon.texture may be zero the first time you call this
	// but filled in later asynchronously in the background. So keep calling this,
	// redrawing the UI often. Only set flags to GAMEINFO_WANTBG or WANTSND if you really want them 
	// because they're big. bgTextures and sound may be discarded over time as well.
	// NOTE: This never returns null, so you don't need to check for that. Do check Ready() flags though.
	std::shared_ptr<GameInfo> GetInfo(Draw::DrawContext *draw, const Path &gamePath, GameInfoFlags wantFlags);
	void FlushBGs();  // Gets rid of all BG textures. Also gets rid of bg sounds.

	void CancelAll();
	void WaitUntilDone(std::shared_ptr<GameInfo> &info);

private:
	void Init();
	void Shutdown();

	// Maps ISO path to info. Need to use shared_ptr as we can return these pointers - 
	// and if they get destructed while being in use, that's bad.
	std::map<std::string, std::shared_ptr<GameInfo> > info_;
	std::mutex mapLock_;
};

// This one can be global, no good reason not to.
extern GameInfoCache *g_gameInfoCache;
