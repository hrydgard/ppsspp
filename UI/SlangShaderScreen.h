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
#include <string>
#include "UI/BaseScreens.h"
#include "Core/Slang/SlangPresetLibrary.h"
#include "Core/Slang/SlangPackageImporter.h"   // SlangImportState
#include "UI/MiscViews.h"

// Top-level browser: Import/Update button, a "None (disable)" entry, and the list of
// CATEGORIES. Selecting a category pushes a SlangCategoryScreen. Kept short on purpose —
// the (potentially hundreds of) presets live one level down, per category.
class SlangShaderScreen : public UIBaseDialogScreen {
public:
	explicit SlangShaderScreen(const Path &gamePath) : UIBaseDialogScreen(gamePath) {}
	void CreateViews() override;
	void update() override;
	// A pushed SlangCategoryScreen activates a preset then pops back with DR_OK; rebuild so the
	// active-category ✓ marker reflects the new selection.
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	const char *tag() const override { return "SlangShader"; }
private:
	void Deactivate();
	SlangPresetLibrary library_;
	SlangImportState lastImportState_ = SlangImportState::IDLE;
};

// Second level: the presets within one category, with a search box scoped to that category.
// Selecting a preset activates it (writes g_Config.sSlangShaderPreset, clears legacy
// post-shaders) and pops back to the caller.
class SlangCategoryScreen : public UIBaseDialogScreen {
public:
	SlangCategoryScreen(const Path &gamePath, const std::string &category)
		: UIBaseDialogScreen(gamePath), category_(category) {}
	void CreateViews() override;
	bool key(const KeyInput &input) override;
	const char *tag() const override { return "SlangCategory"; }
private:
	void ActivatePreset(const Path &presetPath);
	SlangPresetLibrary library_;
	std::string category_;
	ViewSearch search_{};
	UI::ViewGroup *listContainer_ = nullptr;
};

// Parameter sliders for the currently-active preset, bound live to g_Config.mSlangParams.
// Reached from a "Shader parameters" row in Display settings when the active preset has params.
class SlangParamsScreen : public UIBaseDialogScreen {
public:
	explicit SlangParamsScreen(const Path &gamePath) : UIBaseDialogScreen(gamePath) {}
	void CreateViews() override;
	const char *tag() const override { return "SlangParams"; }
};

// Helper shared with the settings screen:
// True if the active preset exists and exposes at least one #pragma parameter.
bool ActiveSlangPresetHasParams();
