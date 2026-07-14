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
#include "UI/BaseScreens.h"
#include "Core/Slang/SlangPresetLibrary.h"
#include "UI/MiscViews.h"

class SlangShaderScreen : public UIBaseDialogScreen {
public:
	explicit SlangShaderScreen(const Path &gamePath) : UIBaseDialogScreen(gamePath) {}
	void CreateViews() override;
	bool key(const KeyInput &input) override;
	const char *tag() const override { return "SlangShader"; }
private:
	void ActivatePreset(const Path &presetPath);
	void Deactivate();
	SlangPresetLibrary library_;
	ViewSearch search_{};
	UI::ViewGroup *listContainer_ = nullptr;
};
