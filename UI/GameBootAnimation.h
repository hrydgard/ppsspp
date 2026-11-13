// Copyright (c) 2026- PPSSPP Project.

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

#include <memory>
#include <string>

#include "Common/Math/geom2d.h"

class UIContext;
class PMFVideoTexture;

// The PSP's game start intro, which the XMB plays before it starts a game: the firmware's
// flash0:/vsh/resource/gameboot.pmf, a PSMF movie (480x272 video, ATRAC3+ audio in private stream
// 0xBD). EmuScreen plays it while a game boots, when g_Config.bGameBootAnimation is on, for games
// that weren't started from the XMB (those get it from the XMB itself).
class GameBootAnimation {
public:
	GameBootAnimation();
	~GameBootAnimation();

	// Whether there's a gameboot.pmf to play, and a video player to play it.
	static bool Available();

	// Starts it. False if it couldn't be loaded.
	bool Start();
	// Once a frame: ends it when it's over.
	void Update();
	bool IsPlaying() const { return playing_; }
	// Ends it early.
	void Stop();
	// Over everything, in black, with the movie where the PSP's screen is.
	void Draw(UIContext &dc, const Bounds &screen);
	void DeviceLost();

private:
	std::string file_;
	std::unique_ptr<PMFVideoTexture> video_;
	double startTime_ = 0.0;
	double duration_ = 0.0;
	bool playing_ = false;
};
