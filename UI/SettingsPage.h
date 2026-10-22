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

#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Common/Data/Text/I18n.h"
#include "Common/Render/TextureAtlas.h"

namespace UI {
class View;
class ViewGroup;
struct EventParams;
}

class ScreenManager;

// Translatable text, kept as a key so it can be looked up both in English and translated
// (for search) as well as when it's shown.
struct SettingText {
	SettingText() {}
	SettingText(const char *key) : key(key) {}
	SettingText(I18NCat cat, const char *key, const char *def = nullptr) : cat(cat), key(key), def(def), hasCat(true) {}

	// Texts without a category use the page's.
	std::string_view Translate(I18NCat pageCat) const;
	std::string_view English() const;
	bool empty() const { return !key; }

	I18NCat cat = I18NCat::NONE;
	const char *key = nullptr;
	const char *def = nullptr;
	bool hasCat = false;
};

enum class SettingType {
	Header,
	Bool,
	IntChoice,
	StringChoice,
	IntSlider,
	FloatSlider,
	Choice,  // A button. onChange is called on click.
	Info,
	Hint,  // Explanatory text attached to the previous item.
	Custom,
};

// Describes one item in a settings page. Views are only created from this in CreateSettingViews,
// so a page can also be inspected without creating any (for search).
struct SettingItem {
	SettingType type;
	SettingText text;
	void *ptr = nullptr;  // bool *, int * or std::string *, depending on type.

	// IntChoice. The choices are translated in the same category as the text, unless choiceCat is set.
	const char **choices = nullptr;
	int minVal = 0;
	int numChoices = 0;
	I18NCat choiceCat = I18NCat::NONE;
	bool hasChoiceCat = false;

	// StringChoice. If values is empty, the names are stored.
	std::vector<std::string> choiceNames;
	std::vector<std::string> choiceValues;

	// IntSlider. If defaultValue isn't set, it's taken from the config.
	int minValue = 0;
	int maxValue = 0;
	int defaultValue = 0;
	bool hasDefault = false;
	int step = 1;
	std::string format;
	SettingText zeroLabel;
	std::string negativeLabel;
	const int *restrictTo = nullptr;
	size_t restrictCount = 0;

	// FloatSlider
	float fMinValue = 0.0f;
	float fMaxValue = 0.0f;
	float fDefaultValue = 0.0f;
	float fStep = 0.0f;

	// Both slider types. Already translated.
	std::string units;
	bool liveUpdate = false;

	// Choice
	ImageID image;

	// Info
	std::string rightText;

	// Custom
	std::function<UI::View *(UI::ViewGroup *parent)> createCustom;

	bool *enabledPtr = nullptr;
	bool *disabledPtr = nullptr;
	std::function<bool()> enabledFunc;
	// Called on click (Bool, Choice), choice (choices) or change (sliders).
	std::function<void(UI::EventParams &)> onChange;
	// For tweaks the description doesn't cover, called after the view is created.
	std::function<void(UI::View *)> setup;

	SettingItem &Enabled(bool *ptr) { enabledPtr = ptr; return *this; }
	SettingItem &Disabled(bool *ptr) { disabledPtr = ptr; return *this; }
	SettingItem &EnabledFunc(std::function<bool()> func) { enabledFunc = std::move(func); return *this; }
	SettingItem &OnChange(std::function<void(UI::EventParams &)> func) { onChange = std::move(func); return *this; }
	SettingItem &OnClick(std::function<void(UI::EventParams &)> func) { onChange = std::move(func); return *this; }
	SettingItem &Setup(std::function<void(UI::View *)> func) { setup = std::move(func); return *this; }
	SettingItem &ChoiceCat(I18NCat cat) { choiceCat = cat; hasChoiceCat = true; return *this; }
	SettingItem &Default(int value) { defaultValue = value; hasDefault = true; return *this; }
	SettingItem &Step(int value) { step = value; return *this; }
	SettingItem &Format(std::string_view fmt) { format = fmt; return *this; }
	SettingItem &ZeroLabel(SettingText label) { zeroLabel = label; return *this; }
	SettingItem &NegativeLabel(std::string_view label) { negativeLabel = label; return *this; }
	SettingItem &Units(std::string_view text) { units = text; return *this; }
	SettingItem &LiveUpdate() { liveUpdate = true; return *this; }
	SettingItem &RestrictTo(const int *values, size_t count) { restrictTo = values; restrictCount = count; return *this; }
};

// A list of settings items. Texts without a category get the page's.
class SettingsPage {
public:
	explicit SettingsPage(I18NCat defaultCat) : defaultCat_(defaultCat) {}

	SettingItem &Header(SettingText text);
	SettingItem &Bool(bool *value, SettingText text);
	SettingItem &IntChoice(int *value, SettingText text, const char **choices, int minVal, int numChoices);
	// Choices are shown as is, untranslated.
	SettingItem &StringChoice(std::string *value, SettingText text, std::vector<std::string> names, std::vector<std::string> values = {});
	SettingItem &IntSlider(int *value, int minValue, int maxValue, SettingText text);
	SettingItem &FloatSlider(float *value, float minValue, float maxValue, float defaultValue, float step, SettingText text);
	SettingItem &Choice(SettingText text, ImageID image = ImageID::invalid());
	SettingItem &Info(SettingText text, std::string_view rightText);
	SettingItem &Hint(SettingText text);
	// For anything the other types can't describe. The text is only used for search. The view
	// returned is the one a following Hint is attached to, and the one search results lead to.
	SettingItem &Custom(SettingText text, std::function<UI::View *(UI::ViewGroup *parent)> create);

	I18NCat DefaultCat() const { return defaultCat_; }
	const std::deque<SettingItem> &Items() const { return items_; }

private:
	SettingItem &Add(SettingType type, SettingText text);

	I18NCat defaultCat_;
	std::deque<SettingItem> items_;  // A deque so references stay valid while adding.
};

// If views is set, it gets the view created for each item (or null), in the same order.
void CreateSettingViews(const SettingsPage &page, UI::ViewGroup *parent, ScreenManager *screenManager, std::vector<UI::View *> *views = nullptr);
UI::View *CreateSettingView(const SettingItem &item, I18NCat pageCat, UI::View *prevView, UI::ViewGroup *parent, ScreenManager *screenManager);
