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

#include <vector>

#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"
#include "Common/Data/Text/I18n.h"

#include "UI/SlangShaderScreen.h"
#include "UI/MiscViews.h"
#include "Core/Config.h"
#include "Core/Slang/SlangPresetLibrary.h"
#include "Core/Slang/SlangPackageImporter.h"
#include "GPU/Common/Slang/SlangPreset.h"
#include "Common/System/OSD.h"

// ---- shared helpers -------------------------------------------------------

bool ActiveSlangPresetHasParams() {
	if (g_Config.sSlangShaderPreset.empty()) {
		return false;
	}
	std::vector<SlangParamDesc> params;
	std::string err;
	return GetPresetParameters(Path(g_Config.sSlangShaderPreset), &params, &err) && !params.empty();
}

// Clears the active preset (and mutual-exclusivity helper).
static void ActivateSlangPreset(const Path &presetPath) {
	g_Config.sSlangShaderPreset = presetPath.ToString();
	// Mutual exclusivity: a slang preset and the legacy post-shader chain never both run.
	g_Config.vPostShaderNames.clear();
	g_Config.vPostShaderNames.push_back("Off");
}

// ---- top-level browser: categories ----------------------------------------

void SlangShaderScreen::CreateViews() {
	library_.Rescan();
	lastImportState_ = g_SlangImporter.GetState();

	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *rightColumn = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300.0f, FILL_PARENT, NONE, 0.f, 0.f, 0.f));
	rightColumn->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(rightColumn);

	ScrollView *leftScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0.f, 0.f, 300.0f, 0.f));
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(new ItemHeader(gr->T("RetroArch (slang) shaders")));

	// Import/Update button.
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

	leftScrollView->Add(leftColumn);
	root_->Add(leftScrollView);

	if (library_.Empty()) {
		leftColumn->Add(new TextView(gr->T("No slang shaders imported yet")));
		return;
	}

	// "None (disable)" entry.
	std::string noneLabel = std::string(gr->T("None (disable)"));
	if (g_Config.sSlangShaderPreset.empty()) {
		noneLabel += " ✓";
	}
	leftColumn->Add(new Choice(noneLabel))->OnClick.Add([this](EventParams &e) {
		Deactivate();
	});

	// Category list — one row per category, pushing the per-category preset screen.
	leftColumn->Add(new ItemHeader(gr->T("Categories")));
	const std::string activeCategory = library_.CategoryOf(Path(g_Config.sSlangShaderPreset));
	for (const std::string &category : library_.GetCategories()) {
		int count = (int)library_.GetPresets(category).size();
		std::string label = category + "  (" + std::to_string(count) + ")";
		if (!activeCategory.empty() && category == activeCategory) {
			label += " ✓";
		}
		leftColumn->Add(new Choice(label))->OnClick.Add([this, category](EventParams &e) {
			screenManager()->push(new SlangCategoryScreen(gamePath_, category));
		});
	}
}

void SlangShaderScreen::update() {
	UIBaseDialogScreen::update();
	// Rebuild once when an import finishes, so newly-extracted categories appear.
	SlangImportState st = g_SlangImporter.GetState();
	if (st != lastImportState_) {
		lastImportState_ = st;
		if (st == SlangImportState::DONE || st == SlangImportState::FAILED) {
			RecreateViews();
		}
	}
}

void SlangShaderScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// Returning from the per-category preset list: refresh the active-category marker.
	RecreateViews();
}

void SlangShaderScreen::Deactivate() {
	g_Config.sSlangShaderPreset.clear();
	RecreateViews();
}

// ---- second level: presets within a category ------------------------------

void SlangCategoryScreen::CreateViews() {
	library_.Rescan();

	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *rightColumn = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300.0f, FILL_PARENT, NONE, 0.f, 0.f, 0.f));
	rightColumn->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(rightColumn);

	ScrollView *leftScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0.f, 0.f, 300.0f, 0.f));
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(new ItemHeader(category_));

	// Search box (scoped to this category's list).
	search_.searchFilter.clear();
	search_.searchBar = leftColumn->Add(new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	search_.searchBar->OnCancel.Add([this](UI::EventParams &) {
		search_.searchFilter.clear();
		search_.ApplySearchFilter(listContainer_, false);
	});

	listContainer_ = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(listContainer_);
	leftScrollView->Add(leftColumn);
	root_->Add(leftScrollView);

	for (const SlangPresetEntry &entry : library_.GetPresets(category_)) {
		std::string label = entry.displayName;
		if (entry.path.ToString() == g_Config.sSlangShaderPreset) {
			label += " ✓";
		}
		listContainer_->Add(new Choice(label))->OnClick.Add([this, entry](EventParams &e) {
			ActivatePreset(entry.path);
		});
	}

	search_.ApplySearchFilter(listContainer_, false);
}

void SlangCategoryScreen::ActivatePreset(const Path &presetPath) {
	ActivateSlangPreset(presetPath);
	// Selection made — return to the category list (which shows the ✓ on this category).
	TriggerFinish(DR_OK);
}

bool SlangCategoryScreen::key(const KeyInput &input) {
	if (listContainer_ && search_.Key(listContainer_, input)) {
		return true;
	}
	return UIBaseDialogScreen::key(input);
}

// ---- parameter sliders for the active preset ------------------------------

void SlangParamsScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *rightColumn = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300.0f, FILL_PARENT, NONE, 0.f, 0.f, 0.f));
	rightColumn->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(rightColumn);

	ScrollView *leftScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0.f, 0.f, 300.0f, 0.f));
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(new ItemHeader(gr->T("Shader parameters")));
	leftScrollView->Add(leftColumn);
	root_->Add(leftScrollView);

	if (g_Config.sSlangShaderPreset.empty()) {
		return;
	}
	std::vector<SlangParamDesc> params;
	std::string err;
	if (!GetPresetParameters(Path(g_Config.sSlangShaderPreset), &params, &err) || params.empty()) {
		leftColumn->Add(new TextView(gr->T("This shader has no adjustable parameters")));
		return;
	}

	const std::string prefix = g_Config.sSlangShaderPreset + "|";
	for (const auto &p : params) {
		const std::string key = prefix + p.name;
		bool existed = g_Config.mSlangParams.find(key) != g_Config.mSlangParams.end();
		float &value = g_Config.mSlangParams[key];   // map auto-creates
		if (!existed) value = p.initial;             // seed with the shader default
		const std::string label = p.description.empty() ? p.name : p.description;
		float step = p.step > 0.0f ? p.step : (p.maximum - p.minimum) / 100.0f;
		PopupSliderChoiceFloat *slider = leftColumn->Add(new PopupSliderChoiceFloat(
			&value, p.minimum, p.maximum, p.initial, label, step, screenManager()));
		slider->SetLiveUpdate(true);
		slider->SetHasDropShadow(false);
	}

	leftColumn->Add(new Choice(gr->T("Reset parameters to defaults")))->OnClick.Add(
		[this](UI::EventParams &e) {
			const std::string pfx = g_Config.sSlangShaderPreset + "|";
			for (auto it = g_Config.mSlangParams.begin(); it != g_Config.mSlangParams.end(); ) {
				if (it->first.compare(0, pfx.size(), pfx) == 0) it = g_Config.mSlangParams.erase(it);
				else ++it;
			}
			RecreateViews();
		});
}
