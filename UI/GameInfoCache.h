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
#include "gfx/texture.h"
#include "Core/ELF/ParamSFO.h"

struct GameInfo {
	GameInfo() : iconTexture(NULL), bgTexture(NULL) {}
	// Hold this when reading or writing from the GameInfo.
	// Don't need to hold it when just passing around the pointer,
	// and obviously also not when creating it and holding the only pointer
	// to it.
	recursive_mutex lock;

	std::string title;  // for easy access, also available in paramSFO.
	ParamSFOData paramSFO;

	// Pre read the data, create a texture the next time (GL thread..)
	std::string iconTextureData;
	Texture *iconTexture;
	std::string bgTextureData;
	Texture *bgTexture;

	double lastAccessedTime;

	// The time at which the Icon and the BG were loaded.
	// Can be useful to fade them in smoothly once they appear.
	double timeIconWasLoaded;
	double timeBgWasLoaded;
};

class GameInfoCache {
public:
	~GameInfoCache();

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

private:
	// Maps ISO path to info.
	std::map<std::string, GameInfo *> info_;
};

// This one can be global, no good reason not to.
extern GameInfoCache g_gameInfoCache;