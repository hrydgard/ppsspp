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

#include "base/mutex.h"
#include "file/file_util.h"
#include "thread/prioritizedworkqueue.h"
#include "gfx/texture.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Loaders.h"

// A GameInfo holds information about a game, and also lets you do things that the VSH
// does on the PSP, namely checking for and deleting savedata, and similar things.
// Only cares about games that are installed on the current device.

// Guessed from GameID, not necessarily accurate
enum GameRegion {
	GAMEREGION_JAPAN,
	GAMEREGION_USA,
	GAMEREGION_EUROPE,
	GAMEREGION_HONGKONG,
	GAMEREGION_ASIA,
	GAMEREGION_OTHER,
	GAMEREGION_MAX,
};

enum GameInfoWantFlags {
	GAMEINFO_WANTBG = 0x01,
	GAMEINFO_WANTSIZE = 0x02,
	GAMEINFO_WANTSND = 0x04,
};

// TODO: Need to fix c++11 still on Symbian and use std::atomic<bool> instead.
class CompletionFlag {
public:
	CompletionFlag() : pending(1) {
	}

	void SetDone() {
#if defined(_WIN32)
		_WriteBarrier();
		pending = 0;
#else
		__sync_lock_release(&pending);
#endif
	}

	bool IsDone() {
		const bool done = pending == 0;
#if defined(_WIN32)
		_ReadBarrier();
#else
		__sync_synchronize();
#endif
		return done;
	}

	CompletionFlag &operator =(const bool &v) {
		pending = v ? 0 : 1;
		return *this;
	}

	operator bool() {
		return IsDone();
	}

private:
	volatile u32 pending;

	DISALLOW_COPY_AND_ASSIGN(CompletionFlag);
};

class GameInfo {
public:
	GameInfo()
		: disc_total(0), disc_number(0), region(-1), fileType(FILETYPE_UNKNOWN), paramSFOLoaded(false),
		  iconTexture(NULL), pic0Texture(NULL), pic1Texture(NULL), wantFlags(0),
		  timeIconWasLoaded(0.0), timePic0WasLoaded(0.0), timePic1WasLoaded(0.0),
		  gameSize(0), saveDataSize(0), installDataSize(0) {}

	bool DeleteGame();  // Better be sure what you're doing when calling this.
	bool DeleteAllSaveData();

	u64 GetGameSizeInBytes();
	u64 GetSaveDataSizeInBytes();
	u64 GetInstallDataSizeInBytes();

	void ParseParamSFO();

	std::vector<std::string> GetSaveDataDirectories();


	// Hold this when reading or writing from the GameInfo.
	// Don't need to hold it when just passing around the pointer,
	// and obviously also not when creating it and holding the only pointer
	// to it.
	recursive_mutex lock;

	FileInfo fileInfo;
	std::string path;
	std::string title;  // for easy access, also available in paramSFO.
	std::string id;
	std::string id_version;
	int disc_total;
	int disc_number;
	int region;
	IdentifiedFileType fileType;
	ParamSFOData paramSFO;
	bool paramSFOLoaded;

	// Pre read the data, create a texture the next time (GL thread..)
	std::string iconTextureData;
	Texture *iconTexture;
	std::string pic0TextureData;
	Texture *pic0Texture;
	std::string pic1TextureData;
	Texture *pic1Texture;

	std::string sndFileData;

	int wantFlags;

	double lastAccessedTime;

	// The time at which the Icon and the BG were loaded.
	// Can be useful to fade them in smoothly once they appear.
	double timeIconWasLoaded;
	double timePic0WasLoaded;
	double timePic1WasLoaded;

	CompletionFlag iconDataLoaded;
	CompletionFlag pic0DataLoaded;
	CompletionFlag pic1DataLoaded;
	CompletionFlag sndDataLoaded;

	u64 gameSize;
	u64 saveDataSize;
	u64 installDataSize;
};

class GameInfoCache {
public:
	GameInfoCache() : gameInfoWQ_(0) {}
	~GameInfoCache();

	// This creates a background worker thread!
	void Init();
	void Shutdown();
	void Clear();

	// All data in GameInfo including iconTexture may be zero the first time you call this
	// but filled in later asynchronously in the background. So keep calling this,
	// redrawing the UI often. Only set flags to GAMEINFO_WANTBG or WANTSND if you really want them 
	// because they're big. bgTextures and sound may be discarded over time as well.
	GameInfo *GetInfo(const std::string &gamePath, int wantFlags);
	void Decimate();  // Deletes old info.
	void FlushBGs();  // Gets rid of all BG textures. Also gets rid of bg sounds.

	// TODO - save cache between sessions
	void Save();
	void Load();

private:
	void SetupTexture(GameInfo *info, std::string &textureData, Texture *&tex, double &loadTime);

	// Maps ISO path to info.
	std::map<std::string, GameInfo *> info_;

	// Work queue and management
	PrioritizedWorkQueue *gameInfoWQ_;
};

// This one can be global, no good reason not to.
extern GameInfoCache g_gameInfoCache;
