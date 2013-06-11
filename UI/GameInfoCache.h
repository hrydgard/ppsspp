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

class GameInfo {
public:
	GameInfo() 
		: fileType(FILETYPE_UNKNOWN), paramSFOLoaded(false), iconTexture(NULL), pic0Texture(NULL), pic1Texture(NULL),
		  wantBG(false), gameSize(0), saveDataSize(0) {}

	bool DeleteGame();  // Better be sure what you're doing when calling this.
	bool DeleteAllSaveData();

	u64 GetGameSizeInBytes();
	u64 GetSaveDataSizeInBytes();

	void LoadParamSFO();

	std::vector<std::string> GetSaveDataDirectories();


	// Hold this when reading or writing from the GameInfo.
	// Don't need to hold it when just passing around the pointer,
	// and obviously also not when creating it and holding the only pointer
	// to it.
	recursive_mutex lock;

	FileInfo fileInfo;
	std::string title;  // for easy access, also available in paramSFO.
	std::string id;
	std::string id_version;
	EmuFileType fileType;
	ParamSFOData paramSFO;
	bool paramSFOLoaded;
	
	// Pre read the data, create a texture the next time (GL thread..)
	std::string iconTextureData;
	Texture *iconTexture;
	std::string pic0TextureData;
	Texture *pic0Texture;
	std::string pic1TextureData;
	Texture *pic1Texture;

	bool wantBG;

	double lastAccessedTime;

	// The time at which the Icon and the BG were loaded.
	// Can be useful to fade them in smoothly once they appear.
	double timeIconWasLoaded;
	double timePic0WasLoaded;
	double timePic1WasLoaded;

	u64 gameSize;
	u64 saveDataSize;
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
	// redrawing the UI often. Only set wantBG if you really want it because
	// it's big. bgTextures may be discarded over time as well.
	GameInfo *GetInfo(const std::string &gamePath, bool wantBG);
	void Decimate();  // Deletes old info.
	void FlushBGs();  // Gets rid of all BG textures.

	// TODO - save cache between sessions
	void Save();
	void Load();

	void Add(const std::string &key, GameInfo *info_);

private:
	// Maps ISO path to info.
	std::map<std::string, GameInfo *> info_;

	// Work queue and management
	PrioritizedWorkQueue *gameInfoWQ_;
};

// This one can be global, no good reason not to.
extern GameInfoCache g_gameInfoCache;
