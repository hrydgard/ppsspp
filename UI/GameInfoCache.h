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

#include "file/file_util.h"
#include "Core/ELF/ParamSFO.h"
#include "UI/TextureUtil.h"

namespace Draw {
	class DrawContext;
	class Texture;
}
class PrioritizedWorkQueue;

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
	GAMEREGION_OTHER,
	GAMEREGION_MAX,
};

enum GameInfoWantFlags {
	GAMEINFO_WANTBG = 0x01,
	GAMEINFO_WANTSIZE = 0x02,
	GAMEINFO_WANTSND = 0x04,
	GAMEINFO_WANTBGDATA = 0x08, // Use with WANTBG.
};

class FileLoader;
enum class IdentifiedFileType;

struct GameInfoTex {
	GameInfoTex() {}
	~GameInfoTex() {
		if (texture) {
			ELOG("LEAKED GameInfoTex");
		}
	}
	std::string data;
	std::unique_ptr<ManagedTexture> texture;
	// The time at which the Icon and the BG were loaded.
	// Can be useful to fade them in smoothly once they appear.
	double timeLoaded = 0.0;
	std::atomic<bool> dataLoaded{};

	void Clear() {
		if (!data.empty()) {
			data.clear();
			dataLoaded = false;
		}
		texture.reset(nullptr);
	}
private:
	DISALLOW_COPY_AND_ASSIGN(GameInfoTex);
};

class GameInfo {
public:
	GameInfo();
	~GameInfo();

	bool Delete();  // Better be sure what you're doing when calling this.
	bool DeleteAllSaveData();
	bool LoadFromPath(const std::string &gamePath);

	std::shared_ptr<FileLoader> GetFileLoader();
	void DisposeFileLoader();

	u64 GetGameSizeInBytes();
	u64 GetSaveDataSizeInBytes();
	u64 GetInstallDataSizeInBytes();

	void ParseParamSFO();

	std::vector<std::string> GetSaveDataDirectories();

	std::string GetTitle();
	void SetTitle(const std::string &newTitle);

	// Hold this when reading or writing from the GameInfo.
	// Don't need to hold it when just passing around the pointer,
	// and obviously also not when creating it and holding the only pointer
	// to it.
	std::mutex lock;

	std::string id;
	std::string id_version;
	int disc_total = 0;
	int disc_number = 0;
	int region = -1;
	IdentifiedFileType fileType;
	ParamSFOData paramSFO;
	bool paramSFOLoaded = false;
	bool hasConfig = false;

	// Pre read the data, create a texture the next time (GL thread..)
	GameInfoTex icon;
	GameInfoTex pic0;
	GameInfoTex pic1;

	std::string sndFileData;
	std::atomic<bool> sndDataLoaded{};

	int wantFlags = 0;

	double lastAccessedTime = 0.0;

	u64 gameSize = 0;
	u64 saveDataSize = 0;
	u64 installDataSize = 0;
	std::atomic<bool> pending{};
	std::atomic<bool> working{};

protected:
	// Note: this can change while loading, use GetTitle().
	std::string title;

	std::shared_ptr<FileLoader> fileLoader;
	std::string filePath_;

private:
	DISALLOW_COPY_AND_ASSIGN(GameInfo);
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
	std::shared_ptr<GameInfo> GetInfo(Draw::DrawContext *draw, const std::string &gamePath, int wantFlags);
	void FlushBGs();  // Gets rid of all BG textures. Also gets rid of bg sounds.

	PrioritizedWorkQueue *WorkQueue() { return gameInfoWQ_; }

	void CancelAll();
	void WaitUntilDone(std::shared_ptr<GameInfo> &info);

private:
	void Init();
	void Shutdown();
	void SetupTexture(std::shared_ptr<GameInfo> &info, Draw::DrawContext *draw, GameInfoTex &tex);

	// Maps ISO path to info. Need to use shared_ptr as we can return these pointers - 
	// and if they get destructed while being in use, that's bad.
	std::map<std::string, std::shared_ptr<GameInfo> > info_;

	// Work queue and management
	PrioritizedWorkQueue *gameInfoWQ_;
};

// This one can be global, no good reason not to.
extern GameInfoCache *g_gameInfoCache;
