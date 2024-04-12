// Copyright (c) 2013- PPSSPP Project.

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

#include "Common/Render/TextureAtlas.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"

#include "UI/TouchControlVisibilityScreen.h"
#include "UI/CustomButtonMappingScreen.h"

static const int leftColumnWidth = 140;

class CheckBoxChoice : public UI::Choice {
public:
	CheckBoxChoice(std::string_view text, UI::CheckBox *checkbox, UI::LayoutParams *lp)
		: Choice(text, lp), checkbox_(checkbox) {
		OnClick.Handle(this, &CheckBoxChoice::HandleClick);
	}
	CheckBoxChoice(ImageID imgID, UI::CheckBox *checkbox, UI::LayoutParams *lp)
		: Choice(imgID, lp), checkbox_(checkbox) {
		OnClick.Handle(this, &CheckBoxChoice::HandleClick);
	}

private:
	UI::EventReturn HandleClick(UI::EventParams &e);

	UI::CheckBox *checkbox_;
};

void TouchControlVisibilityScreen::CreateViews() {
	using namespace UI;
	using namespace CustomKeyData;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth - 10, WRAP_CONTENT, 10, NONE, NONE, 10));
	root_->Add(back)->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	Choice *toggleAll = new Choice(di->T("Toggle All"), "", false, new AnchorLayoutParams(leftColumnWidth - 10, WRAP_CONTENT, 10, NONE, NONE, 84));
	root_->Add(toggleAll)->OnClick.Handle(this, &TouchControlVisibilityScreen::OnToggleAll);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, leftColumnWidth, new AnchorLayoutParams(10, 0, 10, 0, false));
	tabHolder->SetTag("TouchControlVisibility");
	root_->Add(tabHolder);
	ScrollView *rightPanel = new ScrollView(ORIENT_VERTICAL);
	tabHolder->AddTab(co->T("Visibility"), rightPanel);

	LinearLayout *vert = rightPanel->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	vert->SetSpacing(0);

	vert->Add(new ItemHeader(co->T("Touch Control Visibility")));

	const int cellSize = 380;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	toggles_.clear();
	toggles_.push_back({ "Circle", &g_Config.bShowTouchCircle, ImageID("I_CIRCLE"), nullptr });
	toggles_.push_back({ "Cross", &g_Config.bShowTouchCross, ImageID("I_CROSS"), nullptr });
	toggles_.push_back({ "Square", &g_Config.bShowTouchSquare, ImageID("I_SQUARE"), nullptr });
	toggles_.push_back({ "Triangle", &g_Config.bShowTouchTriangle, ImageID("I_TRIANGLE"), nullptr });
	toggles_.push_back({ "L", &g_Config.touchLKey.show, ImageID("I_L"), nullptr });
	toggles_.push_back({ "R", &g_Config.touchRKey.show, ImageID("I_R"), nullptr });
	toggles_.push_back({ "Start", &g_Config.touchStartKey.show, ImageID("I_START"), nullptr });
	toggles_.push_back({ "Select", &g_Config.touchSelectKey.show, ImageID("I_SELECT"), nullptr });
	toggles_.push_back({ "Dpad", &g_Config.touchDpad.show, ImageID::invalid(), nullptr });
	toggles_.push_back({ "Analog Stick", &g_Config.touchAnalogStick.show, ImageID::invalid(), nullptr });
	toggles_.push_back({ "Right Analog Stick", &g_Config.touchRightAnalogStick.show, ImageID::invalid(), [=](EventParams &e) {
		screenManager()->push(new RightAnalogMappingScreen(gamePath_));
		return UI::EVENT_DONE;
	}});
	toggles_.push_back({ "Fast-forward", &g_Config.touchFastForwardKey.show, ImageID::invalid(), nullptr });

	for (int i = 0; i < Config::CUSTOM_BUTTON_COUNT; i++) {
		char temp[256];
		snprintf(temp, sizeof(temp), "Custom %d", i + 1);
		toggles_.push_back({ temp, &g_Config.touchCustom[i].show, ImageID::invalid(), [=](EventParams &e) {
			screenManager()->push(new CustomButtonMappingScreen(gamePath_, i));
			return UI::EVENT_DONE;
		} });
	}

	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);
	for (auto toggle : toggles_) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(toggle.show, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);
		Choice *choice;
		if (toggle.handle) {
			// Handle custom button strings differently, and hackily. But will extend to arbitrary button counts.
			char translated[256];
			int i = 0;
			if (sscanf(toggle.key.c_str(), "Custom %d", &i) == 1) {
				snprintf(translated, sizeof(translated), mc->T_cstr("Custom %d"), i);
			} else {
				truncate_cpy(translated, sizeof(translated), mc->T(toggle.key));
			}
			choice = new Choice(std::string(translated) + " (" + std::string(mc->T("tap to customize")) + ")", "", false, new LinearLayoutParams(1.0f));
			choice->OnClick.Add(toggle.handle);
		} else if (toggle.img.isValid()) {
			choice = new CheckBoxChoice(toggle.img, checkbox, new LinearLayoutParams(1.0f));
		} else {
			choice = new CheckBoxChoice(mc->T(toggle.key), checkbox, new LinearLayoutParams(1.0f));
		}
		choice->SetCentered(true);
		row->Add(choice);
		grid->Add(row);
	}
}

void TouchControlVisibilityScreen::onFinish(DialogResult result) {
	g_Config.Save("TouchControlVisibilityScreen::onFinish");
}

void RightAnalogMappingScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth - 10, WRAP_CONTENT, 10, NONE, NONE, 10));
	root_->Add(back)->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, leftColumnWidth, new AnchorLayoutParams(10, 0, 10, 0, false));
	root_->Add(tabHolder);
	ScrollView *rightPanel = new ScrollView(ORIENT_VERTICAL);
	tabHolder->AddTab(co->T("Binds"), rightPanel);
	LinearLayout *vert = rightPanel->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	vert->SetSpacing(0);

	static const char *rightAnalogButton[] = {"None", "L", "R", "Square", "Triangle", "Circle", "Cross", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Select"};
	
	vert->Add(new ItemHeader(co->T("Analog Style")));
	vert->Add(new CheckBox(&g_Config.touchRightAnalogStick.show, co->T("Visible")));
	vert->Add(new CheckBox(&g_Config.bRightAnalogCustom, co->T("Use custom right analog")));
	vert->Add(new CheckBox(&g_Config.bRightAnalogDisableDiagonal, co->T("Disable diagonal input")))->SetEnabledPtr(&g_Config.bRightAnalogCustom);

	vert->Add(new ItemHeader(co->T("Analog Binding")));
	PopupMultiChoice *rightAnalogUp = vert->Add(new PopupMultiChoice(&g_Config.iRightAnalogUp, mc->T("RightAn.Up"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()));
	PopupMultiChoice *rightAnalogDown = vert->Add(new PopupMultiChoice(&g_Config.iRightAnalogDown, mc->T("RightAn.Down"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()));
	PopupMultiChoice *rightAnalogLeft = vert->Add(new PopupMultiChoice(&g_Config.iRightAnalogLeft, mc->T("RightAn.Left"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()));
	PopupMultiChoice *rightAnalogRight = vert->Add(new PopupMultiChoice(&g_Config.iRightAnalogRight, mc->T("RightAn.Right"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()));
	PopupMultiChoice *rightAnalogPress = vert->Add(new PopupMultiChoice(&g_Config.iRightAnalogPress, co->T("Keep this button pressed when right analog is pressed"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()));
	rightAnalogUp->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	rightAnalogDown->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	rightAnalogLeft->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	rightAnalogRight->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	rightAnalogPress->SetEnabledPtr(&g_Config.bRightAnalogCustom);
}

UI::EventReturn TouchControlVisibilityScreen::OnToggleAll(UI::EventParams &e) {
	for (auto toggle : toggles_) {
		*toggle.show = nextToggleAll_;
	}
	nextToggleAll_ = !nextToggleAll_;

	return UI::EVENT_DONE;
}

UI::EventReturn CheckBoxChoice::HandleClick(UI::EventParams &e) {
	checkbox_->Toggle();

	return UI::EVENT_DONE;
};
