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

#include "base/logging.h"
#include "i18n/i18n.h"
#include "input/keycodes.h"
#include "input/input_state.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

#include "Core/HLE/sceCtrl.h"
#include "Core/System.h"
#include "Common/KeyMap.h"
#include "Core/Config.h"
#include "UI/ui_atlas.h"
#include "UI/ControlMappingScreen.h"
#include "UI/UIShader.h"
#include "UI/GameSettingsScreen.h"

class ControlMapper : public UI::LinearLayout {
public:
	ControlMapper(int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams = 0);

	virtual void Update(const InputState &input);

private:
	void Refresh();

	UI::EventReturn OnAdd(UI::EventParams &params);
	UI::EventReturn OnDelete(UI::EventParams &params);
	UI::EventReturn OnReplace(UI::EventParams &params);
	UI::EventReturn OnReplaceAll(UI::EventParams &params);

	void MappedCallback(KeyDef key);

	enum Action {
		NONE,
		REPLACEONE,
		REPLACEALL,
		ADD,
	};

	Action action_;
	int actionIndex_;
	int pspKey_;
	std::string keyName_;
	ScreenManager *scrm_;
	bool refresh_;
};

ControlMapper::ControlMapper(int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams)
	: UI::LinearLayout(UI::ORIENT_VERTICAL, layoutParams), action_(NONE), pspKey_(pspKey), keyName_(keyName), scrm_(scrm), refresh_(false) {
	Refresh();
}

void ControlMapper::Update(const InputState &input) {
	if (refresh_) {
		refresh_ = false;
		Refresh();
	}
}

void ControlMapper::Refresh() {
	Clear();
	I18NCategory *mc = GetI18NCategory("MappableControls");

	std::map<std::string, int> keyImages;
	keyImages["Circle"] = I_CIRCLE;
	keyImages["Cross"] = I_CROSS;
	keyImages["Square"] = I_SQUARE;
	keyImages["Triangle"] = I_TRIANGLE;
	keyImages["Start"] = I_START;
	keyImages["Select"] = I_SELECT;
	keyImages["L"] = I_L;
	keyImages["R"] = I_R;

	using namespace UI;

	float itemH = 45;

	LinearLayout *root = Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	root->SetSpacing(3.0f);

	const int padding = 4;

	auto iter = keyImages.find(keyName_);
	// First, look among images.
	if (iter != keyImages.end()) {
		Choice *c = root->Add(new Choice(iter->second, new LinearLayoutParams(200, itemH)));
		c->OnClick.Handle(this, &ControlMapper::OnReplaceAll);
	} else {
		// No image? Let's translate.
		Choice *c = new Choice(mc->T(keyName_.c_str()), new LinearLayoutParams(200, itemH));
		c->SetCentered(true);
		root->Add(c)->OnClick.Handle(this, &ControlMapper::OnReplaceAll);
	}

	LinearLayout *rightColumn = root->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	rightColumn->SetSpacing(2.0f);
	std::vector<KeyDef> mappings;
	KeyMap::KeyFromPspButton(pspKey_, &mappings);

	for (size_t i = 0; i < mappings.size(); i++) {
		std::string deviceName = GetDeviceName(mappings[i].deviceId);
		std::string keyName = KeyMap::GetKeyOrAxisName(mappings[i].keyCode);
		int image = -1;

		LinearLayout *row = rightColumn->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		row->SetSpacing(1.0f);

		Choice *c = row->Add(new Choice(deviceName + "." + keyName, new LinearLayoutParams(FILL_PARENT, itemH, 1.0f)));
		char tagbuf[16];
		sprintf(tagbuf, "%i", (int)i);
		c->SetTag(tagbuf);
		c->OnClick.Handle(this, &ControlMapper::OnReplace);

		Choice *d = row->Add(new Choice(" X ", new LayoutParams(FILL_PARENT, itemH)));
		d->SetTag(tagbuf);
		d->OnClick.Handle(this, &ControlMapper::OnDelete);

		Choice *p = row->Add(new Choice(" + ", new LayoutParams(FILL_PARENT, itemH)));
		p->OnClick.Handle(this, &ControlMapper::OnAdd);
	}

	if (mappings.size() == 0) {
		// look like an empty line
		Choice *c = rightColumn->Add(new Choice("", new LinearLayoutParams(FILL_PARENT, itemH)));
		c->OnClick.Handle(this, &ControlMapper::OnAdd);
	}
}

void ControlMapper::MappedCallback(KeyDef kdf) {
	switch (action_) {
	case ADD:
		KeyMap::SetKeyMapping(pspKey_, kdf, false);
		break;
	case REPLACEALL:
		KeyMap::SetKeyMapping(pspKey_, kdf, true);
		break;
	case REPLACEONE:
		KeyMap::g_controllerMap[pspKey_][actionIndex_] = kdf;
		break;
	default:
		;
	}
	refresh_ = true;
}

UI::EventReturn ControlMapper::OnReplace(UI::EventParams &params) {
	actionIndex_ = atoi(params.v->Tag().c_str());
	action_ = REPLACEONE;
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&ControlMapper::MappedCallback, this, placeholder::_1)));
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMapper::OnReplaceAll(UI::EventParams &params) {
	action_ = REPLACEALL;
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&ControlMapper::MappedCallback, this, placeholder::_1)));
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMapper::OnAdd(UI::EventParams &params) {
	action_ = ADD;
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&ControlMapper::MappedCallback, this, placeholder::_1)));
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMapper::OnDelete(UI::EventParams &params) {
	int index = atoi(params.v->Tag().c_str());
	KeyMap::g_controllerMap[pspKey_].erase(KeyMap::g_controllerMap[pspKey_].begin() + index);
	refresh_ = true;
	return UI::EVENT_DONE;
}

void ControlMappingScreen::CreateViews() {
	using namespace UI;

	I18NCategory *k = GetI18NCategory("KeyMapping");
	I18NCategory *d = GetI18NCategory("Dialog");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT));
	leftColumn->Add(new Choice(k->T("Clear All")))->OnClick.Handle(this, &ControlMappingScreen::OnClearMapping);
	leftColumn->Add(new Choice(k->T("Default All")))->OnClick.Handle(this, &ControlMappingScreen::OnDefaultMapping);
	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	leftColumn->Add(new Choice(d->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	ScrollView *rightScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll->SetScrollToTop(false);
	LinearLayout *rightColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll->Add(rightColumn);

	root_->Add(leftColumn);
	root_->Add(rightScroll);

	std::vector<KeyMap::KeyMap_IntStrPair> mappableKeys = KeyMap::GetMappableKeys();
	for (size_t i = 0; i < mappableKeys.size(); i++) {
		rightColumn->Add(new ControlMapper(mappableKeys[i].key, mappableKeys[i].name, screenManager(), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	}
}

void ControlMappingScreen::sendMessage(const char *message, const char *value) {
	// Always call the base class method first to handle the most common messages.
	UIDialogScreenWithBackground::sendMessage(message, value);

	if (!strcmp(message, "settings")) {
		UpdateUIState(UISTATE_MENU);
		screenManager()->push(new GameSettingsScreen(""));
	}
}

UI::EventReturn ControlMappingScreen::OnClearMapping(UI::EventParams &params) {
	KeyMap::g_controllerMap.clear();
	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMappingScreen::OnDefaultMapping(UI::EventParams &params) {
	KeyMap::RestoreDefault();
	RecreateViews();
	return UI::EVENT_DONE;
}

void KeyMappingNewKeyDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	I18NCategory *keyI18N = GetI18NCategory("KeyMapping");

	std::string pspButtonName = KeyMap::GetPspButtonName(this->pspBtn_);

	parent->Add(new TextView(std::string(keyI18N->T("Map a new key for")) + " " + pspButtonName, new LinearLayoutParams(Margins(10,0))));
}

void KeyMappingNewKeyDialog::key(const KeyInput &key) {
	if (key.flags & KEY_DOWN) {
		if (key.keyCode == NKCODE_EXT_MOUSEBUTTON_1) {
			return;
		}

		KeyDef kdf(key.deviceId, key.keyCode);
		screenManager()->finishDialog(this, DR_OK);
		if (callback_)
			callback_(kdf);
	}
}

void KeyMappingNewKeyDialog::axis(const AxisInput &axis) {
	switch (axis.axisId) {
	// Ignore the accelerometer for mapping for now.
	case JOYSTICK_AXIS_ACCELEROMETER_X:
	case JOYSTICK_AXIS_ACCELEROMETER_Y:
	case JOYSTICK_AXIS_ACCELEROMETER_Z:
		return;

	// Also ignore some weird axis events we get on Ouya.
	case JOYSTICK_AXIS_OUYA_UNKNOWN1:
	case JOYSTICK_AXIS_OUYA_UNKNOWN2:
	case JOYSTICK_AXIS_OUYA_UNKNOWN3:
	case JOYSTICK_AXIS_OUYA_UNKNOWN4:
		return;

	default:
		;
	}

	if (axis.value > AXIS_BIND_THRESHOLD) {
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, 1));
		screenManager()->finishDialog(this, DR_OK);
		if (callback_)
			callback_(kdf);
	}

	if (axis.value < -AXIS_BIND_THRESHOLD) {
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, -1));
		screenManager()->finishDialog(this, DR_OK);
		if (callback_)
			callback_(kdf);
	}
}
