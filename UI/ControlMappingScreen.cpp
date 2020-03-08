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

#include <algorithm>
#include <deque>
#include <mutex>

#include "base/colorutil.h"
#include "base/logging.h"
#include "base/display.h"
#include "gfx/texture_atlas.h"
#include "i18n/i18n.h"
#include "input/keycodes.h"
#include "input/input_state.h"
#include "ui/root.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

#include "Core/Host.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/System.h"
#include "Common/KeyMap.h"
#include "Core/Config.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"

class ControlMapper : public UI::LinearLayout {
public:
	ControlMapper(ControlMappingScreen *ctrlScreen, int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams = 0);

	void Update() override;
	int GetPspKey() const { return pspKey_; }
private:
	void Refresh();

	UI::EventReturn OnAdd(UI::EventParams &params);
	UI::EventReturn OnAddMouse(UI::EventParams &params);
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

	ControlMappingScreen *ctrlScreen_;
	Action action_;
	int actionIndex_;
	int pspKey_;
	std::string keyName_;
	ScreenManager *scrm_;
	bool refresh_;
};

ControlMapper::ControlMapper(ControlMappingScreen *ctrlScreen, int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams)
	: UI::LinearLayout(UI::ORIENT_VERTICAL, layoutParams), ctrlScreen_(ctrlScreen), action_(NONE), pspKey_(pspKey), keyName_(keyName), scrm_(scrm), refresh_(false) {
	Refresh();
}

void ControlMapper::Update() {
	if (refresh_) {
		refresh_ = false;
		Refresh();
		host->UpdateUI();
	}
}

void ControlMapper::Refresh() {
	bool hasFocus = UI::GetFocusedView() == this;
	Clear();
	auto mc = GetI18NCategory("MappableControls");

	std::map<std::string, ImageID> keyImages;
	keyImages["Circle"] = ImageID("I_CIRCLE");
	keyImages["Cross"] = ImageID("I_CROSS");
	keyImages["Square"] = ImageID("I_SQUARE");
	keyImages["Triangle"] = ImageID("I_TRIANGLE");
	keyImages["Start"] = ImageID("I_START");
	keyImages["Select"] = ImageID("I_SELECT");
	keyImages["L"] = ImageID("I_L");
	keyImages["R"] = ImageID("I_R");

	using namespace UI;

	float itemH = 45;

	LinearLayout *root = Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(550, WRAP_CONTENT)));
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

	Choice *p = root->Add(new Choice(" + ", new LayoutParams(WRAP_CONTENT, itemH)));
	p->OnClick.Handle(this, &ControlMapper::OnAdd);
	if (g_Config.bMouseControl) {
		Choice *p = root->Add(new Choice("M", new LayoutParams(WRAP_CONTENT, itemH)));
		p->OnClick.Handle(this, &ControlMapper::OnAddMouse);
	}

	LinearLayout *rightColumn = root->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	rightColumn->SetSpacing(2.0f);
	std::vector<KeyDef> mappings;
	KeyMap::KeyFromPspButton(pspKey_, &mappings, false);

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

		Choice *d = row->Add(new Choice(" X ", new LayoutParams(WRAP_CONTENT, itemH)));
		d->SetTag(tagbuf);
		d->OnClick.Handle(this, &ControlMapper::OnDelete);
	}

	if (mappings.size() == 0) {
		// look like an empty line
		Choice *c = rightColumn->Add(new Choice("", new LinearLayoutParams(FILL_PARENT, itemH)));
		c->OnClick.Handle(this, &ControlMapper::OnAdd);
	}

	if (hasFocus)
		this->SetFocus();
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
		KeyMap::g_controllerMapGeneration++;
		break;
	default:
		;
	}
	g_Config.bMapMouse = false;
	refresh_ = true;
	ctrlScreen_->KeyMapped(pspKey_);
	// After this, we do not exist any more. So the refresh_ = true is probably irrelevant.
}

UI::EventReturn ControlMapper::OnReplace(UI::EventParams &params) {
	actionIndex_ = atoi(params.v->Tag().c_str());
	action_ = REPLACEONE;
	auto km = GetI18NCategory("KeyMapping");
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&ControlMapper::MappedCallback, this, std::placeholders::_1), km));
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMapper::OnReplaceAll(UI::EventParams &params) {
	action_ = REPLACEALL;
	auto km = GetI18NCategory("KeyMapping");
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&ControlMapper::MappedCallback, this, std::placeholders::_1), km));
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMapper::OnAdd(UI::EventParams &params) {
	action_ = ADD;
	auto km = GetI18NCategory("KeyMapping");
	scrm_->push(new KeyMappingNewKeyDialog(pspKey_, true, std::bind(&ControlMapper::MappedCallback, this, std::placeholders::_1), km));
	return UI::EVENT_DONE;
}
UI::EventReturn ControlMapper::OnAddMouse(UI::EventParams &params) {
	action_ = ADD;
	g_Config.bMapMouse = true;
	auto km = GetI18NCategory("KeyMapping");
	scrm_->push(new KeyMappingNewMouseKeyDialog(pspKey_, true, std::bind(&ControlMapper::MappedCallback, this, std::placeholders::_1), km));
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMapper::OnDelete(UI::EventParams &params) {
	int index = atoi(params.v->Tag().c_str());
	KeyMap::g_controllerMap[pspKey_].erase(KeyMap::g_controllerMap[pspKey_].begin() + index);
	KeyMap::g_controllerMapGeneration++;
	refresh_ = true;
	return UI::EVENT_DONE;
}

void ControlMappingScreen::CreateViews() {
	using namespace UI;
	mappers_.clear();

	auto km = GetI18NCategory("KeyMapping");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT, Margins(10, 0, 0, 10)));
	leftColumn->Add(new Choice(km->T("Clear All")))->OnClick.Handle(this, &ControlMappingScreen::OnClearMapping);
	leftColumn->Add(new Choice(km->T("Default All")))->OnClick.Handle(this, &ControlMappingScreen::OnDefaultMapping);

	std::string sysName = System_GetProperty(SYSPROP_NAME);
	// If there's a builtin controller, restore to default should suffice. No need to conf the controller on top.
	if (!KeyMap::HasBuiltinController(sysName) && KeyMap::GetSeenPads().size()) {
		leftColumn->Add(new Choice(km->T("Autoconfigure")))->OnClick.Handle(this, &ControlMappingScreen::OnAutoConfigure);
	}
	leftColumn->Add(new Choice(km->T("Test Analogs")))->OnClick.Handle(this, &ControlMappingScreen::OnTestAnalogs);
	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	AddStandardBack(leftColumn);

	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll_->SetTag("ControlMapping");
	rightScroll_->SetScrollToTop(false);
	LinearLayout *rightColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll_->Add(rightColumn);

	root_->Add(leftColumn);
	root_->Add(rightScroll_);

	std::vector<KeyMap::KeyMap_IntStrPair> mappableKeys = KeyMap::GetMappableKeys();
	for (size_t i = 0; i < mappableKeys.size(); i++) {
		ControlMapper *mapper = rightColumn->Add(new ControlMapper(this, mappableKeys[i].key, mappableKeys[i].name, screenManager(), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		mappers_.push_back(mapper);
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

UI::EventReturn ControlMappingScreen::OnAutoConfigure(UI::EventParams &params) {
	std::vector<std::string> items;
	const auto seenPads = KeyMap::GetSeenPads();
	for (auto s = seenPads.begin(), end = seenPads.end(); s != end; ++s) {
		items.push_back(*s);
	}
	auto km = GetI18NCategory("KeyMapping");
	ListPopupScreen *autoConfList = new ListPopupScreen(km->T("Autoconfigure for device"), items, -1);
	if (params.v)
		autoConfList->SetPopupOrigin(params.v);
	screenManager()->push(autoConfList);
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMappingScreen::OnTestAnalogs(UI::EventParams &params) {
	screenManager()->push(new AnalogTestScreen());
	return UI::EVENT_DONE;
}

void ControlMappingScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_OK && dialog->tag() == "listpopup") {
		ListPopupScreen *popup = (ListPopupScreen *)dialog;
		KeyMap::AutoConfForPad(popup->GetChoiceString());
		RecreateViews();
	}
}

void ControlMappingScreen::KeyMapped(int pspkey) {  // Notification to let us refocus the same one after recreating views.
	for (size_t i = 0; i < mappers_.size(); i++) {
		if (mappers_[i]->GetPspKey() == pspkey)
			SetFocusedView(mappers_[i]);
	}
}

void KeyMappingNewKeyDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto km = GetI18NCategory("KeyMapping");
	auto mc = GetI18NCategory("MappableControls");

	std::string pspButtonName = KeyMap::GetPspButtonName(this->pspBtn_);

	parent->Add(new TextView(std::string(km->T("Map a new key for")) + " " + mc->T(pspButtonName), new LinearLayoutParams(Margins(10,0))));
}

bool KeyMappingNewKeyDialog::key(const KeyInput &key) {
	if (mapped_)
		return false;
	if (key.flags & KEY_DOWN) {
		if (key.keyCode == NKCODE_EXT_MOUSEBUTTON_1) {
			return true;
		}

		mapped_ = true;
		KeyDef kdf(key.deviceId, key.keyCode);
		TriggerFinish(DR_OK);
		if (callback_)
			callback_(kdf);
	}
	return true;
}

void KeyMappingNewMouseKeyDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto km = GetI18NCategory("KeyMapping");

	parent->Add(new TextView(std::string(km->T("You can press ESC to cancel.")), new LinearLayoutParams(Margins(10, 0))));
}

bool KeyMappingNewMouseKeyDialog::key(const KeyInput &key) {
	if (mapped_)
		return false;
	if (key.flags & KEY_DOWN) {
		if (key.keyCode == NKCODE_ESCAPE) {
			TriggerFinish(DR_OK);
			g_Config.bMapMouse = false;
			return false;
		}

		mapped_ = true;
		KeyDef kdf(key.deviceId, key.keyCode);
		TriggerFinish(DR_OK);
		g_Config.bMapMouse = false;
		if (callback_)
			callback_(kdf);
	}
	return true;
}

static bool IgnoreAxisForMapping(int axis) {
	switch (axis) {
		// Ignore the accelerometer for mapping for now.
	case JOYSTICK_AXIS_ACCELEROMETER_X:
	case JOYSTICK_AXIS_ACCELEROMETER_Y:
	case JOYSTICK_AXIS_ACCELEROMETER_Z:
		return true;

		// Also ignore some weird axis events we get on Ouya.
	case JOYSTICK_AXIS_OUYA_UNKNOWN1:
	case JOYSTICK_AXIS_OUYA_UNKNOWN2:
	case JOYSTICK_AXIS_OUYA_UNKNOWN3:
	case JOYSTICK_AXIS_OUYA_UNKNOWN4:
		return true;

	default:
		return false;
	}
}


bool KeyMappingNewKeyDialog::axis(const AxisInput &axis) {
	if (mapped_)
		return false;
	if (IgnoreAxisForMapping(axis.axisId))
		return false;

	if (axis.value > AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, 1));
		TriggerFinish(DR_OK);
		if (callback_)
			callback_(kdf);
	}

	if (axis.value < -AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, -1));
		TriggerFinish(DR_OK);
		if (callback_)
			callback_(kdf);
	}
	return true;
}

bool KeyMappingNewMouseKeyDialog::axis(const AxisInput &axis) {
	if (mapped_)
		return false;
	if (IgnoreAxisForMapping(axis.axisId))
		return false;

	if (axis.value > AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, 1));
		TriggerFinish(DR_OK);
		if (callback_)
			callback_(kdf);
	}

	if (axis.value < -AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, -1));
		TriggerFinish(DR_OK);
		if (callback_)
			callback_(kdf);
	}
	return true;
}

class JoystickHistoryView : public UI::InertView {
public:
	JoystickHistoryView(int xAxis, int xDevice, int xDir, int yAxis, int yDevice, int yDir, UI::LayoutParams *layoutParams = nullptr)
		: UI::InertView(layoutParams),
			xAxis_(xAxis), xDir_(xDir),
			yAxis_(yAxis), yDir_(yDir) {}
	void Draw(UIContext &dc) override;
	void Update() override;
	void Axis(const AxisInput &input) override {
		// TODO: Check input.deviceId?
		if (input.axisId == xAxis_) {
			curX_ = input.value * xDir_;
		} else if (input.axisId == yAxis_) {
			curY_ = input.value * yDir_;
		}
	}

private:
	struct Location {
		float x;
		float y;
	};

	int xAxis_;
	int xDir_;
	int yAxis_;
	int yDir_;

	float curX_ = 0.0f;
	float curY_ = 0.0f;

	std::deque<Location> locations_;
	int maxCount_ = 500;
};

void JoystickHistoryView::Draw(UIContext &dc) {
	if (xAxis_ > -1 && yAxis_ > -1) {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(ImageID("I_CROSS"));
		if (!image) {
			return;
		}
		float minRadius = std::min(bounds_.w, bounds_.h) * 0.5f - image->w;
		dc.BeginNoTex();
		dc.Draw()->RectOutline(bounds_.centerX() - minRadius, bounds_.centerY() - minRadius, minRadius * 2.0f, minRadius * 2.0f, 0x80FFFFFF);
		dc.Flush();
		dc.Begin();
		int a = maxCount_ - (int)locations_.size();
		for (auto iter = locations_.begin(); iter != locations_.end(); ++iter) {
			float x = bounds_.centerX() + minRadius * iter->x;
			float y = bounds_.centerY() - minRadius * iter->y;
			float alpha = (float)a / maxCount_;
			if (alpha < 0.0f) {
				alpha = 0.0f;
			}
			dc.Draw()->DrawImage(ImageID("I_CROSS"), x, y, 0.8f, colorAlpha(0xFFFFFF, alpha), ALIGN_CENTER);
			a++;
		}
		dc.Flush();
	} else {
		dc.Begin();
		dc.DrawText("N/A", bounds_.centerX(), bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER);
		dc.Flush();
	}
}

void JoystickHistoryView::Update() {
	if (xAxis_ > -1 && yAxis_ > -1) {
		locations_.push_back(Location{ curX_, curY_ });
		if ((int)locations_.size() > maxCount_) {
			locations_.pop_front();
		}
	}
}

bool AnalogTestScreen::key(const KeyInput &key) {
	bool retval = true;
	if (UI::IsEscapeKey(key)) {
		TriggerFinish(DR_BACK);
		return true;
	}

	char buf[512];
	snprintf(buf, sizeof(buf), "Keycode: %d Device ID: %d [%s%s%s%s]", key.keyCode, key.deviceId,
		(key.flags & KEY_IS_REPEAT) ? "REP" : "",
		(key.flags & KEY_UP) ? "UP" : "",
		(key.flags & KEY_DOWN) ? "DOWN" : "",
		(key.flags & KEY_CHAR) ? "CHAR" : "");
	if (lastLastKeyEvent_ && lastKeyEvent_) {
		lastLastKeyEvent_->SetText(lastKeyEvent_->GetText());
		lastKeyEvent_->SetText(buf);
	}
	return retval;
}

bool AnalogTestScreen::axis(const AxisInput &axis) {
	UIScreen::axis(axis);
	// This is mainly to catch axis events that would otherwise get translated
	// into arrow keys, since seeing keyboard arrow key events appear when using
	// a controller would be confusing for the user.
	if (IgnoreAxisForMapping(axis.axisId))
		return false;

	if (axis.value > AXIS_BIND_THRESHOLD || axis.value < -AXIS_BIND_THRESHOLD) {
		char buf[512];
		snprintf(buf, sizeof(buf), "Axis: %d (value %1.3f) Device ID: %d",
			axis.axisId, axis.value, axis.deviceId);
		// Null-check just in case they weren't created yet.
		if (lastLastKeyEvent_ && lastKeyEvent_) {
			lastLastKeyEvent_->SetText(lastKeyEvent_->GetText());
			lastKeyEvent_->SetText(buf);
		}
		return true;
	}
	return false;
}

void AnalogTestScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");

	root_ = new LinearLayout(ORIENT_VERTICAL);

	LinearLayout *theTwo = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f));

	int axis1, device1, dir1;
	int axis2, device2, dir2;

	if (!KeyMap::AxisFromPspButton(VIRTKEY_AXIS_X_MAX, &device1, &axis1, &dir1)) axis1 = -1;
	if (!KeyMap::AxisFromPspButton(VIRTKEY_AXIS_Y_MAX, &device2, &axis2, &dir2)) axis2 = -1;

	theTwo->Add(new JoystickHistoryView(axis1, device1, dir1, axis2, device2, dir2, new LinearLayoutParams(1.0f)));

	if (!KeyMap::AxisFromPspButton(VIRTKEY_AXIS_RIGHT_X_MAX, &device1, &axis1, &dir1)) axis1 = -1;
	if (!KeyMap::AxisFromPspButton(VIRTKEY_AXIS_RIGHT_Y_MAX, &device2, &axis2, &dir2)) axis2 = -1;

	theTwo->Add(new JoystickHistoryView(axis1, device1, dir1, axis2, device2, dir2, new LinearLayoutParams(1.0f)));

	root_->Add(theTwo);

	lastLastKeyEvent_ = root_->Add(new TextView("-", new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	lastLastKeyEvent_->SetTextColor(0x80FFFFFF);   // semi-transparent
	lastKeyEvent_ = root_->Add(new TextView("-", new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	root_->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

bool TouchTestScreen::touch(const TouchInput &touch) {
	UIDialogScreenWithBackground::touch(touch);
	if (touch.flags & TOUCH_DOWN) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				WLOG("Double touch");
				touches_[i].x = touch.x;
				touches_[i].y = touch.y;
				found = true;
			}
		}
		if (!found) {
			for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
				if (touches_[i].id == -1) {
					touches_[i].id = touch.id;
					touches_[i].x = touch.x;
					touches_[i].y = touch.y;
					break;
				}
			}
		}
	}
	if (touch.flags & TOUCH_MOVE) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				touches_[i].x = touch.x;
				touches_[i].y = touch.y;
				found = true;
			}
		}
		if (!found) {
			WLOG("Move without touch down: %d", touch.id);
		}
	}
	if (touch.flags & TOUCH_UP) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				found = true;
				touches_[i].id = -1;
				break;
			}
		}
		if (!found) {
			WLOG("Touch release without touch down");
		}
	}
	return true;
}

void TouchTestScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto gr = GetI18NCategory("Graphics");
	root_ = new LinearLayout(ORIENT_VERTICAL);
	LinearLayout *theTwo = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f));
	root_->Add(theTwo);
	root_->Add(new CheckBox(&g_Config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &TouchTestScreen::OnImmersiveModeChange);
	root_->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

#if PPSSPP_PLATFORM(ANDROID)
extern int display_xres;
extern int display_yres;
#endif

void TouchTestScreen::render() {
	UIDialogScreenWithBackground::render();
	UIContext *ui_context = screenManager()->getUIContext();
	ui_context->BeginNoTex();
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touches_[i].id != -1) {
			ui_context->Draw()->Circle(touches_[i].x, touches_[i].y, 100.0, 3.0, 80, 0.0f, 0xFFFFFFFF, 1.0);
		}
	}
	ui_context->Flush();

	ui_context->Begin();

	char buffer[1024];
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touches_[i].id != -1) {
			ui_context->Draw()->Circle(touches_[i].x, touches_[i].y, 100.0, 3.0, 80, 0.0f, 0xFFFFFFFF, 1.0);
			snprintf(buffer, sizeof(buffer), "%0.1fx%0.1f", touches_[i].x, touches_[i].y);
			ui_context->DrawText(buffer, touches_[i].x, touches_[i].y + (touches_[i].y > dp_yres - 100.0f ? -135.0f : 95.0f), 0xFFFFFFFF, ALIGN_HCENTER | FLAG_DYNAMIC_ASCII);
		}
	}

	snprintf(buffer, sizeof(buffer),
#if PPSSPP_PLATFORM(ANDROID)
		"display_res: %dx%d\n"
#endif
		"dp_res: %dx%d\n"
		"pixel_res: %dx%d\n"
		"g_dpi: %f\n"
		"g_dpi_scale: %0.3fx%0.3f\n"
		"g_dpi_scale_real: %0.3fx%0.3f\n",
#if PPSSPP_PLATFORM(ANDROID)
		display_xres, display_yres,
#endif
		dp_xres, dp_yres,
		pixel_xres, pixel_yres,
		g_dpi,
		g_dpi_scale_x, g_dpi_scale_y,
		g_dpi_scale_real_x, g_dpi_scale_real_y);

	ui_context->DrawTextShadow(buffer, 20.0f, dp_yres / 2.0f, 0xFFFFFFFF, ALIGN_VCENTER | FLAG_DYNAMIC_ASCII);
	ui_context->Flush();
}

void RecreateActivity();

UI::EventReturn TouchTestScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_SendMessage("immersive", "");
	if (g_Config.iAndroidHwScale != 0) {
		RecreateActivity();
	}
	return UI::EVENT_DONE;
}
