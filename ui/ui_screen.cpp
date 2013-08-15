#include "input/input_state.h"
#include "input/keycodes.h"
#include "ui/ui_screen.h"
#include "ui/ui_context.h"
#include "ui/screen.h"

UIScreen::UIScreen()
	: Screen(), root_(0), recreateViews_(true), hatDown_(0) {
}

void UIScreen::DoRecreateViews() {
	if (recreateViews_) {
		delete root_;
		root_ = 0;
		CreateViews();
		recreateViews_ = false;
	}
}

void UIScreen::update(InputState &input) {
	DoRecreateViews();

	if (root_) {
		UpdateViewHierarchy(input, root_);
	}
}

void UIScreen::render() {
	DoRecreateViews();

	if (root_) {
		UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_);

		screenManager()->getUIContext()->Begin();
		DrawBackground(*screenManager()->getUIContext());
		root_->Draw(*screenManager()->getUIContext());
		screenManager()->getUIContext()->End();
		screenManager()->getUIContext()->Flush();
	}
}

void UIScreen::touch(const TouchInput &touch) {
	if (root_) {
		UI::TouchEvent(touch, root_);
	}
}

void UIScreen::key(const KeyInput &key) {
	if (root_) {
		UI::KeyEvent(key, root_);
	}
}

void DialogScreen::key(const KeyInput &key) {
	if ((key.flags & KEY_DOWN) && key.keyCode == NKCODE_ESCAPE || key.keyCode == NKCODE_BACK || key.keyCode == NKCODE_BUTTON_B) {
		screenManager()->finishDialog(this, DR_CANCEL);
	} else {
		UIScreen::key(key);
	}
}

void UIScreen::axis(const AxisInput &axis) {
	// Simple translation of hat to keys for Shield and other modern pads.
	// TODO: Use some variant of keymap?
	int flags = 0;
	if (axis.axisId == JOYSTICK_AXIS_HAT_X) {
		if (axis.value < -0.7f)
			flags |= PAD_BUTTON_LEFT;
		if (axis.value > 0.7f)
			flags |= PAD_BUTTON_RIGHT;
	}
	if (axis.axisId == JOYSTICK_AXIS_HAT_Y) {
		if (axis.value < -0.7f)
			flags |= PAD_BUTTON_UP;
		if (axis.value > 0.7f)
			flags |= PAD_BUTTON_DOWN;
	}

	// Yeah yeah, this should be table driven..
	int pressed = flags & ~hatDown_;
	int released = ~flags & hatDown_;
	if (pressed & PAD_BUTTON_LEFT) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_LEFT, KEY_DOWN));
	if (pressed & PAD_BUTTON_RIGHT) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_RIGHT, KEY_DOWN));
	if (pressed & PAD_BUTTON_UP) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_UP, KEY_DOWN));
	if (pressed & PAD_BUTTON_DOWN) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_DOWN, KEY_DOWN));
	if (released & PAD_BUTTON_LEFT) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_LEFT, KEY_UP));
	if (released & PAD_BUTTON_RIGHT) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_RIGHT, KEY_UP));
	if (released & PAD_BUTTON_UP) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_UP, KEY_UP));
	if (released & PAD_BUTTON_DOWN) key(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_DPAD_DOWN, KEY_UP));
	hatDown_ = flags;
	if (root_) {
		UI::AxisEvent(axis, root_);
	}
}


UI::EventReturn UIScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}


PopupScreen::PopupScreen(const std::string &title)
	: title_(title) {}

void PopupScreen::CreateViews() {
	using namespace UI;

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	LinearLayout *box = new LinearLayout(ORIENT_VERTICAL, 
		new AnchorLayoutParams(550, FillVertical() ? dp_yres - 30 : WRAP_CONTENT, dp_xres / 2, dp_yres / 2, NONE, NONE, true));

	root_->Add(box);
	box->SetBG(UI::Drawable(0xFF303030));
	box->SetHasDropShadow(true);

	View *title = new PopupHeader(title_);
	box->Add(title);

	CreatePopupContents(box);

	if (ShowButtons()) {
		// And the two buttons at the bottom.
		ViewGroup *buttonRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(200, WRAP_CONTENT));
		buttonRow->Add(new Button("OK", new LinearLayoutParams(1.0f)))->OnClick.Handle(this, &PopupScreen::OnOK);
		buttonRow->Add(new Button("Cancel", new LinearLayoutParams(1.0f)))->OnClick.Handle(this, &PopupScreen::OnCancel);
		box->Add(buttonRow);
	}
}

void PopupScreen::key(const KeyInput &key) {
	if ((key.flags & KEY_DOWN) && UI::IsEscapeKeyCode(key.keyCode))
		screenManager()->finishDialog(this, DR_CANCEL);
	UIScreen::key(key);
}

UI::EventReturn PopupScreen::OnOK(UI::EventParams &e) {
	OnCompleted();
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn PopupScreen::OnCancel(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

void ListPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	listView_ = parent->Add(new ListView(&adaptor_, new LinearLayoutParams(1.0)));
	listView_->OnChoice.Handle(this, &ListPopupScreen::OnListChoice);
}

UI::EventReturn ListPopupScreen::OnListChoice(UI::EventParams &e) {
	adaptor_.SetSelected(e.a);
	if (callback_)
		callback_(adaptor_.GetSelected());	
	screenManager()->finishDialog(this, DR_OK);
	OnCompleted();
	OnChoice.Dispatch(e);
	return UI::EVENT_DONE;
}

void SliderPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	sliderValue_ = *value_;
	slider_ = parent->Add(new Slider(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 5))));
}

void SliderFloatPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	sliderValue_ = *value_;
	slider_ = parent->Add(new SliderFloat(&sliderValue_, minValue_, maxValue_));
}

void SliderPopupScreen::OnCompleted() {
	*value_ = sliderValue_;
}

void SliderFloatPopupScreen::OnCompleted() {
	*value_ = sliderValue_;
}
