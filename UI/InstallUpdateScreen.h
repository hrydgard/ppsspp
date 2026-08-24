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

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "Common/File/Path.h"
#include "Common/UI/Notice.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Core/Util/PSARUnpack.h"

#include "UI/BaseScreens.h"
#include "UI/SimpleDialogScreen.h"

// An official PSP firmware updater (a PSP/GAME/UPDATE/EBOOT.PBP, or the folder holding one).
// Running it isn't going to get anyone anywhere, but the firmware inside it is exactly what the
// emulated flash0/flash1 want, so offer to unpack it into the NAND directory instead.
class InstallUpdateScreen : public UISimpleBaseDialogScreen {
public:
	// title is the updater's SFO title, which already carries the version ("PSP Update ver 6.61").
	InstallUpdateScreen(const Path &path, std::string_view title);

	void CreateDialogViews(UI::ViewGroup *parent) override;
	void update() override;
	bool key(const KeyInput &key) override;

	const char *tag() const override { return "InstallUpdate"; }

protected:
	std::string_view GetTitle() const override;

private:
	// The unpack runs on a worker thread, and the user can leave the screen while it's going, so
	// the state it writes is shared with the task rather than owned by the screen.
	struct InstallState {
		std::atomic<float> progress{};
		std::atomic<bool> done{};
		// Only valid to read once done is set.
		bool success = false;
		PSARUnpackStats stats;
		std::string error;
	};

	void StartInstall();
	void RefreshStatus();

	Path path_;
	Path destination_;
	std::string title_;
	u64 fileSize_ = 0;
	bool overwrites_ = false;

	std::shared_ptr<InstallState> state_;
	bool reportedDone_ = false;

	UI::Choice *installChoice_ = nullptr;
	UI::ProgressBar *progressBar_ = nullptr;
	NoticeView *resultView_ = nullptr;
};
