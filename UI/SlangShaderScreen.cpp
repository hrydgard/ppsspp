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
#include "Common/UI/PopupScreens.h"
#include "Common/Data/Text/I18n.h"

#include "UI/SlangShaderScreen.h"
#include "UI/MiscViews.h"
#include "Core/Config.h"
#include "Core/Slang/SlangPresetLibrary.h"
#include "Core/Slang/SlangPackageImporter.h"
#include "GPU/Common/Slang/SlangPreset.h"
#include "Common/System/OSD.h"

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

	// Import/Update button
	std::string importLabel = library_.Empty() ?
		std::string(gr->T("Import RetroArch (slang) shaders")) :
		std::string(gr->T("Import / update shaders"));
	leftColumn->Add(new Choice(importLabel))->OnClick.Add([](EventParams &e) {
		if (!g_SlangImporter.Busy()) {
			if (!g_SlangImporter.Start("")) {
				g_OSD.Show(OSDType::MESSAGE_ERROR, "Slang shader import failed", g_SlangImporter.GetError(), 4.0f);
			}
		}
	});

	// Search box
	search_.searchFilter.clear();
	search_.searchBar = leftColumn->Add(new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	search_.searchBar->OnCancel.Add([this](UI::EventParams &) {
		search_.searchFilter.clear();
		search_.ApplySearchFilter(listContainer_, false);
	});

	leftScrollView->Add(leftColumn);
	root_->Add(leftScrollView);

	if (library_.Empty()) {
		leftColumn->Add(new TextView(gr->T("No slang shaders imported yet")));
		listContainer_ = nullptr;
		return;
	}

	listContainer_ = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(listContainer_);

	// Add "None (disable)" option
	std::string noneLabel = std::string(gr->T("None (disable)"));
	if (g_Config.sSlangShaderPreset.empty()) {
		noneLabel += " ✓";  // Unicode checkmark
	}
	listContainer_->Add(new Choice(noneLabel))->OnClick.Add([this](EventParams &e) {
		Deactivate();
	});

	// Add presets by category
	for (const std::string &category : library_.GetCategories()) {
		ItemHeader *header = listContainer_->Add(new ItemHeader(category));
		header->SetAlwaysVisibleInSearch(true);
		std::vector<SlangPresetEntry> presets = library_.GetPresets(category);
		for (const SlangPresetEntry &entry : presets) {
			std::string label = entry.displayName;
			if (entry.path.ToString() == g_Config.sSlangShaderPreset) {
				label += " ✓";
			}
			listContainer_->Add(new Choice(label))->OnClick.Add([this, entry](EventParams &e) {
				ActivatePreset(entry.path);
			});
		}
	}

	// Add parameter sliders if a preset is active
	if (!g_Config.sSlangShaderPreset.empty()) {
		std::vector<SlangParamDesc> params;
		std::string err;
		if (GetPresetParameters(Path(g_Config.sSlangShaderPreset), &params, &err) && !params.empty()) {
			ItemHeader *paramHeader = listContainer_->Add(new ItemHeader(gr->T("Shader parameters")));
			paramHeader->SetAlwaysVisibleInSearch(true);
			const std::string prefix = g_Config.sSlangShaderPreset + "|";
			for (const auto &p : params) {
				const std::string key = prefix + p.name;
				bool existed = g_Config.mSlangParams.find(key) != g_Config.mSlangParams.end();
				float &value = g_Config.mSlangParams[key];   // map auto-creates
				if (!existed) value = p.initial;             // seed with the shader default
				const std::string label = p.description.empty() ? p.name : p.description;
				float step = p.step > 0.0f ? p.step : (p.maximum - p.minimum) / 100.0f;
				PopupSliderChoiceFloat *slider = listContainer_->Add(new PopupSliderChoiceFloat(
					&value, p.minimum, p.maximum, p.initial, label, step, screenManager()));
				slider->SetLiveUpdate(true);
				slider->SetHasDropShadow(false);
			}
			listContainer_->Add(new Choice(gr->T("Reset parameters to defaults")))->OnClick.Add(
				[this](UI::EventParams &e) {
					const std::string pfx = g_Config.sSlangShaderPreset + "|";
					for (auto it = g_Config.mSlangParams.begin(); it != g_Config.mSlangParams.end(); ) {
						if (it->first.compare(0, pfx.size(), pfx) == 0) it = g_Config.mSlangParams.erase(it);
						else ++it;
					}
					RecreateViews();
				});
		}
	}

	// Apply search filter after building the list
	search_.ApplySearchFilter(listContainer_, false);
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

bool SlangShaderScreen::key(const KeyInput &input) {
	if (listContainer_ && search_.Key(listContainer_, input)) {
		return true;
	}
	return UIBaseDialogScreen::key(input);
}
