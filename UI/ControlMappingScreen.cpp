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

#include "ppsspp_config.h"
#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "Common/Render/TextureAtlas.h"
#include "Common/UI/Root.h"
#include "Common/UI/UI.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/VR/PPSSPPVR.h"

#include "Common/Log.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Input/InputState.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/TimeUtil.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/JoystickHistoryView.h"
#include "UI/OnScreenDisplay.h"

#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/app-android.h"
#endif

using KeyMap::MultiInputMapping;

class SingleControlMapper : public UI::LinearLayout {
public:
	SingleControlMapper(int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams = nullptr);

	int GetPspKey() const { return pspKey_; }

private:
	void Refresh();

	UI::EventReturn OnAdd(UI::EventParams &params);
	UI::EventReturn OnAddMouse(UI::EventParams &params);
	UI::EventReturn OnDelete(UI::EventParams &params);
	UI::EventReturn OnReplace(UI::EventParams &params);
	UI::EventReturn OnReplaceAll(UI::EventParams &params);

	void MappedCallback(const MultiInputMapping &key);

	enum Action {
		NONE,
		REPLACEONE,
		REPLACEALL,
		ADD,
	};

	UI::Choice *addButton_ = nullptr;
	UI::Choice *replaceAllButton_ = nullptr;
	std::vector<UI::View *> rows_;
	Action action_ = NONE;
	int actionIndex_;
	int pspKey_;
	std::string keyName_;
	ScreenManager *scrm_;
};

SingleControlMapper::SingleControlMapper(int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams)
	: UI::LinearLayout(UI::ORIENT_VERTICAL, layoutParams), pspKey_(pspKey), keyName_(keyName), scrm_(scrm) {
	Refresh();
}

void SingleControlMapper::Refresh() {
	Clear();
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	std::map<std::string, ImageID> keyImages = {
		{ "Circle",   ImageID("I_CIRCLE")   },
		{ "Cross",    ImageID("I_CROSS")    },
		{ "Square",   ImageID("I_SQUARE")   },
		{ "Triangle", ImageID("I_TRIANGLE") },
		{ "Start",    ImageID("I_START")    },
		{ "Select",   ImageID("I_SELECT")   },
		{ "L",        ImageID("I_L")        },
		{ "R",        ImageID("I_R")        }
	};

	using namespace UI;

	float itemH = 55.0f;

	float leftColumnWidth = 200;
	float rightColumnWidth = 350;  // TODO: Should be flexible somehow. Maybe we need to implement Measure.

	LinearLayout *root = Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	root->SetSpacing(3.0f);

	auto iter = keyImages.find(keyName_);
	// First, look among images.
	if (iter != keyImages.end()) {
		replaceAllButton_ = new Choice(iter->second, new LinearLayoutParams(leftColumnWidth, itemH));
	} else {
		// No image? Let's translate.
		replaceAllButton_ = new Choice(mc->T(keyName_.c_str()), new LinearLayoutParams(leftColumnWidth, itemH));
		replaceAllButton_->SetCentered(true);
	}
	root->Add(replaceAllButton_)->OnClick.Handle(this, &SingleControlMapper::OnReplaceAll);

	addButton_ = root->Add(new Choice(" + ", new LayoutParams(WRAP_CONTENT, itemH)));
	addButton_->OnClick.Handle(this, &SingleControlMapper::OnAdd);
	if (g_Config.bMouseControl) {
		Choice *p = root->Add(new Choice("M", new LayoutParams(WRAP_CONTENT, itemH)));
		p->OnClick.Handle(this, &SingleControlMapper::OnAddMouse);
	}

	LinearLayout *rightColumn = root->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(rightColumnWidth, WRAP_CONTENT)));
	rightColumn->SetSpacing(2.0f);
	std::vector<MultiInputMapping> mappings;
	KeyMap::InputMappingsFromPspButton(pspKey_, &mappings, false);

	rows_.clear();
	for (size_t i = 0; i < mappings.size(); i++) {
		std::string multiMappingString = mappings[i].ToVisualString();
		LinearLayout *row = rightColumn->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		row->SetSpacing(2.0f);
		rows_.push_back(row);

		Choice *c = row->Add(new Choice(multiMappingString, new LinearLayoutParams(FILL_PARENT, itemH, 1.0f)));
		c->SetTag(StringFromFormat("%d_Change%d", (int)i, pspKey_));
		c->OnClick.Handle(this, &SingleControlMapper::OnReplace);

		Choice *d = row->Add(new Choice(ImageID("I_TRASHCAN"), new LayoutParams(WRAP_CONTENT, itemH)));
		d->SetTag(StringFromFormat("%d_Del%d", (int)i, pspKey_));
		d->OnClick.Handle(this, &SingleControlMapper::OnDelete);
	}

	if (mappings.size() == 0) {
		// look like an empty line
		Choice *c = rightColumn->Add(new Choice("", new LinearLayoutParams(FILL_PARENT, itemH)));
		c->OnClick.Handle(this, &SingleControlMapper::OnAdd);
	}
}

void SingleControlMapper::MappedCallback(const MultiInputMapping &kdf) {
	if (kdf.empty()) {
		// Don't want to try to add this.
		return;
	}

	switch (action_) {
	case ADD:
		KeyMap::SetInputMapping(pspKey_, kdf, false);
		addButton_->SetFocus();
		break;
	case REPLACEALL:
		KeyMap::SetInputMapping(pspKey_, kdf, true);
		replaceAllButton_->SetFocus();
		break;
	case REPLACEONE:
	{
		bool success = KeyMap::ReplaceSingleKeyMapping(pspKey_, actionIndex_, kdf);
		if (!success) {
			replaceAllButton_->SetFocus(); // Last got removed as a duplicate
		} else if (actionIndex_ < (int)rows_.size()) {
			rows_[actionIndex_]->SetFocus();
		} else {
			SetFocus();
		}
		break;
	}
	default:
		SetFocus();
		break;
	}
	KeyMap::UpdateNativeMenuKeys();
	g_Config.bMapMouse = false;
}

UI::EventReturn SingleControlMapper::OnReplace(UI::EventParams &params) {
	actionIndex_ = atoi(params.v->Tag().c_str());
	action_ = REPLACEONE;
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&SingleControlMapper::MappedCallback, this, std::placeholders::_1), I18NCat::KEYMAPPING));
	return UI::EVENT_DONE;
}

UI::EventReturn SingleControlMapper::OnReplaceAll(UI::EventParams &params) {
	action_ = REPLACEALL;
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&SingleControlMapper::MappedCallback, this, std::placeholders::_1), I18NCat::KEYMAPPING));
	return UI::EVENT_DONE;
}

UI::EventReturn SingleControlMapper::OnAdd(UI::EventParams &params) {
	action_ = ADD;
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&SingleControlMapper::MappedCallback, this, std::placeholders::_1), I18NCat::KEYMAPPING));
	return UI::EVENT_DONE;
}
UI::EventReturn SingleControlMapper::OnAddMouse(UI::EventParams &params) {
	action_ = ADD;
	g_Config.bMapMouse = true;
	scrm_->push(new KeyMappingNewMouseKeyDialog(pspKey_, true, std::bind(&SingleControlMapper::MappedCallback, this, std::placeholders::_1), I18NCat::KEYMAPPING));
	return UI::EVENT_DONE;
}

UI::EventReturn SingleControlMapper::OnDelete(UI::EventParams &params) {
	int index = atoi(params.v->Tag().c_str());
	KeyMap::DeleteNthMapping(pspKey_, index);

	if (index + 1 < (int)rows_.size())
		rows_[index]->SetFocus();
	else
		SetFocus();
	return UI::EVENT_DONE;
}


struct BindingCategory {
	const char *catName;
	int firstKey;
};

// Category name, first input from psp_button_names.
static const BindingCategory cats[] = {
	{"Standard PSP controls", CTRL_UP},
	{"Control modifiers", VIRTKEY_ANALOG_ROTATE_CW},
	{"Emulator controls", VIRTKEY_FASTFORWARD},
	{"Extended PSP controls", VIRTKEY_AXIS_RIGHT_Y_MAX},
	{},  // sentinel
};

void ControlMappingScreen::CreateViews() {
	using namespace UI;
	mappers_.clear();

	auto km = GetI18NCategory(I18NCat::KEYMAPPING);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT, Margins(10, 0, 0, 10)));
	leftColumn->Add(new Choice(km->T("Clear All")))->OnClick.Add([](UI::EventParams &) {
		KeyMap::ClearAllMappings();
		return UI::EVENT_DONE;
	});
	leftColumn->Add(new Choice(km->T("Default All")))->OnClick.Add([](UI::EventParams &) {
		KeyMap::RestoreDefault();
		return UI::EVENT_DONE;
	});
	std::string sysName = System_GetProperty(SYSPROP_NAME);
	// If there's a builtin controller, restore to default should suffice. No need to conf the controller on top.
	if (!KeyMap::HasBuiltinController(sysName) && KeyMap::GetSeenPads().size()) {
		leftColumn->Add(new Choice(km->T("Autoconfigure")))->OnClick.Handle(this, &ControlMappingScreen::OnAutoConfigure);
	}

	leftColumn->Add(new Choice(km->T("Show PSP")))->OnClick.Add([=](UI::EventParams &) {
		screenManager()->push(new VisualMappingScreen(gamePath_));
		return UI::EVENT_DONE;
	});
	leftColumn->Add(new CheckBox(&g_Config.bAllowMappingCombos, km->T("Allow combo mappings")));
	leftColumn->Add(new CheckBox(&g_Config.bStrictComboOrder, km->T("Strict combo input order")));

	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	AddStandardBack(leftColumn);

	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll_->SetTag("ControlMapping");
	LinearLayout *rightColumn = new LinearLayoutList(ORIENT_VERTICAL);
	rightScroll_->Add(rightColumn);

	root_->Add(leftColumn);
	root_->Add(rightScroll_);

	size_t numMappableKeys = 0;
	const KeyMap::KeyMap_IntStrPair *mappableKeys = KeyMap::GetMappableKeys(&numMappableKeys);

	int curCat = -1;
	CollapsibleSection *curSection = nullptr;
	for (size_t i = 0; i < numMappableKeys; i++) {
		if (curCat < (int)ARRAY_SIZE(cats) && mappableKeys[i].key == cats[curCat + 1].firstKey) {
			if (curCat >= 0) {
				curSection->SetOpenPtr(&categoryToggles_[curCat]);
			}
			curCat++;
			curSection = rightColumn->Add(new CollapsibleSection(km->T(cats[curCat].catName)));
			curSection->SetSpacing(6.0f);
		}
		SingleControlMapper *mapper = curSection->Add(
			new SingleControlMapper(mappableKeys[i].key, mappableKeys[i].name, screenManager(),
				                    new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		mapper->SetTag(StringFromFormat("KeyMap%s", mappableKeys[i].name));
		mappers_.push_back(mapper);
	}
	if (curCat >= 0 && curSection) {
		curSection->SetOpenPtr(&categoryToggles_[curCat]);
	}
	_dbg_assert_(curCat == ARRAY_SIZE(cats) - 2);  // count the sentinel

	keyMapGeneration_ = KeyMap::g_controllerMapGeneration;
}

void ControlMappingScreen::update() {
	if (KeyMap::HasChanged(keyMapGeneration_)) {
		RecreateViews();
	}

	UIDialogScreenWithGameBackground::update();
	SetVRAppMode(VRAppMode::VR_MENU_MODE);
}

UI::EventReturn ControlMappingScreen::OnAutoConfigure(UI::EventParams &params) {
	std::vector<std::string> items;
	const auto seenPads = KeyMap::GetSeenPads();
	for (auto s = seenPads.begin(), end = seenPads.end(); s != end; ++s) {
		items.push_back(*s);
	}
	auto km = GetI18NCategory(I18NCat::KEYMAPPING);
	UI::ListPopupScreen *autoConfList = new UI::ListPopupScreen(km->T("Autoconfigure for device"), items, -1);
	if (params.v)
		autoConfList->SetPopupOrigin(params.v);
	screenManager()->push(autoConfList);
	return UI::EVENT_DONE;
}

void ControlMappingScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_OK && std::string(dialog->tag()) == "listpopup") {
		UI::ListPopupScreen *popup = (UI::ListPopupScreen *)dialog;
		KeyMap::AutoConfForPad(popup->GetChoiceString());
	}
}

void KeyMappingNewKeyDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto km = GetI18NCategory(I18NCat::KEYMAPPING);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	std::string pspButtonName = KeyMap::GetPspButtonName(this->pspBtn_);

	parent->Add(new TextView(std::string(km->T("Map a new key for")) + " " + std::string(mc->T(pspButtonName)), new LinearLayoutParams(Margins(10, 0))));
	parent->Add(new TextView(std::string(mapping_.ToVisualString()), new LinearLayoutParams(Margins(10, 0))));

	comboMappingsNotEnabled_ = parent->Add(new NoticeView(NoticeLevel::WARN, km->T("Combo mappings are not enabled"), "", new LinearLayoutParams(Margins(10, 0))));
	comboMappingsNotEnabled_->SetVisibility(UI::V_GONE);

	SetVRAppMode(VRAppMode::VR_CONTROLLER_MAPPING_MODE);
}

bool KeyMappingNewKeyDialog::key(const KeyInput &key) {
	if (ignoreInput_)
		return true;
	if (time_now_d() < delayUntil_)
		return true;

	if (key.flags & KEY_DOWN) {
		if (key.keyCode == NKCODE_EXT_MOUSEBUTTON_1) {
			// Don't map
			return true;
		}

		if (pspBtn_ == VIRTKEY_SPEED_ANALOG && !UI::IsEscapeKey(key)) {
			// Only map analog values to this mapping.
			return true;
		}

		InputMapping newMapping(key.deviceId, key.keyCode);

		if (!(key.flags & KEY_IS_REPEAT)) {
			if (!g_Config.bAllowMappingCombos && !mapping_.mappings.empty()) {
				comboMappingsNotEnabled_->SetVisibility(UI::V_VISIBLE);
			} else if (!mapping_.mappings.contains(newMapping)) {
				mapping_.mappings.push_back(newMapping);
				RecreateViews();
			}
		}
	}
	if (key.flags & KEY_UP) {
		// If the key released wasn't part of the mapping, ignore it here. Some device can cause
		// stray key-up events.
		InputMapping upMapping(key.deviceId, key.keyCode);
		if (!mapping_.mappings.contains(upMapping)) {
			return true;
		}

		if (callback_)
			callback_(mapping_);
		TriggerFinish(DR_YES);
	}
	return true;
}

void KeyMappingNewKeyDialog::SetDelay(float t) {
	delayUntil_ = time_now_d() + t;
}

void KeyMappingNewMouseKeyDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto km = GetI18NCategory(I18NCat::KEYMAPPING);

	parent->Add(new TextView(std::string(km->T("You can press ESC to cancel.")), new LinearLayoutParams(Margins(10, 0))));
	SetVRAppMode(VRAppMode::VR_CONTROLLER_MAPPING_MODE);
}

bool KeyMappingNewMouseKeyDialog::key(const KeyInput &key) {
	if (mapped_)
		return false;
	if (ignoreInput_)
		return true;
	if (key.flags & KEY_DOWN) {
		if (key.keyCode == NKCODE_ESCAPE) {
			TriggerFinish(DR_OK);
			g_Config.bMapMouse = false;
			return false;
		}

		mapped_ = true;

		TriggerFinish(DR_YES);
		g_Config.bMapMouse = false;
		if (callback_) {
			MultiInputMapping kdf(InputMapping(key.deviceId, key.keyCode));
			callback_(kdf);
		}
	}
	return true;
}

// Only used during the bind process. In other places, it's configurable for some types of axis, like trigger.
const float AXIS_BIND_THRESHOLD = 0.75f;
const float AXIS_BIND_RELEASE_THRESHOLD = 0.35f;  // Used during mapping only to detect a "key-up" reliably.

void KeyMappingNewKeyDialog::axis(const AxisInput &axis) {
	if (time_now_d() < delayUntil_)
		return;
	if (ignoreInput_)
		return;

	if (axis.value > AXIS_BIND_THRESHOLD) {
		InputMapping mapping(axis.deviceId, axis.axisId, 1);
		triggeredAxes_.insert(mapping);
		if (!g_Config.bAllowMappingCombos && !mapping_.mappings.empty()) {
			if (mapping_.mappings.size() == 1 && mapping != mapping_.mappings[0])
				comboMappingsNotEnabled_->SetVisibility(UI::V_VISIBLE);
		} else if (!mapping_.mappings.contains(mapping)) {
			mapping_.mappings.push_back(mapping);
			RecreateViews();
		}
	} else if (axis.value < -AXIS_BIND_THRESHOLD) {
		InputMapping mapping(axis.deviceId, axis.axisId, -1);
		triggeredAxes_.insert(mapping);
		if (!g_Config.bAllowMappingCombos && !mapping_.mappings.empty()) {
			if (mapping_.mappings.size() == 1 && mapping != mapping_.mappings[0])
				comboMappingsNotEnabled_->SetVisibility(UI::V_VISIBLE);
		} else if (!mapping_.mappings.contains(mapping)) {
			mapping_.mappings.push_back(mapping);
			RecreateViews();
		}
	} else if (fabsf(axis.value) < AXIS_BIND_RELEASE_THRESHOLD) {
		InputMapping neg(axis.deviceId, axis.axisId, -1);
		InputMapping pos(axis.deviceId, axis.axisId, 1);
		if (triggeredAxes_.find(neg) != triggeredAxes_.end() || triggeredAxes_.find(pos) != triggeredAxes_.end()) {
			// "Key-up" the axis.
			TriggerFinish(DR_YES);
			if (callback_)
				callback_(mapping_);
		}
	}
}

void KeyMappingNewMouseKeyDialog::axis(const AxisInput &axis) {
	if (mapped_)
		return;

	if (axis.value > AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		TriggerFinish(DR_YES);
		if (callback_) {
			MultiInputMapping kdf(InputMapping(axis.deviceId, axis.axisId, 1));
			callback_(kdf);
		}
	}

	if (axis.value < -AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		TriggerFinish(DR_YES);
		if (callback_) {
			MultiInputMapping kdf(InputMapping(axis.deviceId, axis.axisId, -1));
			callback_(kdf);
		}
	}
}

AnalogSetupScreen::AnalogSetupScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {
	mapper_.SetCallbacks(
		[](int vkey, bool down) {},
		[](int vkey, float analogValue) {},
		[&](uint32_t bitsToSet, uint32_t bitsToClear) {},
		[&](int stick, float x, float y) {
			analogX_[stick] = x;
			analogY_[stick] = y;
		},
		[&](int stick, float x, float y) {
			rawX_[stick] = x;
			rawY_[stick] = y;
		});
}

void AnalogSetupScreen::update() {
	mapper_.Update(time_now_d());
	// We ignore the secondary stick for now and just use the two views
	// for raw and psp input.
	if (stickView_[0]) {
		stickView_[0]->SetXY(analogX_[0], analogY_[0]);
	}
	if (stickView_[1]) {
		stickView_[1]->SetXY(rawX_[0], rawY_[0]);
	}
	UIScreen::update();
}

bool AnalogSetupScreen::key(const KeyInput &key) {
	bool retval = UIScreen::key(key);

	// Allow testing auto-rotation. If it collides with UI keys, too bad.
	bool pauseTrigger = false;
	mapper_.Key(key, &pauseTrigger);

	if (UI::IsEscapeKey(key)) {
		TriggerFinish(DR_BACK);
		return retval;
	}
	return retval;
}

void AnalogSetupScreen::axis(const AxisInput &axis) {
	// We DON'T call UIScreen::Axis here! Otherwise it'll try to move the UI focus around.
	// UIScreen::axis(axis);

	// Instead we just send the input directly to the mapper, that we'll visualize.
	mapper_.Axis(&axis, 1);
}

void AnalogSetupScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	LinearLayout *leftColumn = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300.0f, FILL_PARENT)));
	LinearLayout *rightColumn = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));

	auto co = GetI18NCategory(I18NCat::CONTROLS);
	ScrollView *scroll = leftColumn->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));

	LinearLayout *scrollContents = scroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300.0f, WRAP_CONTENT)));

	scrollContents->Add(new ItemHeader(co->T("Analog Settings", "Analog Settings")));

	// TODO: Would be nicer if these didn't pop up...
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogDeadzone, 0.0f, 0.5f, 0.15f, co->T("Deadzone radius"), 0.01f, screenManager(), "/ 1.0"));
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogInverseDeadzone, 0.0f, 1.0f, 0.0f, co->T("Low end radius"), 0.01f, screenManager(), "/ 1.0"));
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogSensitivity, 0.0f, 2.0f, 1.1f, co->T("Sensitivity (scale)", "Sensitivity"), 0.01f, screenManager(), "x"));
	// TODO: This should probably be a slider.
	scrollContents->Add(new CheckBox(&g_Config.bAnalogIsCircular, co->T("Circular stick input")));
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogAutoRotSpeed, 0.1f, 20.0f, 8.0f, co->T("Auto-rotation speed"), 1.0f, screenManager()));
	scrollContents->Add(new Choice(co->T("Reset to defaults")))->OnClick.Handle(this, &AnalogSetupScreen::OnResetToDefaults);

	LinearLayout *theTwo = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f));

	stickView_[0] = theTwo->Add(new JoystickHistoryView(StickHistoryViewType::OUTPUT, co->T("Calibrated"), new LinearLayoutParams(1.0f)));
	stickView_[1] = theTwo->Add(new JoystickHistoryView(StickHistoryViewType::INPUT, co->T("Raw input"), new LinearLayoutParams(1.0f)));

	rightColumn->Add(theTwo);

	leftColumn->Add(new Button(di->T("Back"), new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

UI::EventReturn AnalogSetupScreen::OnResetToDefaults(UI::EventParams &e) {
	g_Config.fAnalogDeadzone = 0.15f;
	g_Config.fAnalogInverseDeadzone = 0.0f;
	g_Config.fAnalogSensitivity = 1.1f;
	g_Config.bAnalogIsCircular = false;
	g_Config.fAnalogAutoRotSpeed = 8.0f;
	return UI::EVENT_DONE;
}

class Backplate : public UI::InertView {
public:
	Backplate(float scale, UI::LayoutParams *layoutParams = nullptr) : InertView(layoutParams), scale_(scale) {}

	void Draw(UIContext &dc) override {
		for (float dy = 0.0f; dy <= 4.0f; dy += 1.0f) {
			for (float dx = 0.0f; dx <= 4.0f; dx += 1.0f) {
				if (dx == 2.0f && dy == 2.0f)
					continue;
				DrawPSP(dc, dx, dy, 0x06C1B6B6);
			}
		}
		DrawPSP(dc, 2.0f, 2.0f, 0xC01C1818);
	}

	void DrawPSP(UIContext &dc, float xoff, float yoff, uint32_t color) {
		using namespace UI;

		const AtlasImage *whiteImage = dc.Draw()->GetAtlas()->getImage(dc.theme->whiteImage);
		float centerU = (whiteImage->u1 + whiteImage->u2) * 0.5f;
		float centerV = (whiteImage->v1 + whiteImage->v2) * 0.5f;

		auto V = [&](float x, float y) {
			dc.Draw()->V(bounds_.x + (x + xoff) * scale_, bounds_.y + (y + yoff) * scale_, color, centerU, centerV);
		};
		auto R = [&](float x1, float y1, float x2, float y2) {
			V(x1, y1); V(x2, y1); V(x2, y2);
			V(x1, y1); V(x2, y2); V(x1, y2);
		};

		// Curved left side.
		V(12.0f, 44.0f); V(30.0f, 16.0f); V(30.0f, 44.0f);
		V(0.0f, 80.0f); V(12.0f, 44.0f); V(12.0f, 80.0f);
		R(12.0f, 44.0f, 30.0f, 80.0f);
		R(0.0f, 80.0f, 30.0f, 114.0f);
		V(0.0f, 114.0f); V(12.0f, 114.0f); V(12.0f, 154.0f);
		R(12.0f, 114.0f, 30.0f, 154.0f);
		V(12.0f, 154.0f); V(30.0f, 154.0f); V(30.0f, 180.0f);
		// Left side.
		V(30.0f, 16.0f); V(64.0f, 13.0f); V(64.0f, 184.0f);
		V(30.0f, 16.0f); V(64.0f, 184.0f); V(30.0f, 180.0f);
		V(64.0f, 13.0f); V(76.0f, 0.0f); V(76.0f, 13.0f);
		V(64.0f, 184.0f); V(76.0f, 200.0f); V(76.0f, 184.0f);
		R(64.0f, 13.0f, 76.0f, 184.0f);
		// Center.
		R(76.0f, 0.0f, 400.0f, 13.0f);
		R(76.0f, 167.0f, 400.0f, 200.0f);
		R(76.0f, 13.0f, 99.0f, 167.0f);
		R(377.0f, 13.0f, 400.0f, 167.0f);
		// Right side.
		V(400.0f, 0.0f); V(412.0f, 13.0f); V(400.0f, 13.0f);
		V(400.0f, 184.0f); V(412.0f, 184.0f); V(400.0f, 200.0f);
		R(400.0f, 13.0f, 412.0f, 184.0f);
		V(412.0f, 13.0f); V(446.0f, 16.0f); V(446.0f, 180.0f);
		V(412.0f, 13.0f); V(446.0f, 180.0f); V(412.0f, 184.0f);
		// Curved right side.
		V(446.0f, 16.0f); V(462.0f, 44.0f); V(446.0f, 44.0f);
		V(462.0f, 44.0f); V(474.0f, 80.0f); V(462.0f, 80.0f);
		R(446.0f, 44.0f, 462.0f, 80.0f);
		R(446.0f, 80.0f, 474.0f, 114.0f);
		V(462.0f, 114.0f); V(474.0f, 114.0f); V(462.0f, 154.0f);
		R(446.0f, 114.0f, 462.0f, 154.0f);
		V(446.0f, 154.0f); V(462.0f, 154.0f); V(446.0f, 180.0f);
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 478.0f * scale_;
		h = 204.0f * scale_;
	}

protected:
	float scale_ = 1.0f;
};

class MockScreen : public UI::InertView {
public:
	MockScreen(UI::LayoutParams *layoutParams = nullptr) : InertView(layoutParams) {
	}

	void Draw(UIContext &dc) override {
		ImageID bg = ImageID("I_PSP_DISPLAY");
		dc.Draw()->DrawImageStretch(bg, bounds_, 0x7FFFFFFF);
	}
};

class MockButton : public UI::Clickable {
public:
	MockButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *layoutParams = nullptr)
		: Clickable(layoutParams), button_(button), img_(img), bgImg_(bg), angle_(angle) {
	}

	void Draw(UIContext &dc) override {
		uint32_t c = 0xFFFFFFFF;
		if (HasFocus() || Selected())
			c = dc.theme->itemFocusedStyle.background.color;

		float scales[2]{};
		if (bgImg_.isValid())
			dc.Draw()->DrawImageRotatedStretch(bgImg_, bounds_, scales, angle_, c, flipHBG_);
		if (img_.isValid()) {
			scales[0] *= scaleX_;
			scales[1] *= scaleY_;
			if (timeLastPressed_ >= 0.0) {
				double sincePress = time_now_d() - timeLastPressed_;
				if (sincePress < 1.0) {
					c = colorBlend(c, dc.theme->itemDownStyle.background.color, (float)sincePress);
				}
			}
			dc.Draw()->DrawImageRotatedStretch(img_, bounds_.Offset(offsetX_, offsetY_), scales, angle_, c);
		}
	}

	MockButton *SetScale(float s) {
		scaleX_ = s;
		scaleY_ = s;
		return this;
	}

	MockButton *SetScale(float x, float y) {
		scaleX_ = x;
		scaleY_ = y;
		return this;
	}

	MockButton *SetFlipHBG(float f) {
		flipHBG_ = f;
		return this;
	}

	MockButton *SetOffset(float x, float y) {
		offsetX_ = x;
		offsetY_ = y;
		return this;
	}

	MockButton *SetSelectedButton(int *s) {
		selectedButton_ = s;
		return this;
	}

	bool Selected() {
		return selectedButton_ && *selectedButton_ == button_;
	}

	int Button() {
		return button_;
	}

	void NotifyPressed() {
		timeLastPressed_ = time_now_d();
	}

private:
	int button_;
	ImageID img_;
	ImageID bgImg_;
	float angle_;
	float scaleX_ = 1.0f;
	float scaleY_ = 1.0f;
	float offsetX_ = 0.0f;
	float offsetY_ = 0.0f;
	bool flipHBG_ = false;
	int *selectedButton_ = nullptr;
	double timeLastPressed_ = -1.0;
};

class MockPSP : public UI::AnchorLayout {
public:
	static constexpr float SCALE = 1.4f;

	MockPSP(UI::LayoutParams *layoutParams = nullptr);
	void SelectButton(int btn);
	void FocusButton(int btn);
	void NotifyPressed(int btn);
	float GetPopupOffset();

	bool SubviewFocused(View *view) override;

	UI::Event ButtonClick;

private:
	UI::AnchorLayoutParams *LayoutAt(float l, float t, float r, float b);
	UI::AnchorLayoutParams *LayoutSize(float w, float h, float l, float t);
	MockButton *AddButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *lp);

	UI::EventReturn OnSelectButton(UI::EventParams &e);

	std::unordered_map<int, MockButton *> buttons_;
	UI::TextView *labelView_ = nullptr;
	int selectedButton_ = 0;
};

MockPSP::MockPSP(UI::LayoutParams *layoutParams) : AnchorLayout(layoutParams) {
	Add(new Backplate(SCALE));
	Add(new MockScreen(LayoutAt(99.0f, 13.0f, 97.0f, 33.0f)));

	// Left side.
	AddButton(VIRTKEY_AXIS_Y_MAX, ImageID("I_STICK_LINE"), ImageID("I_STICK_BG_LINE"), 0.0f, LayoutSize(34.0f, 34.0f, 35.0f, 133.0f));
	AddButton(CTRL_LEFT, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 0.0f, LayoutSize(28.0f, 20.0f, 14.0f, 75.0f))->SetOffset(-4.0f * SCALE, 0.0f);
	AddButton(CTRL_UP, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 0.5f, LayoutSize(20.0f, 28.0f, 40.0f, 50.0f))->SetOffset(0.0f, -4.0f * SCALE);
	AddButton(CTRL_RIGHT, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 1.0f, LayoutSize(28.0f, 20.0f, 58.0f, 75.0f))->SetOffset(4.0f * SCALE, 0.0f);
	AddButton(CTRL_DOWN, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 1.5f, LayoutSize(20.0f, 28.0f, 40.0f, 92.0f))->SetOffset(0.0f, 4.0f * SCALE);

	// Top.
	AddButton(CTRL_LTRIGGER, ImageID("I_L"), ImageID("I_SHOULDER_LINE"), 0.0f, LayoutSize(50.0f, 16.0f, 29.0f, 0.0f));
	AddButton(CTRL_RTRIGGER, ImageID("I_R"), ImageID("I_SHOULDER_LINE"), 0.0f, LayoutSize(50.0f, 16.0f, 397.0f, 0.0f))->SetFlipHBG(true);

	// Bottom.
	AddButton(CTRL_HOME, ImageID("I_HOME"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 88.0f, 181.0f))->SetScale(1.0f, 0.65f);
	AddButton(CTRL_SELECT, ImageID("I_SELECT"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 330.0f, 181.0f));
	AddButton(CTRL_START, ImageID("I_START"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 361.0f, 181.0f));

	// Right side.
	AddButton(CTRL_TRIANGLE, ImageID("I_TRIANGLE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 419.0f, 46.0f))->SetScale(0.7f)->SetOffset(0.0f, -1.0f * SCALE);
	AddButton(CTRL_CIRCLE, ImageID("I_CIRCLE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 446.0f, 74.0f))->SetScale(0.7f);
	AddButton(CTRL_CROSS, ImageID("I_CROSS"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 419.0f, 102.0f))->SetScale(0.7f);
	AddButton(CTRL_SQUARE, ImageID("I_SQUARE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 392.0f, 74.0f))->SetScale(0.7f);

	labelView_ = Add(new UI::TextView(""));
	labelView_->SetShadow(true);
	labelView_->SetVisibility(UI::V_GONE);
}

void MockPSP::SelectButton(int btn) {
	selectedButton_ = btn;
}

void MockPSP::FocusButton(int btn) {
	MockButton *view = buttons_[btn];
	if (view) {
		view->SetFocus();
	} else {
		labelView_->SetVisibility(UI::V_GONE);
	}
}

void MockPSP::NotifyPressed(int btn) {
	MockButton *view = buttons_[btn];
	if (view)
		view->NotifyPressed();
}

bool MockPSP::SubviewFocused(View *view) {
	for (auto it : buttons_) {
		if (view == it.second) {
			labelView_->SetVisibility(UI::V_VISIBLE);
			labelView_->SetText(KeyMap::GetPspButtonName(it.first));

			const Bounds &pos = view->GetBounds().Offset(-GetBounds().x, -GetBounds().y);
			labelView_->ReplaceLayoutParams(new UI::AnchorLayoutParams(pos.centerX(), pos.y2() + 5, UI::NONE, UI::NONE));
		}
	}
	return AnchorLayout::SubviewFocused(view);
}

float MockPSP::GetPopupOffset() {
	MockButton *view = buttons_[selectedButton_];
	if (!view)
		return 0.0f;

	float ypos = view->GetBounds().centerY();
	if (ypos > bounds_.centerY()) {
		return -0.25f;
	}
	return 0.25f;
}

UI::AnchorLayoutParams *MockPSP::LayoutAt(float l, float t, float r, float b) {
	return new UI::AnchorLayoutParams(l * SCALE, t * SCALE, r * SCALE, b * SCALE);
}
UI::AnchorLayoutParams *MockPSP::LayoutSize(float w, float h, float l, float t) {
	return new UI::AnchorLayoutParams(w * SCALE, h * SCALE, l * SCALE, t * SCALE, UI::NONE, UI::NONE);
}

MockButton *MockPSP::AddButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *lp) {
	MockButton *view = Add(new MockButton(button, img, bg, angle, lp));
	view->OnClick.Handle(this, &MockPSP::OnSelectButton);
	view->SetSelectedButton(&selectedButton_);
	buttons_[button] = view;
	return view;
}

UI::EventReturn MockPSP::OnSelectButton(UI::EventParams &e) {
	auto view = (MockButton *)e.v;
	e.a = view->Button();
	return ButtonClick.Dispatch(e);
}

static std::vector<int> bindAllOrder{
	CTRL_LTRIGGER,
	CTRL_RTRIGGER,
	CTRL_UP,
	CTRL_DOWN,
	CTRL_LEFT,
	CTRL_RIGHT,
	VIRTKEY_AXIS_Y_MAX,
	VIRTKEY_AXIS_Y_MIN,
	VIRTKEY_AXIS_X_MIN,
	VIRTKEY_AXIS_X_MAX,
	CTRL_HOME,
	CTRL_SELECT,
	CTRL_START,
	CTRL_CROSS,
	CTRL_CIRCLE,
	CTRL_TRIANGLE,
	CTRL_SQUARE,
};

void VisualMappingScreen::CreateViews() {
	using namespace UI;

	auto km = GetI18NCategory(I18NCat::KEYMAPPING);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	constexpr float leftColumnWidth = 200.0f;
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(leftColumnWidth, FILL_PARENT, Margins(10, 0, 0, 10)));
	leftColumn->Add(new Choice(km->T("Bind All")))->OnClick.Handle(this, &VisualMappingScreen::OnBindAll);
	leftColumn->Add(new CheckBox(&replace_, km->T("Replace"), ""));

	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	AddStandardBack(leftColumn);

	Bounds bounds = screenManager()->getUIContext()->GetLayoutBounds();
	// Account for left side.
	bounds.w -= leftColumnWidth + 10.0f;

	AnchorLayout *rightColumn = new AnchorLayout(new LinearLayoutParams(bounds.w, FILL_PARENT, 1.0f));
	psp_ = rightColumn->Add(new MockPSP(new AnchorLayoutParams(bounds.centerX(), bounds.centerY(), NONE, NONE, true)));
	psp_->ButtonClick.Handle(this, &VisualMappingScreen::OnMapButton);

	root_->Add(leftColumn);
	root_->Add(rightColumn);
}

bool VisualMappingScreen::key(const KeyInput &key) {
	if (key.flags & KEY_DOWN) {
		std::vector<int> pspKeys;
		KeyMap::InputMappingToPspButton(InputMapping(key.deviceId, key.keyCode), &pspKeys);
		for (int pspKey : pspKeys) {
			switch (pspKey) {
			case VIRTKEY_AXIS_X_MIN:
			case VIRTKEY_AXIS_Y_MIN:
			case VIRTKEY_AXIS_X_MAX:
			case VIRTKEY_AXIS_Y_MAX:
				psp_->NotifyPressed(VIRTKEY_AXIS_Y_MAX);
				break;
			default:
				psp_->NotifyPressed(pspKey);
				break;
			}
		}
	}
	return UIDialogScreenWithGameBackground::key(key);
}

void VisualMappingScreen::axis(const AxisInput &axis) {
	std::vector<int> results;
	if (axis.value >= g_Config.fAnalogDeadzone * 0.7f)
		KeyMap::InputMappingToPspButton(InputMapping(axis.deviceId, axis.axisId, 1), &results);
	if (axis.value <= g_Config.fAnalogDeadzone * -0.7f)
		KeyMap::InputMappingToPspButton(InputMapping(axis.deviceId, axis.axisId, -1), &results);

	for (int result : results) {
		switch (result) {
		case VIRTKEY_AXIS_X_MIN:
		case VIRTKEY_AXIS_Y_MIN:
		case VIRTKEY_AXIS_X_MAX:
		case VIRTKEY_AXIS_Y_MAX:
			psp_->NotifyPressed(VIRTKEY_AXIS_Y_MAX);
			break;
		default:
			psp_->NotifyPressed(result);
			break;
		}
	}
	UIDialogScreenWithGameBackground::axis(axis);
}

void VisualMappingScreen::resized() {
	UIDialogScreenWithGameBackground::resized();
	RecreateViews();
}

UI::EventReturn VisualMappingScreen::OnMapButton(UI::EventParams &e) {
	nextKey_ = e.a;
	MapNext(false);
	return UI::EVENT_DONE;
}

UI::EventReturn VisualMappingScreen::OnBindAll(UI::EventParams &e) {
	bindAll_ = 0;
	nextKey_ = bindAllOrder[bindAll_];
	MapNext(false);
	return UI::EVENT_DONE;
}

void VisualMappingScreen::HandleKeyMapping(const KeyMap::MultiInputMapping &key) {
	KeyMap::SetInputMapping(nextKey_, key, replace_);
	KeyMap::UpdateNativeMenuKeys();

	if (bindAll_ < 0) {
		// For analog, we do each direction in a row.
		if (nextKey_ == VIRTKEY_AXIS_Y_MAX)
			nextKey_ = VIRTKEY_AXIS_Y_MIN;
		else if (nextKey_ == VIRTKEY_AXIS_Y_MIN)
			nextKey_ = VIRTKEY_AXIS_X_MIN;
		else if (nextKey_ == VIRTKEY_AXIS_X_MIN)
			nextKey_ = VIRTKEY_AXIS_X_MAX;
		else {
			if (nextKey_ == VIRTKEY_AXIS_X_MAX)
				psp_->FocusButton(VIRTKEY_AXIS_Y_MAX);
			else
				psp_->FocusButton(nextKey_);
			nextKey_ = 0;
		}
	} else if ((size_t)bindAll_ + 1 < bindAllOrder.size()) {
		bindAll_++;
		nextKey_ = bindAllOrder[bindAll_];
	} else {
		bindAll_ = -1;
		nextKey_ = 0;
	}
}

void VisualMappingScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_YES && nextKey_ != 0) {
		MapNext(true);
	} else {
		// This means they canceled.
		if (nextKey_ != 0)
			psp_->FocusButton(nextKey_);
		nextKey_ = 0;
		bindAll_ = -1;
		psp_->SelectButton(0);
	}
}

void VisualMappingScreen::MapNext(bool successive) {
	if (nextKey_ == VIRTKEY_AXIS_Y_MIN || nextKey_ == VIRTKEY_AXIS_X_MIN || nextKey_ == VIRTKEY_AXIS_X_MAX) {
		psp_->SelectButton(VIRTKEY_AXIS_Y_MAX);
	} else {
		psp_->SelectButton(nextKey_);
	}
	auto dialog = new KeyMappingNewKeyDialog(nextKey_, true, std::bind(&VisualMappingScreen::HandleKeyMapping, this, std::placeholders::_1), I18NCat::KEYMAPPING);

	Bounds bounds = screenManager()->getUIContext()->GetLayoutBounds();
	dialog->SetPopupOffset(psp_->GetPopupOffset() * bounds.h);
	dialog->SetDelay(successive ? 0.5f : 0.1f);
	screenManager()->push(dialog);
}
