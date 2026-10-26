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

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Root.h"
#include "Core/Config.h"
#include "Core/ConfigSettings.h"
#include "UI/MiscViews.h"
#include "UI/SettingsPage.h"

std::string_view SettingText::Translate(I18NCat pageCat) const {
	if (!key) {
		return "";
	}
	if (raw) {
		return key;
	}
	return T(hasCat ? cat : pageCat, key, def ? def : "");
}

std::string_view SettingText::English() const {
	return def ? def : key;
}

SettingItem &SettingsPage::Add(SettingType type, SettingText text) {
	SettingItem &item = items_.emplace_back();
	item.type = type;
	item.text = text;
	return item;
}

SettingItem &SettingsPage::Header(SettingText text) {
	return Add(SettingType::Header, text);
}

// The info of the config setting that value points to.
static const ConfigSettingInfo &InfoFor(const void *value) {
	static const ConfigSettingInfo none{};
	const ConfigSettingRef *ref = Config::FindSetting(value);
	_dbg_assert_msg_(ref && ref->setting->Info().label, "Not a config setting, or it has no label");
	return (ref && ref->setting->Info().label) ? ref->setting->Info() : none;
}

static SettingText LabelText(const ConfigSettingInfo &info) {
	if (!info.label) {
		return SettingText::Raw("?");
	}
	return SettingText(info.cat, info.label, info.labelDefault);
}

// Units that are symbols or short abbreviations, like %, x, ms and MHz, aren't translated.
static std::string TranslateUnits(I18NCat cat, const char *units) {
	if (strlen(units) <= 3) {
		return units;
	}
	for (const char *c = units; *c; c++) {
		if (isalpha((unsigned char)*c)) {
			return std::string(T(cat, units));
		}
	}
	return units;
}

SettingItem &SettingsPage::Setting(bool *value) {
	const ConfigSettingInfo &info = InfoFor(value);
	SettingItem &item = Add(SettingType::Bool, LabelText(info));
	item.ptr = value;
	item.configOnChanged = info.onChanged;
	return item;
}

SettingItem &SettingsPage::Setting(int *value) {
	const ConfigSettingInfo &info = InfoFor(value);
	if (info.HasChoices()) {
		SettingItem &item = Add(SettingType::IntChoice, LabelText(info));
		item.ptr = value;
		item.configOnChanged = info.onChanged;
		item.choices = info.choices;
		item.minVal = info.choiceMin;
		item.numChoices = info.choiceCount;
		return item;
	}

	_dbg_assert_msg_(info.HasRange(), "Int setting '%s' has neither choices nor a range", info.label ? info.label : "?");
	SettingItem &item = Add(SettingType::IntSlider, LabelText(info));
	item.ptr = value;
	item.configOnChanged = info.onChanged;
	item.minValue = (int)info.minValue;
	item.maxValue = (int)info.maxValue;
	item.step = info.step >= 1.0f ? (int)info.step : 1;
	if (info.units) {
		item.units = TranslateUnits(info.cat, info.units);
		item.format = item.units == "%" ? "%d%%" : "%d " + ReplaceAll(item.units, "%", "%%");
	}
	if (info.zeroLabel) {
		item.zeroLabel = SettingText(info.cat, info.zeroLabel);
	}
	if (info.negativeLabel) {
		item.negativeLabel = SettingText(info.cat, info.negativeLabel);
	}
	return item;
}

SettingItem &SettingsPage::Setting(float *value) {
	const ConfigSettingInfo &info = InfoFor(value);
	_dbg_assert_msg_(info.HasRange(), "Float setting '%s' has no range", info.label ? info.label : "?");
	SettingItem &item = Add(SettingType::FloatSlider, LabelText(info));
	item.ptr = value;
	item.configOnChanged = info.onChanged;
	item.fMinValue = info.minValue;
	item.fMaxValue = info.maxValue;
	item.fStep = info.step;
	item.fDefaultValue = Config::GetDefaultValueFloat(value);
	if (info.units) {
		item.units = TranslateUnits(info.cat, info.units);
	}
	return item;
}

SettingItem &SettingsPage::Setting(std::string *value, std::vector<std::string> names, std::vector<std::string> values) {
	_dbg_assert_(values.empty() || values.size() == names.size());
	const ConfigSettingInfo &info = InfoFor(value);
	SettingItem &item = Add(SettingType::StringChoice, LabelText(info));
	item.ptr = value;
	item.configOnChanged = info.onChanged;
	item.choiceNames = std::move(names);
	item.choiceValues = std::move(values);
	return item;
}

SettingItem &SettingsPage::Bool(bool *value, SettingText text) {
	SettingItem &item = Add(SettingType::Bool, text);
	item.ptr = value;
	return item;
}

SettingItem &SettingsPage::Bool(SettingText text, std::function<bool()> get, std::function<void(bool)> set) {
	SettingItem &item = Add(SettingType::Bool, text);
	item.getBool = std::move(get);
	item.setBool = std::move(set);
	return item;
}

SettingItem &SettingsPage::IntSlider(int *value, int minValue, int maxValue, SettingText text) {
	SettingItem &item = Add(SettingType::IntSlider, text);
	item.ptr = value;
	item.minValue = minValue;
	item.maxValue = maxValue;
	return item;
}

SettingItem &SettingsPage::Choice(SettingText text, ImageID image) {
	SettingItem &item = Add(SettingType::Choice, text);
	item.image = image;
	return item;
}

SettingItem &SettingsPage::Info(SettingText text, std::string_view rightText) {
	SettingItem &item = Add(SettingType::Info, text);
	item.rightText = rightText;
	return item;
}

SettingItem &SettingsPage::Hint(SettingText text) {
	_dbg_assert_(!items_.empty());
	return Add(SettingType::Hint, text);
}

SettingItem &SettingsPage::Link(SettingText text, I18NCat cat, std::function<void(SettingsPage *)> build) {
	SettingItem &item = Add(SettingType::Link, text);
	item.linkCat = cat;
	item.linkBuild = std::move(build);
	return item;
}

SettingItem &SettingsPage::Custom(SettingText text, std::function<UI::View *(UI::ViewGroup *parent)> create) {
	SettingItem &item = Add(SettingType::Custom, text);
	item.createCustom = std::move(create);
	return item;
}

namespace {

// Shows and changes state through functions, instead of a bool it points to.
class CallbackCheckBox : public UI::CheckBox {
public:
	CallbackCheckBox(std::string_view text, std::function<bool()> get, std::function<void(bool)> set)
		: CheckBox(nullptr, text), get_(std::move(get)), set_(std::move(set)) {}

	void Toggle() override {
		bool on = !get_();
		set_(on);
		UI::PlayUISound(on ? UI::UISound::TOGGLE_ON : UI::UISound::TOGGLE_OFF);
	}
	bool Toggled() const override {
		return get_();
	}

private:
	std::function<bool()> get_;
	std::function<void(bool)> set_;
};

}  // namespace

// The config's reaction to the new value comes first, then the page's.
static std::function<void(UI::EventParams &)> ChangeHandler(const SettingItem &item) {
	if (!item.configOnChanged) {
		return item.onChange;
	}
	void (*onChanged)() = item.configOnChanged;
	std::function<void(UI::EventParams &)> pageHandler = item.onChange;
	return [onChanged, pageHandler](UI::EventParams &e) {
		onChanged();
		if (pageHandler) {
			pageHandler(e);
		}
	};
}

static UI::View *CreateSettingViewInner(const SettingItem &item, I18NCat pageCat, UI::View *prevView, UI::ViewGroup *parent, ScreenManager *screenManager) {
	using namespace UI;

	std::string_view text = item.text.Translate(pageCat);
	switch (item.type) {
	case SettingType::Header:
		return parent->Add(new ChoiceGroup(text));
	case SettingType::Bool:
	{
		CheckBox *checkBox = item.getBool ? new CallbackCheckBox(text, item.getBool, item.setBool) : new CheckBox((bool *)item.ptr, text);
		parent->Add(checkBox);
		if (std::function<void(UI::EventParams &)> onChange = ChangeHandler(item)) {
			checkBox->OnClick.Add(onChange);
		}
		return checkBox;
	}
	case SettingType::IntChoice:
	{
		I18NCat choiceCat = item.text.hasCat ? item.text.cat : pageCat;
		PopupMultiChoice *choice = parent->Add(new PopupMultiChoice((int *)item.ptr, text, const_cast<const char **>(item.choices), item.minVal, item.numChoices, choiceCat, screenManager));
		if (std::function<void(UI::EventParams &)> onChange = ChangeHandler(item)) {
			choice->OnChoice.Add(onChange);
		}
		return choice;
	}
	case SettingType::StringChoice:
	{
		// PopupMultiChoiceDynamic copies the values.
		std::vector<std::string> values = item.choiceValues;
		PopupMultiChoiceDynamic *choice = parent->Add(new PopupMultiChoiceDynamic((std::string *)item.ptr, text, item.choiceNames, I18NCat::NONE, screenManager, values.empty() ? nullptr : &values));
		if (std::function<void(UI::EventParams &)> onChange = ChangeHandler(item)) {
			choice->OnChoice.Add(onChange);
		}
		return choice;
	}
	case SettingType::IntSlider:
	{
		int defaultValue = item.defaultValue;
		if (!item.hasDefault) {
			defaultValue = Config::GetDefaultValueInt((int *)item.ptr);
			_dbg_assert_msg_(defaultValue != ::NO_DEFAULT_INT, "No default for slider '%s'", item.text.key);
		}
		PopupSliderChoice *slider = parent->Add(new PopupSliderChoice((int *)item.ptr, item.minValue, item.maxValue, defaultValue, text, item.step, screenManager, item.units));
		if (!item.format.empty()) {
			slider->SetFormat(item.format);
		}
		if (!item.zeroLabel.empty()) {
			slider->SetZeroLabel(item.zeroLabel.Translate(pageCat));
		}
		if (!item.negativeLabel.empty()) {
			slider->SetNegativeDisable(std::string(item.negativeLabel.Translate(pageCat)));
		}
		if (item.liveUpdate) {
			slider->SetLiveUpdate(true);
		}
		if (item.restrictTo) {
			slider->RestrictChoices(item.restrictTo, item.restrictCount);
		}
		if (std::function<void(UI::EventParams &)> onChange = ChangeHandler(item)) {
			slider->OnChange.Add(onChange);
		}
		return slider;
	}
	case SettingType::FloatSlider:
	{
		PopupSliderChoiceFloat *slider = parent->Add(new PopupSliderChoiceFloat((float *)item.ptr, item.fMinValue, item.fMaxValue, item.fDefaultValue, text, item.fStep, screenManager, item.units));
		if (item.liveUpdate) {
			slider->SetLiveUpdate(true);
		}
		if (std::function<void(UI::EventParams &)> onChange = ChangeHandler(item)) {
			slider->OnChange.Add(onChange);
		}
		return slider;
	}
	case SettingType::Choice:
	{
		UI::Choice *choice = parent->Add(new UI::Choice(text, item.image));
		if (std::function<void(UI::EventParams &)> onChange = ChangeHandler(item)) {
			choice->OnClick.Add(onChange);
		}
		return choice;
	}
	case SettingType::Info:
		return parent->Add(new InfoItem(text, item.rightText));
	case SettingType::Hint:
		return parent->Add(new SettingHint(text, prevView));
	case SettingType::Custom:
		return item.createCustom(parent);
	case SettingType::Link:
	{
		// The screen showing the page handles the click.
		UI::Choice *choice = parent->Add(new UI::Choice(text, item.image));
		choice->SetIconRight(ImageID("I_CHEVRON_RIGHT"));
		return choice;
	}
	}
	return nullptr;
}

static UI::View *CreateSettingView(const SettingItem &item, I18NCat pageCat, UI::View *prevView, UI::ViewGroup *parent, ScreenManager *screenManager) {
	UI::View *view = CreateSettingViewInner(item, pageCat, prevView, parent, screenManager);
	if (!view) {
		return nullptr;
	}
	if (item.enabledFunc) {
		view->SetEnabledFunc(item.enabledFunc);
	} else if (item.enabledPtr) {
		view->SetEnabledPtr(item.enabledPtr);
	} else if (item.disabledPtr) {
		view->SetDisabledPtr(item.disabledPtr);
	}
	if (item.opensScreen) {
		if (UI::Choice *choice = dynamic_cast<UI::Choice *>(view)) {
			choice->SetIconRight(ImageID("I_CHEVRON_RIGHT"));
		}
	}
	if (item.setup) {
		item.setup(view);
	}
	return view;
}

void CreateSettingViews(const SettingsPage &page, UI::ViewGroup *parent, ScreenManager *screenManager, std::vector<UI::View *> *views) {
	UI::View *prevView = nullptr;
	// Each header is a group, which the items after it go in. Items before the first get one without a title.
	UI::ViewGroup *group = nullptr;
	for (const SettingItem &item : page.Items()) {
		if (!group && item.type != SettingType::Header) {
			group = parent->Add(new UI::ChoiceGroup());
		}
		UI::View *view = CreateSettingView(item, page.DefaultCat(), prevView, item.type == SettingType::Header ? parent : group, screenManager);
		if (views) {
			views->push_back(view);
		}
		if (view && item.type == SettingType::Header) {
			group = static_cast<UI::ViewGroup *>(view);
		} else if (view) {
			prevView = view;
		}
	}
}
