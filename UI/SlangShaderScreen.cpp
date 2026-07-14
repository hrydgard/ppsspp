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

#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"
#include "Common/Data/Text/I18n.h"

#include "UI/SlangShaderScreen.h"
#include "UI/MiscViews.h"
#include "Core/Config.h"

void SlangShaderScreen::CreateViews() {
	library_.Rescan();

	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *rightColumn = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300.0f, FILL_PARENT, NONE, 0.f, 0.f, 0.f));
	rightColumn->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	rightColumn->Add(new Spacer(12.0f));
	root_->Add(rightColumn);

	ScrollView *leftScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0.f, 0.f, 300.0f, 0.f));
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(new ItemHeader(gr->T("RetroArch (slang) shaders")));
	leftScrollView->Add(leftColumn);
	root_->Add(leftScrollView);

	if (library_.Empty()) {
		leftColumn->Add(new TextView(gr->T("No slang shaders imported yet")));
		return;
	}

	LinearLayout *listContainer = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(listContainer);

	// Add "None (disable)" option
	std::string noneLabel = std::string(gr->T("None (disable)"));
	if (g_Config.sSlangShaderPreset.empty()) {
		noneLabel += " ✓";  // Unicode checkmark
	}
	listContainer->Add(new Choice(noneLabel))->OnClick.Add([this](EventParams &e) {
		Deactivate();
	});

	// Add presets by category
	for (const std::string &category : library_.GetCategories()) {
		listContainer->Add(new ItemHeader(category));
		std::vector<SlangPresetEntry> presets = library_.GetPresets(category);
		for (const SlangPresetEntry &entry : presets) {
			std::string label = entry.displayName;
			if (entry.path.ToString() == g_Config.sSlangShaderPreset) {
				label += " ✓";
			}
			listContainer->Add(new Choice(label))->OnClick.Add([this, entry](EventParams &e) {
				ActivatePreset(entry.path);
			});
		}
	}
}

void SlangShaderScreen::ActivatePreset(const Path &presetPath) {
	g_Config.sSlangShaderPreset = presetPath.ToString();
	// Mutual exclusivity: a slang preset and the legacy post-shader chain never both run.
	g_Config.vPostShaderNames.clear();
	g_Config.vPostShaderNames.push_back("Off");
	RecreateViews();
}

void SlangShaderScreen::Deactivate() {
	g_Config.sSlangShaderPreset.clear();
	RecreateViews();
}
