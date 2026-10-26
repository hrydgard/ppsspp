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

class Path;
class ScreenManager;
class SettingsPage;

// What the pages' handlers can do on the screen showing them. Lives as long as the views do.
class SettingsPageHost {
public:
	virtual ~SettingsPageHost() {}
	virtual ScreenManager *PageScreenManager() = 0;
	// For System_ requests (like file dialogs) whose results go to the views.
	virtual int PageRequesterToken() = 0;
	// Builds the page again, for when what's on it changed.
	virtual void RecreatePageViews() = 0;
	// The game whose settings are being edited, if any.
	virtual const Path &PageGamePath() const = 0;
	// For a setting that only takes effect after a restart: asks to restart, or else calls otherwise
	// (which usually puts the setting back).
	virtual void RestartOrDo(std::function<void()> otherwise) = 0;
};

// Translatable text, kept as a key so it can be looked up both in English and translated
// (for search) as well as when it's shown.
struct SettingText {
	SettingText() {}
	SettingText(const char *key) : key(key) {}
	SettingText(I18NCat cat, const char *key, const char *def = nullptr) : cat(cat), key(key), def(def), hasCat(true) {}
	// Shown as is, without translation (like module names).
	static SettingText Raw(const char *text) {
		SettingText t(text);
		t.raw = true;
		return t;
	}

	// Texts without a category use the page's.
	std::string_view Translate(I18NCat pageCat) const;
	std::string_view English() const;
	bool empty() const { return !key; }

	I18NCat cat = I18NCat::NONE;
	const char *key = nullptr;
	const char *def = nullptr;
	bool hasCat = false;
	bool raw = false;
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
	Link,  // Opens another page.
};

// Describes one item in a settings page. Views are only created from this in CreateSettingViews,
// so a page can also be inspected without creating any (for search).
struct SettingItem {
	SettingType type;
	SettingText text;
	void *ptr = nullptr;  // bool *, int * or std::string *, depending on type.

	// IntChoice. The choices are translated in the same category as the text.
	const char *const *choices = nullptr;
	int minVal = 0;
	int numChoices = 0;

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
	SettingText negativeLabel;
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

	// Link: builds the page it opens, whose texts default to linkCat. helpTag is the docs page for it and the
	// pages under it, if not the same as the linking page's.
	std::function<void(SettingsPage *)> linkBuild;
	I18NCat linkCat = I18NCat::NONE;
	const char *helpTag = nullptr;

	// Bool, instead of ptr: for state that lives elsewhere, like whether a server runs.
	std::function<bool()> getBool;
	std::function<void(bool)> setBool;

	bool *enabledPtr = nullptr;
	bool *disabledPtr = nullptr;
	std::function<bool()> enabledFunc;
	// Called on click (Bool, Choice), choice (choices) or change (sliders).
	std::function<void(UI::EventParams &)> onChange;
	// From the config, for settings described there. Called before onChange.
	void (*configOnChanged)() = nullptr;
	// For tweaks the description doesn't cover, called after the view is created.
	std::function<void(UI::View *)> setup;
	// Choice or Custom (if its view is a Choice) that opens another screen: shows the same chevron as a Link.
	bool opensScreen = false;

	SettingItem &Enabled(bool *ptr) { enabledPtr = ptr; return *this; }
	SettingItem &Disabled(bool *ptr) { disabledPtr = ptr; return *this; }
	SettingItem &EnabledFunc(std::function<bool()> func) { enabledFunc = std::move(func); return *this; }
	SettingItem &OnChange(std::function<void(UI::EventParams &)> func) { onChange = std::move(func); return *this; }
	SettingItem &OnClick(std::function<void(UI::EventParams &)> func) { onChange = std::move(func); return *this; }
	SettingItem &Setup(std::function<void(UI::View *)> func) { setup = std::move(func); return *this; }
	SettingItem &OpensScreen() { opensScreen = true; return *this; }
	SettingItem &Default(int value) { defaultValue = value; hasDefault = true; return *this; }
	SettingItem &Step(int value) { step = value; return *this; }
	SettingItem &Format(std::string_view fmt) { format = fmt; return *this; }
	SettingItem &LiveUpdate() { liveUpdate = true; return *this; }
	SettingItem &RestrictTo(const int *values, size_t count) { restrictTo = values; restrictCount = count; return *this; }
};

// A list of settings items. Texts without a category get the page's.
class SettingsPage {
public:
	SettingsPage(I18NCat defaultCat, SettingsPageHost *host) : defaultCat_(defaultCat), host_(host) {}

	// Without a title, just starts a new group.
	SettingItem &Header(SettingText text = SettingText());
	// Settings described in the config: the label, kind of view, range or choices, and default come from there.
	SettingItem &Setting(bool *value);
	SettingItem &Setting(int *value);
	SettingItem &Setting(float *value);
	// For string settings with choices that are only known at runtime, like devices. The names are shown as
	// is, untranslated. If values is empty, the names are stored.
	SettingItem &Setting(std::string *value, std::vector<std::string> names, std::vector<std::string> values = {});
	// For values that aren't config settings.
	SettingItem &Bool(bool *value, SettingText text);
	SettingItem &Bool(SettingText text, std::function<bool()> get, std::function<void(bool)> set);
	SettingItem &IntSlider(int *value, int minValue, int maxValue, SettingText text);
	SettingItem &Choice(SettingText text, ImageID image = ImageID::invalid());
	SettingItem &Info(SettingText text, std::string_view rightText);
	SettingItem &Hint(SettingText text);
	// For anything the other types can't describe. The text is only used for search. The view
	// returned is the one a following Hint is attached to, and the one search results lead to.
	SettingItem &Custom(SettingText text, std::function<UI::View *(UI::ViewGroup *parent)> create);
	// Opens another page, built by build, on top of this one. The text is the new page's title.
	SettingItem &Link(SettingText text, I18NCat cat, std::function<void(SettingsPage *)> build);

	I18NCat DefaultCat() const { return defaultCat_; }
	SettingsPageHost *Host() const { return host_; }
	const std::deque<SettingItem> &Items() const { return items_; }

private:
	SettingItem &Add(SettingType type, SettingText text);

	I18NCat defaultCat_;
	SettingsPageHost *host_;
	std::deque<SettingItem> items_;  // A deque so references stay valid while adding.
};

// If views is set, it gets the view created for each item (or null), in the same order.
void CreateSettingViews(const SettingsPage &page, UI::ViewGroup *parent, ScreenManager *screenManager, std::vector<UI::View *> *views = nullptr);
