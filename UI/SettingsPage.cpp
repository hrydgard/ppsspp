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
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/PopupScreens.h"
#include "Core/Config.h"
#include "UI/MiscViews.h"
#include "UI/SettingsPage.h"

std::string_view SettingText::Translate(I18NCat pageCat) const {
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

SettingItem &SettingsPage::Bool(bool *value, SettingText text) {
	SettingItem &item = Add(SettingType::Bool, text);
	item.ptr = value;
	return item;
}

SettingItem &SettingsPage::IntChoice(int *value, SettingText text, const char **choices, int minVal, int numChoices) {
	SettingItem &item = Add(SettingType::IntChoice, text);
	item.ptr = value;
	item.choices = choices;
	item.minVal = minVal;
	item.numChoices = numChoices;
	return item;
}

SettingItem &SettingsPage::StringChoice(std::string *value, SettingText text, std::vector<std::string> names, std::vector<std::string> values) {
	_dbg_assert_(values.empty() || values.size() == names.size());
	SettingItem &item = Add(SettingType::StringChoice, text);
	item.ptr = value;
	item.choiceNames = std::move(names);
	item.choiceValues = std::move(values);
	return item;
}

SettingItem &SettingsPage::IntSlider(int *value, int minValue, int maxValue, SettingText text) {
	SettingItem &item = Add(SettingType::IntSlider, text);
	item.ptr = value;
	item.minValue = minValue;
	item.maxValue = maxValue;
	return item;
}

SettingItem &SettingsPage::FloatSlider(float *value, float minValue, float maxValue, float defaultValue, float step, SettingText text) {
	SettingItem &item = Add(SettingType::FloatSlider, text);
	item.ptr = value;
	item.fMinValue = minValue;
	item.fMaxValue = maxValue;
	item.fDefaultValue = defaultValue;
	item.fStep = step;
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

SettingItem &SettingsPage::Custom(SettingText text, std::function<UI::View *(UI::ViewGroup *parent)> create) {
	SettingItem &item = Add(SettingType::Custom, text);
	item.createCustom = std::move(create);
	return item;
}

static UI::View *CreateSettingViewInner(const SettingItem &item, I18NCat pageCat, UI::View *prevView, UI::ViewGroup *parent, ScreenManager *screenManager) {
	using namespace UI;

	std::string_view text = item.text.Translate(pageCat);
	switch (item.type) {
	case SettingType::Header:
		return parent->Add(new ItemHeader(text));
	case SettingType::Bool:
	{
		CheckBox *checkBox = parent->Add(new CheckBox((bool *)item.ptr, text));
		if (item.onChange) {
			checkBox->OnClick.Add(item.onChange);
		}
		return checkBox;
	}
	case SettingType::IntChoice:
	{
		I18NCat choiceCat = item.hasChoiceCat ? item.choiceCat : (item.text.hasCat ? item.text.cat : pageCat);
		PopupMultiChoice *choice = parent->Add(new PopupMultiChoice((int *)item.ptr, text, item.choices, item.minVal, item.numChoices, choiceCat, screenManager));
		if (item.onChange) {
			choice->OnChoice.Add(item.onChange);
		}
		return choice;
	}
	case SettingType::StringChoice:
	{
		// PopupMultiChoiceDynamic copies the values.
		std::vector<std::string> values = item.choiceValues;
		PopupMultiChoiceDynamic *choice = parent->Add(new PopupMultiChoiceDynamic((std::string *)item.ptr, text, item.choiceNames, I18NCat::NONE, screenManager, values.empty() ? nullptr : &values));
		if (item.onChange) {
			choice->OnChoice.Add(item.onChange);
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
			slider->SetNegativeDisable(item.negativeLabel);
		}
		if (item.liveUpdate) {
			slider->SetLiveUpdate(true);
		}
		if (item.restrictTo) {
			slider->RestrictChoices(item.restrictTo, item.restrictCount);
		}
		if (item.onChange) {
			slider->OnChange.Add(item.onChange);
		}
		return slider;
	}
	case SettingType::FloatSlider:
	{
		PopupSliderChoiceFloat *slider = parent->Add(new PopupSliderChoiceFloat((float *)item.ptr, item.fMinValue, item.fMaxValue, item.fDefaultValue, text, item.fStep, screenManager, item.units));
		if (item.liveUpdate) {
			slider->SetLiveUpdate(true);
		}
		if (item.onChange) {
			slider->OnChange.Add(item.onChange);
		}
		return slider;
	}
	case SettingType::Choice:
	{
		UI::Choice *choice = parent->Add(new UI::Choice(text, item.image));
		if (item.onChange) {
			choice->OnClick.Add(item.onChange);
		}
		return choice;
	}
	case SettingType::Info:
		return parent->Add(new InfoItem(text, item.rightText));
	case SettingType::Hint:
		return parent->Add(new SettingHint(text, prevView));
	case SettingType::Custom:
		return item.createCustom(parent);
	}
	return nullptr;
}

UI::View *CreateSettingView(const SettingItem &item, I18NCat pageCat, UI::View *prevView, UI::ViewGroup *parent, ScreenManager *screenManager) {
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
	if (item.setup) {
		item.setup(view);
	}
	return view;
}

void CreateSettingViews(const SettingsPage &page, UI::ViewGroup *parent, ScreenManager *screenManager, std::vector<UI::View *> *views) {
	UI::View *prevView = nullptr;
	for (const SettingItem &item : page.Items()) {
		UI::View *view = CreateSettingView(item, page.DefaultCat(), prevView, parent, screenManager);
		if (views) {
			views->push_back(view);
		}
		if (view) {
			prevView = view;
		}
	}
}
