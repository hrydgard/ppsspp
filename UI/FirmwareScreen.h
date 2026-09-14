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

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"

#include "Core/Util/PSARUnpack.h"

#include "UI/SimpleDialogScreen.h"

// What's in PSP/NAND - which can be nothing at all, just the fonts we pulled off some game's
// disc, or a full firmware unpacked from an updater. Also the place to launch the XMB from,
// since that's what a full firmware buys you.
class FirmwareScreen : public UITwoPaneBaseDialogScreen {
public:
	FirmwareScreen(const Path &gamePath)
		: UITwoPaneBaseDialogScreen(gamePath, TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll) {}

	const char *tag() const override { return "Firmware"; }

protected:
	std::string_view GetTitle() const override;
	void BeforeCreateViews() override;
	void CreateSettingsViews(UI::ViewGroup *parent) override;
	void CreateContentViews(UI::ViewGroup *parent) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

private:
	void LaunchVSH();
	void BrowseForUpdater();
	void AskToErase();

	Path nandRoot_;
	InstalledFirmwareInfo info_;
};
