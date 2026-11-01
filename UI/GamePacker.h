// Copyright (c) 2012- PPSSPP Project.

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

#include <functional>
#include <string>
#include <vector>

#include "Common/File/Path.h"
#include "Common/UI/PopupScreens.h"
#include "Core/FileSystems/CSOCompress.h"

// Compresses the game shown on the game screen as CSO, or extracts or recompresses it, in the
// background (Core/FileSystems/CSOCompress.h does the work). One game at a time.
namespace GamePacker {

enum class Container {
	None,  // Nothing we convert.
	ISO,
	CSO,
	CHD,
	PBP,  // A PSN EBOOT.PBP holding a UMD image.
};

// What the game at gamePath is stored as. *file is the image itself: for a PSP/GAME/<folder>
// game, its EBOOT.PBP.
Container DetectContainer(const Path &gamePath, Path *file);

struct Option {
	bool toISO = false;  // Extract only.
	CSOCompress::Preset preset = CSOCompress::Preset::Best;
	std::string title;
	std::string description;
};

// For an ISO the CSO presets, for anything else Extract to ISO first and then the presets.
std::vector<Option> ListOptions(Container from);

// Whether a pack can start now: no other job, and no game running.
bool CanStart();
// Unless keepOriginal, the game's file is deleted once the new one is written. A name that's
// taken (by the original, when it's kept, or by another file) gets a number added.
bool Start(const Path &gamePath, const Option &option, bool keepOriginal);

bool IsBusy();
bool IsPacking(const Path &gamePath);
void Abort();

struct Status {
	Path gamePath;
	std::string label;  // "Compressing GAME.iso"
	float progress = 0.0f;
	double etaSeconds = -1.0;  // Negative if not known yet.
	bool aborting = false;
};
bool GetStatus(Status *status);

struct Result {
	Path oldPath;
	Path newPath;  // Where the game is now: oldPath unless the original was replaced.
	bool success = false;
};
// Bumped when a finished job has been wrapped up, so screens know to look at LastResult().
int Generation();
Result LastResult();

// Wraps up a finished job (replacing the original, updating recents). Call every frame.
void UpdateOnUIThread();
// Stops a running job and waits for it, leaving no partial output.
void Shutdown();

}  // namespace GamePacker

// The preset list the game screen's "Compress game" opens.
class GamePackPopupScreen : public UI::PopupScreen {
public:
	GamePackPopupScreen(const Path &gamePath, GamePacker::Container from);

	const char *tag() const override { return "GamePack"; }
	void CreatePopupContents(UI::ViewGroup *parent) override;

protected:
	UI::Size PopupWidth() const override { return 600; }
	void OnCompleted(DialogResult result) override;

private:
	void Select(int index);

	Path gamePath_;
	std::vector<GamePacker::Option> options_;
	std::vector<UI::View *> choices_;
	int selected_ = 0;
	bool keepOriginal_ = false;
};
