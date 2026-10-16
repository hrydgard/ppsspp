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
#include "Core/Util/ImageCompressors.h"

// Packs, extracts or repacks the one game shown on the game screen, in the background, through
// the user-supplied libraries (Core/Util/ImageCompressors.h). One game at a time, and never while
// the Un/Pack screen has a job running.
namespace GamePacker {

enum class Container {
	None,  // Nothing we can convert (or no library for it).
	ISO,
	CHD,
	CSO,
	PBP,
};

// What the game at gamePath is stored as, if a library that reads it is loaded. *file is the image
// itself: for a PSP/GAME/<folder> game, its EBOOT.PBP.
Container DetectContainer(const Path &gamePath, Path *file);

struct Option {
	bool toISO = false;  // Extract only.
	ImageFormat format = ImageFormat::CHD;
	int preset = 0;  // Indexed like iCHDPreset, iCSOPreset (MaxCSO::Preset) and iPBPPreset.
	std::string title;
	std::string description;
};

// For an ISO the presets of every loaded library, for anything else Extract to ISO first and then
// the presets it can be repacked to.
std::vector<Option> ListOptions(Container from);

// The option writes to the game's own file name (repacking a CSO as CSO), so the original has to go.
bool MustReplace(const Path &gamePath, const Option &option);

// Whether a pack can start now: no other job, and no game running.
bool CanStart();
bool Start(const Path &gamePath, const Option &option, bool replaceOriginal);

bool IsBusy();
bool IsPacking(const Path &gamePath);
void Abort();

struct Status {
	Path gamePath;
	std::string label;  // "Packing GAME.iso (1/2)"
	float progress = 0.0f;  // Of the current step.
	double etaSeconds = -1.0;  // Of the current step, negative if not known yet.
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

// The preset list the game screen's Pack / Extract / Repack button opens.
class GamePackPopupScreen : public UI::PopupScreen {
public:
	GamePackPopupScreen(const Path &gamePath, GamePacker::Container from, std::function<void()> onStarted);

	const char *tag() const override { return "GamePack"; }
	void CreatePopupContents(UI::ViewGroup *parent) override;

protected:
	bool FillVertical() const override { return options_.size() > 5; }
	UI::Size PopupWidth() const override { return 600; }
	void OnCompleted(DialogResult result) override;

private:
	void Select(int index);

	Path gamePath_;
	std::vector<GamePacker::Option> options_;
	std::vector<UI::View *> choices_;
	UI::CheckBox *replaceCheckBox_ = nullptr;
	std::function<void()> onStarted_;
	int selected_ = 0;
	bool replace_ = false;
	bool replaceWanted_ = false;  // What the user picked, for when a forced choice is undone.
};
