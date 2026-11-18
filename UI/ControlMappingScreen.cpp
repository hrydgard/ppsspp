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

#include "Common/Render/TextureAtlas.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Notice.h"
#include "Common/UI/ScreenManager.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/Log.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Input/InputState.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/TimeUtil.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Config.h"
#include "UI/ControlMappingScreen.h"
#include "UI/VisualMappingScreen.h"
#include "UI/PopupScreens.h"
#include "UI/JoystickHistoryView.h"

#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/app-android.h"
#endif

using KeyMap::MultiInputMapping;

class SingleControlMapper : public UI::LinearLayout {
public:
	SingleControlMapper(int pspKey, std::string_view keyName, bool portrait, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams = nullptr)
		: UI::LinearLayout(ORIENT_VERTICAL, layoutParams), pspKey_(pspKey), keyName_(keyName), scrm_(scrm), portrait_(portrait) {
		Refresh();
	}
	~SingleControlMapper() {
		g_IsMappingMouseInput = false;
	}
	int GetPspKey() const { return pspKey_; }

private:
	void Refresh();

	void OnAdd(UI::EventParams &params);
	void OnAddMouse(UI::EventParams &params);
	void OnDelete(UI::EventParams &params);
	void OnReplace(UI::EventParams &params);
	void OnReplaceAll(UI::EventParams &params);

	int pspKey_;

	UI::Choice *addButton_ = nullptr;
	UI::Choice *replaceAllButton_ = nullptr;
	std::vector<UI::View *> rows_;
	std::string keyName_;
	ScreenManager *scrm_;
	bool portrait_;
};

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
		{ "Home",     ImageID("I_HOME")     },
		{ "L",        ImageID("I_L")        },
		{ "R",        ImageID("I_R")        }
	};

	using namespace UI;

	float itemH = 55.0f;

	float leftColumnWidth = 200;
	float rightColumnWidth = 350;

	LinearLayout *root = Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	root->SetPadding(Margins(0, 0, 12, 0));
	root->SetSpacing(3.0f);

	auto iter = keyImages.find(keyName_);
	// First, look among images.
	if (iter != keyImages.end()) {
		replaceAllButton_ = new Choice(iter->second, new LinearLayoutParams(leftColumnWidth, itemH));
	} else {
		// No image? Let's translate.
		replaceAllButton_ = new Choice(mc->T(keyName_), new LinearLayoutParams(leftColumnWidth, itemH));
		replaceAllButton_->SetCentered(true);
	}
	root->Add(replaceAllButton_)->OnClick.Handle(this, &SingleControlMapper::OnReplaceAll);

	addButton_ = root->Add(new Choice(ImageID("I_PLUS"), new LayoutParams(WRAP_CONTENT, itemH)));
	addButton_->OnClick.Handle(this, &SingleControlMapper::OnAdd);
	if (g_Config.bMouseControl) {
		Choice *p = root->Add(new Choice(ImageID("I_MOUSE"), new LayoutParams(WRAP_CONTENT, itemH)));
		p->OnClick.Handle(this, &SingleControlMapper::OnAddMouse);
	}

	LinearLayout *rightColumn = root->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
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

	if (mappings.empty()) {
		// look like an empty line
		Choice *c = rightColumn->Add(new Choice("", new LinearLayoutParams(FILL_PARENT, itemH)));
		c->OnClick.Handle(this, &SingleControlMapper::OnAdd);
	}
}

void SingleControlMapper::OnReplace(UI::EventParams &params) {
	const int index = atoi(params.v->Tag().c_str());
	std::vector<MultiInputMapping> mappings;
	KeyMap::InputMappingsFromPspButton(pspKey_, &mappings, false);
	std::vector<MultiInputMapping> replacing;
	if (index >= 0 && index < (int)mappings.size()) {
		replacing.push_back(mappings[index]);
	}
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, replacing, [this, index](KeyMap::MultiInputMapping mapping) {
		using namespace UI;
		if (mapping.empty())
			return;
		bool success = KeyMap::ReplaceSingleKeyMapping(pspKey_, index, mapping);
		if (!success) {
			replaceAllButton_->SetFocus(FocusFlags::CAUSE_FORCED); // Last got removed as a duplicate
		} else if (index < (int)rows_.size()) {
			rows_[index]->SetFocus(FocusFlags::CAUSE_FORCED);
		} else {
			SetFocus(FocusFlags::CAUSE_FORCED);
		}
		KeyMap::UpdateNativeMenuKeys();
		g_IsMappingMouseInput = false;
	}, I18NCat::KEYMAPPING));
}

void SingleControlMapper::OnReplaceAll(UI::EventParams &params) {
	std::vector<MultiInputMapping> replacing;
	KeyMap::InputMappingsFromPspButton(pspKey_, &replacing, false);
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, replacing, [this](KeyMap::MultiInputMapping mapping) {
		if (mapping.empty())
			return;
		KeyMap::SetInputMapping(pspKey_, mapping, true);
		replaceAllButton_->SetFocus(UI::FocusFlags::CAUSE_FORCED);
		KeyMap::UpdateNativeMenuKeys();
		g_IsMappingMouseInput = false;
	}, I18NCat::KEYMAPPING));
}

void SingleControlMapper::OnAdd(UI::EventParams &params) {
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, {}, [this](KeyMap::MultiInputMapping mapping) {
		if (mapping.empty())
			return;
		KeyMap::SetInputMapping(pspKey_, mapping, false);
		addButton_->SetFocus(UI::FocusFlags::CAUSE_FORCED);
		KeyMap::UpdateNativeMenuKeys();
		g_IsMappingMouseInput = false;
	}, I18NCat::KEYMAPPING));
}

void SingleControlMapper::OnAddMouse(UI::EventParams &params) {
	g_IsMappingMouseInput = true;
	scrm_->push(new KeyMappingNewMouseKeyDialog(pspKey_, true, [this](KeyMap::MultiInputMapping mapping) {
		if (mapping.empty())
			return;
		KeyMap::SetInputMapping(pspKey_, mapping, false);
		addButton_->SetFocus(UI::FocusFlags::CAUSE_FORCED);
		KeyMap::UpdateNativeMenuKeys();
		g_IsMappingMouseInput = false;
	}, I18NCat::KEYMAPPING));
}

void SingleControlMapper::OnDelete(UI::EventParams &params) {
	int index = atoi(params.v->Tag().c_str());
	KeyMap::DeleteNthMapping(pspKey_, index);
	if (index + 1 < (int)rows_.size())
		rows_[index]->SetFocus(UI::FocusFlags::CAUSE_FORCED);
	else
		SetFocus(UI::FocusFlags::CAUSE_FORCED);
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

void ControlMappingScreen::CreateSettingsViews(UI::ViewGroup *parent, bool inPopup) {
	using namespace UI;
	auto km = GetI18NCategory(I18NCat::KEYMAPPING);

	// Use the whole width of the narrow settings column.
	LinearLayoutList *groups;
	if (inPopup) {
		groups = parent->Add(new PopupChoiceGroupList());
	} else {
		groups = parent->Add(new ChoiceGroupList());
	}

	ChoiceGroup *resetChoices = groups->Add(new ChoiceGroup());
	resetChoices->Add(new Choice(km->T("Clear All")))->OnClick.Add([](UI::EventParams &) {
		KeyMap::ClearAllMappings();
	});
	resetChoices->Add(new Choice(km->T("Default All")))->OnClick.Add([](UI::EventParams &) {
		KeyMap::RestoreDefault();
	});

	ChoiceGroup *moreChoices = groups->Add(new ChoiceGroup());

	std::string sysName = System_GetProperty(SYSPROP_NAME);
	// If there's a builtin controller, restore to default should suffice. No need to conf the controller on top.
	if (!KeyMap::HasBuiltinController(sysName) && KeyMap::GetSeenPads().size()) {
		moreChoices->Add(new Choice(km->T("Autoconfigure")))->OnClick.Handle(this, &ControlMappingScreen::OnAutoConfigure);
	}
	moreChoices->Add(new Choice(km->T("Show PSP")))->OnClick.Add([this](UI::EventParams &params) {
		screenManager()->push(new VisualMappingScreen(gamePath_));
	});
	moreChoices->Add(new CheckBox(&g_Config.bAllowMappingCombos, km->T("Allow combo mappings")));
	moreChoices->Add(new CheckBox(&g_Config.bStrictComboOrder, km->T("Strict combo input order")));
}

std::string_view ControlMappingScreen::GetTitle() const {
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	return co->T("Control mapping");
}

void ControlMappingScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	LinearLayout *rootLayout = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	rootLayout->SetPadding(Margins(0, 12, 12, 12));
	mappers_.clear();

	size_t numMappableKeys = 0;
	const KeyMap::KeyMap_IntStrPair *mappableKeys = KeyMap::GetMappableKeys(&numMappableKeys);

	auto km = GetI18NCategory(I18NCat::KEYMAPPING);

	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	int curCat = -1;
	CollapsibleSection *curSection = nullptr;
	for (size_t i = 0; i < numMappableKeys; i++) {
		if (curCat < (int)ARRAY_SIZE(cats) && mappableKeys[i].key == cats[curCat + 1].firstKey) {
			if (curCat >= 0) {
				curSection->SetOpenPtr(&categoryToggles_[curCat]);
			}
			curCat++;
			curSection = rootLayout->Add(new CollapsibleSection(km->T(cats[curCat].catName)));
			curSection->SetSpacing(6.0f);
		}
		SingleControlMapper *mapper = curSection->Add(
			new SingleControlMapper(mappableKeys[i].key, mappableKeys[i].name, portrait, screenManager()));
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

	UIBaseDialogScreen::update();
	SetVRAppMode(VRAppMode::VR_MENU_MODE);
}

void ControlMappingScreen::OnAutoConfigure(UI::EventParams &params) {
	std::vector<std::string> items;
	const auto seenPads = KeyMap::GetSeenPads();
	for (auto s = seenPads.begin(), end = seenPads.end(); s != end; ++s) {
		items.push_back(*s);
	}
	auto km = GetI18NCategory(I18NCat::KEYMAPPING);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	// Uses a callback, since in portrait the settings are in a popup, which would get dialogFinished instead of us.
	UI::ListPopupScreen *autoConfList = new UI::ListPopupScreen(km->T("Autoconfigure for device"), items, -1, [items](int index) {
		KeyMap::AutoConfForPad(items[index]);
	});
	autoConfList->SetNotification(NoticeLevel::WARN, di->T("This will overwrite the existing configuration"));
	if (params.v)
		autoConfList->SetPopupOrigin(params.v);
	screenManager()->push(autoConfList);
}

void KeyMappingNewKeyDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto km = GetI18NCategory(I18NCat::KEYMAPPING);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	std::string pspButtonName = KeyMap::GetPspButtonName(this->pspBtn_);

	parent->Add(new TextView(std::string(km->T("Map a new key for")) + " " + std::string(mc->T(pspButtonName)), new LinearLayoutParams(Margins(10, 0))));
	if (!replacing_.empty()) {
		std::string replacingText = std::string(km->T("Replaces")) + ": ";
		for (size_t i = 0; i < replacing_.size(); i++) {
			if (i != 0) {
				replacingText += ", ";
			}
			replacingText += replacing_[i].ToVisualString();
		}
		parent->Add(new TextView(replacingText, ALIGN_LEFT, true, new LinearLayoutParams(Margins(10, 0))));
	}
	parent->Add(new TextView(mapping_.ToVisualString(), new LinearLayoutParams(Margins(10, 0))));

	comboMappingsNotEnabled_ = parent->Add(new NoticeView(NoticeLevel::WARN, km->T("Combo mappings are not enabled"), "", new LinearLayoutParams(Margins(10, 0))));
	comboMappingsNotEnabled_->SetVisibility(UI::V_GONE);

	SetVRAppMode(VRAppMode::VR_CONTROLLER_MAPPING_MODE);
}

bool KeyMappingNewKeyDialog::key(const KeyInput &key) {
	if (ignoreInput_)
		return true;
	if (time_now_d() < delayUntil_)
		return true;

	if (key.flags & KeyInputFlags::DOWN) {
		if (key.keyCode == NKCODE_EXT_MOUSEBUTTON_1) {
			// Don't map
			return true;
		}

		if (pspBtn_ == VIRTKEY_SPEED_ANALOG) {
			// Only map analog values to this mapping. Escape cancels.
			if (UI::IsEscapeKey(key)) {
				TriggerFinish(DR_CANCEL);
			}
			return true;
		}

		InputMapping newMapping(key.deviceId, key.keyCode);

		if (!(key.flags & KeyInputFlags::IS_REPEAT)) {
			if (!g_Config.bAllowMappingCombos && !mapping_.mappings.empty()) {
				comboMappingsNotEnabled_->SetVisibility(UI::V_VISIBLE);
			} else if (!mapping_.mappings.contains(newMapping)) {
				mapping_.mappings.push_back(newMapping);
				RecreateViews();
			}
		}
	}
	if (key.flags & KeyInputFlags::UP) {
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
	if (key.flags & KeyInputFlags::DOWN) {
		if (key.keyCode == NKCODE_ESCAPE) {
			TriggerFinish(DR_OK);
			g_IsMappingMouseInput = false;
			return false;
		}

		mapped_ = true;

		TriggerFinish(DR_YES);
		g_IsMappingMouseInput = false;
		if (callback_) {
			MultiInputMapping kdf(InputMapping(key.deviceId, key.keyCode));
			callback_(kdf);
		}
	}
	return true;
}

// Only used during the bind process. In other places, it's configurable for some types of axis, like trigger.
const float AXIS_BIND_THRESHOLD = 0.75f;
const float AXIS_BIND_RELEASE_THRESHOLD = 0.35f;  // Used during mapping only to detect a "key-up" reliably (hysteresis).

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

AnalogCalibrationScreen::AnalogCalibrationScreen(const Path &gamePath) : UITwoPaneBaseDialogScreen(gamePath, TwoPaneFlags::SettingsCanScroll) {
	g_controlMapper.AddListener(this);
}

AnalogCalibrationScreen::~AnalogCalibrationScreen() {
	g_controlMapper.RemoveListener(this);
}

void AnalogCalibrationScreen::SetPSPAnalog(int rotation, int stick, float x, float y) {
	analogX_[stick] = x;
	analogY_[stick] = y;
}

void AnalogCalibrationScreen::SetRawAnalog(int stick, float x, float y) {
	rawX_[stick] = x;
	rawY_[stick] = y;
}

void AnalogCalibrationScreen::update() {
	g_controlMapper.UpdateConfig(g_Config.GetDisplayLayoutConfig(GetDeviceOrientation()));
	// We ignore the secondary stick for now and just use the two views
	// for raw and psp input.
	if (stickView_[0]) {
		stickView_[0]->SetXY(analogX_[0], analogY_[0]);
	}
	if (stickView_[1]) {
		stickView_[1]->SetXY(rawX_[0], rawY_[0]);
	}
	UITwoPaneBaseDialogScreen::update();
}

void AnalogCalibrationScreen::axis(const AxisInput &axis) {
	// We DON'T call UIScreen::Axis here! Otherwise it'll try to move the UI focus around.
	// UIScreen::axis(axis);
}

std::string_view AnalogCalibrationScreen::GetTitle() const {
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	return co->T("Calibrate analog stick");
}

void AnalogCalibrationScreen::CreateSettingsViews(UI::ViewGroup *scrollContents, bool inPopup) {
	using namespace UI;
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	ChoiceGroupList *groups = scrollContents->Add(new ChoiceGroupList());
	groups->SetSpacing(12.0f);

	ChoiceGroup *analogSettings = groups->Add(new ChoiceGroup(co->T("Analog Settings")));

	// TODO: Would be nicer if these didn't pop up...
	analogSettings->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogDeadzone, 0.0f, 0.5f, 0.15f, co->T("Deadzone radius"), 0.01f, screenManager(), "/ 1.0"));
	analogSettings->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogInverseDeadzone, 0.0f, 1.0f, 0.0f, co->T("Low end radius"), 0.01f, screenManager(), "/ 1.0"));
	analogSettings->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogSensitivity, 0.0f, 2.0f, 1.1f, co->T("Sensitivity (scale)", "Sensitivity"), 0.01f, screenManager(), "x"));
	// Legacy circular toggle. Disabled when deadzone shape is set to Circle, since that already provides circular behavior.
	CheckBox *circularCheck = analogSettings->Add(new CheckBox(&g_Config.bAnalogIsCircular, co->T("Circular stick input")));
	circularCheck->SetEnabledFunc([] {
		return g_Config.iAnalogDeadzoneShape != 0;  // Disabled when shape is Circle (redundant)
	});
	analogSettings->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogAutoRotSpeed, 0.1f, 20.0f, 8.0f, co->T("Auto-rotation speed"), 1.0f, screenManager()));

	// Advanced deadzone settings (Steam Input-style).
	ChoiceGroup *deadzoneSettings = groups->Add(new ChoiceGroup(co->T("Advanced Deadzone Settings")));

	static const char *deadzoneShapes[] = { "Circle", "Square", "Cross" };
	deadzoneSettings->Add(new PopupMultiChoice(&g_Config.iAnalogDeadzoneShape, co->T("Deadzone shape"), deadzoneShapes, 0, ARRAY_SIZE(deadzoneShapes), I18NCat::CONTROLS, screenManager()));

	PopupSliderChoiceFloat *axialDZ = deadzoneSettings->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogAxialDeadzone, 0.0f, 0.5f, 0.0f, co->T("Axial anti-deadzone"), 0.01f, screenManager(), "/ 1.0"));
	axialDZ->SetEnabledFunc([] {
		return g_Config.iAnalogDeadzoneShape == 2;  // Only enabled for Cross shape
	});

	static const char *responseCurves[] = { "Linear", "Aggressive", "Relaxed", "Wide" };
	deadzoneSettings->Add(new PopupMultiChoice(&g_Config.iAnalogResponseCurve, co->T("Response curve"), responseCurves, 0, ARRAY_SIZE(responseCurves), I18NCat::CONTROLS, screenManager()));

	groups->Add(new ChoiceGroup())->Add(new Choice(co->T("Reset to defaults")))->OnClick.Handle(this, &AnalogCalibrationScreen::OnResetToDefaults);
}

void AnalogCalibrationScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	// Two joystick views, one for calibrated output, one for raw input.
	LinearLayout *theTwo = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f));

	stickView_[0] = theTwo->Add(new JoystickHistoryView(StickHistoryViewType::OUTPUT, co->T("Calibrated"), new LinearLayoutParams(1.0f)));
	stickView_[1] = theTwo->Add(new JoystickHistoryView(StickHistoryViewType::INPUT, co->T("Raw input"), new LinearLayoutParams(1.0f)));

	parent->Add(theTwo);
}

void AnalogCalibrationScreen::OnResetToDefaults(UI::EventParams &e) {
	g_Config.fAnalogDeadzone = 0.15f;
	g_Config.fAnalogInverseDeadzone = 0.0f;
	g_Config.fAnalogSensitivity = 1.1f;
	g_Config.bAnalogIsCircular = false;
	g_Config.fAnalogAutoRotSpeed = 8.0f;
	// Advanced settings
	g_Config.iAnalogDeadzoneShape = 1;  // Square (matches legacy default)
	g_Config.fAnalogAxialDeadzone = 0.0f;
	g_Config.iAnalogResponseCurve = 0;
}
