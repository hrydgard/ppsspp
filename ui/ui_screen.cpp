#include "input/input_state.h"
#include "input/keycodes.h"
#include "ui/ui_screen.h"
#include "ui/ui_context.h"
#include "ui/screen.h"
#include "i18n/i18n.h"
#include "gfx_es2/draw_buffer.h"

UIScreen::UIScreen()
	: Screen(), root_(0), recreateViews_(true), hatDown_(0) {
}

UIScreen::~UIScreen() {
	delete root_;
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

void UIDialogScreen::key(const KeyInput &key) {
	if ((key.flags & KEY_DOWN) && UI::IsEscapeKeyCode(key.keyCode)) {
		if (finished_) {
			ELOG("Screen already finished");
		} else {
			finished_ = true;
			screenManager()->finishDialog(this, DR_BACK);
		}
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
	screenManager()->finishDialog(this, DR_BACK);
	return UI::EVENT_DONE;
}

PopupScreen::PopupScreen(std::string title, std::string button1, std::string button2)
	: box_(0), title_(title) {
	I18NCategory *d = GetI18NCategory("Dialog");
	if (!button1.empty())
		button1_ = d->T(button1.c_str());
	if (!button2.empty())
		button2_ = d->T(button2.c_str());
}

void PopupScreen::touch(const TouchInput &touch) {
	if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0) {
		UIDialogScreen::touch(touch);
		return;
	}

	if (!box_->GetBounds().Contains(touch.x, touch.y))
		screenManager()->finishDialog(this, DR_BACK);

	UIDialogScreen::touch(touch);
}

void PopupScreen::CreateViews() {
	using namespace UI;

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	box_ = new LinearLayout(ORIENT_VERTICAL,
		new AnchorLayoutParams(550, FillVertical() ? dp_yres - 30 : WRAP_CONTENT, dp_xres / 2, dp_yres / 2, NONE, NONE, true));

	root_->Add(box_);
	box_->SetBG(UI::Drawable(0xFF303030));
	box_->SetHasDropShadow(true);

	View *title = new PopupHeader(title_);
	box_->Add(title);

	CreatePopupContents(box_);

	if (ShowButtons() && !button1_.empty()) {
		// And the two buttons at the bottom.
		LinearLayout *buttonRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(200, WRAP_CONTENT));
		buttonRow->SetSpacing(0);
		Margins buttonMargins(5, 5);

		// Adjust button order to the platform default.
#if defined(_WIN32)
		buttonRow->Add(new Button(button1_, new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle(this, &PopupScreen::OnOK);
		if (!button2_.empty())
			buttonRow->Add(new Button(button2_, new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle(this, &PopupScreen::OnCancel);
#else
		if (!button2_.empty())
			buttonRow->Add(new Button(button2_, new LinearLayoutParams(1.0f)))->OnClick.Handle(this, &PopupScreen::OnCancel);
		buttonRow->Add(new Button(button1_, new LinearLayoutParams(1.0f)))->OnClick.Handle(this, &PopupScreen::OnOK);
#endif

		box_->Add(buttonRow);
	}
}

void MessagePopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	parent->Add(new UI::TextView(message_));
}

UI::EventReturn PopupScreen::OnOK(UI::EventParams &e) {
	OnCompleted(DR_OK);
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn PopupScreen::OnCancel(UI::EventParams &e) {
	OnCompleted(DR_CANCEL);
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
	OnCompleted(DR_OK);
	OnChoice.Dispatch(e);
	return UI::EVENT_DONE;
}

namespace UI {

UI::EventReturn PopupMultiChoice::HandleClick(UI::EventParams &e) {
	std::vector<std::string> choices;
	for (int i = 0; i < numChoices_; i++) {
		choices.push_back(category_ ? category_->T(choices_[i]) : choices_[i]);
	}

	Screen *popupScreen = new ListPopupScreen(text_, choices, *value_ - minVal_,
		std::bind(&PopupMultiChoice::ChoiceCallback, this, placeholder::_1));
	screenManager_->push(popupScreen);
	return UI::EVENT_DONE;
}

void PopupMultiChoice::UpdateText() {
	// Clamp the value to be safe.
	if (*value_ < minVal_ || *value_ > minVal_ + numChoices_ - 1) {
		valueText_ = "(invalid choice)";  // Shouldn't happen. Should be no need to translate this.
	} else {
		valueText_ = category_ ? category_->T(choices_[*value_ - minVal_]) : choices_[*value_ - minVal_];
	}
}

void PopupMultiChoice::ChoiceCallback(int num) {
	if (num != -1) {
		*value_ = num + minVal_;
		UpdateText();

		UI::EventParams e;
		e.v = this;
		e.a = num;
		OnChoice.Trigger(e);
	}
}

void PopupMultiChoice::Draw(UIContext &dc) {
	Choice::Draw(dc);
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(valueText_.c_str(), bounds_.x2() - paddingX, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

PopupSliderChoice::PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, ScreenManager *screenManager, LayoutParams *layoutParams)
	: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), screenManager_(screenManager) {
	OnClick.Handle(this, &PopupSliderChoice::HandleClick);
}

PopupSliderChoiceFloat::PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, ScreenManager *screenManager, LayoutParams *layoutParams)
	: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), screenManager_(screenManager) {
	OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
}

EventReturn PopupSliderChoice::HandleClick(EventParams &e) {
	SliderPopupScreen *popupScreen = new SliderPopupScreen(value_, minValue_, maxValue_, text_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoice::HandleChange);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

EventReturn PopupSliderChoice::HandleChange(EventParams &e) {
	e.v = this;
	OnChange.Trigger(e);
	return EVENT_DONE;
}

void PopupSliderChoice::Draw(UIContext &dc) {
	Choice::Draw(dc);
	char temp[32];
	sprintf(temp, "%i", *value_);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(temp, bounds_.x2() - 12, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

EventReturn PopupSliderChoiceFloat::HandleClick(EventParams &e) {
	SliderFloatPopupScreen *popupScreen = new SliderFloatPopupScreen(value_, minValue_, maxValue_, text_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoiceFloat::HandleChange);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

EventReturn PopupSliderChoiceFloat::HandleChange(EventParams &e) {
	e.v = this;
	OnChange.Trigger(e);
	return EVENT_DONE;
}

void PopupSliderChoiceFloat::Draw(UIContext &dc) {
	Choice::Draw(dc);
	char temp[32];
	sprintf(temp, "%2.2f", *value_);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(temp, bounds_.x2() - 12, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

void SliderPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	sliderValue_ = *value_;
	slider_ = parent->Add(new Slider(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 5))));
	UI::SetFocusedView(slider_);
}

void SliderFloatPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	sliderValue_ = *value_;
	slider_ = parent->Add(new SliderFloat(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 5))));
	UI::SetFocusedView(slider_);
}

void SliderPopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = sliderValue_;
		EventParams e;
		e.v = 0;
		e.a = *value_;
		OnChange.Trigger(e);
	}
}

void SliderFloatPopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = sliderValue_;
		EventParams e;
		e.v = 0;
		e.a = (int)*value_;
		e.f = *value_;
		OnChange.Trigger(e);
	}
}

}  // namespace UI
