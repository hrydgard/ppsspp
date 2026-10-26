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

#include <cstdint>
#include <memory>

#include "Common/Math/geom2d.h"

class UIContext;
struct TouchInput;
class ImposeArt;

// The PSP's HOME screen ("Do you want to quit the game?"), which impose.prx draws over a running
// game. Owned by EmuScreen, which feeds it input and frames and draws it over the PSP display.
// See ImposeHome.cpp for how it follows the firmware, and the firmware art it uses when there is
// a firmware dump.
class ImposeHomeScreen {
public:
	ImposeHomeScreen();
	~ImposeHomeScreen();

	// What EmuScreen has to do after an update.
	enum class Action {
		NONE,
		LEAVE_GAME,  // Quit to the game browser, like Exit to menu.
	};

	// Once a frame. running: the game is running (booted, CPU running, no reboot pending).
	// onTop: EmuScreen is the top screen, so the HOME screen may take input.
	Action Update(bool running, bool onTop);
	// Touch input while open. True if it took it.
	bool Touch(const TouchInput &touch);
	// screen: where the PSP's 480x272 display is, in UI coordinates.
	void Draw(UIContext &dc, const Bounds &screen);
	bool IsOpen() const { return state_ != State::CLOSED; }
	void Close();
	void DeviceLost();

private:
	enum class State {
		CLOSED,
		ASKING,    // Yes / No.
		QUITTING,  // "Please wait...", while the game's exit callback runs.
	};

	void Open();
	void Choose(bool quit);
	void FinishQuit();
	Action TakeAction();

	State state_ = State::CLOSED;
	bool yes_ = false;  // Which option is highlighted.
	uint32_t lastButtons_ = 0;
	// After choosing Yes, while the game's exit callback runs: when to stop waiting and quit anyway.
	double deadline_ = -1.0;
	Action pendingAction_ = Action::NONE;
	// Where Yes and No were last drawn, for touch.
	Bounds yesBounds_{};
	Bounds noBounds_{};
	// The firmware's own artwork, when a firmware dump is installed.
	std::unique_ptr<ImposeArt> art_;
};
