#include <map>
#include "base/display.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "ui/ui_screen.h"
#include "ui/ui_context.h"
#include "ui/screen.h"
#include "i18n/i18n.h"
#include "gfx_es2/draw_buffer.h"

static const bool ClickDebug = false;

UIScreen::UIScreen()
	: Screen(), root_(0), recreateViews_(true), hatDown_(0) {
}

UIScreen::~UIScreen() {
	delete root_;
}

void UIScreen::DoRecreateViews() {
	if (recreateViews_) {
		UI::PersistMap persisted;
		bool persisting = root_ != nullptr;
		if (persisting) {
			root_->PersistData(UI::PERSIST_SAVE, "root", persisted);
		}

		delete root_;
		root_ = nullptr;
		CreateViews();
		if (root_ && root_->GetDefaultFocusView()) {
			root_->GetDefaultFocusView()->SetFocus();
		}
		recreateViews_ = false;

		if (persisting && root_ != nullptr) {
			root_->PersistData(UI::PERSIST_RESTORE, "root", persisted);

			// Update layout and refocus so things scroll into view.
			// This is for resizing down, when focused on something now offscreen.
			UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_);
			UI::View *focused = UI::GetFocusedView();
			if (focused) {
				root_->SubviewFocused(focused);
			}
		}
	}
}

void UIScreen::update(InputState &input) {
	DoRecreateViews();

	if (root_) {
		UpdateViewHierarchy(input, root_);
	}
}

void UIScreen::preRender() {
	Thin3DContext *thin3d = screenManager()->getThin3DContext();
	if (!thin3d) {
		return;
	}
	thin3d->Begin(true, 0xFF000000, 0.0f, 0);

	T3DViewport viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = pixel_xres;
	viewport.Height = pixel_yres;
	viewport.MaxDepth = 1.0;
	viewport.MinDepth = 0.0;
	thin3d->SetViewports(1, &viewport);
	thin3d->SetTargetSize(pixel_xres, pixel_yres);
}

void UIScreen::postRender() {
	Thin3DContext *thin3d = screenManager()->getThin3DContext();
	if (!thin3d) {
		return;
	}
	thin3d->End();
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

bool UIScreen::touch(const TouchInput &touch) {
	if (root_) {
		if (ClickDebug && (touch.flags & TOUCH_DOWN)) {
			ILOG("Touch down!");
			std::vector<UI::View *> views;
			root_->Query(touch.x, touch.y, views);
			for (auto view : views) {
				ILOG("%s", view->Describe().c_str());
			}
		}

		UI::TouchEvent(touch, root_);
		return true;
	}
	return false;
}

bool UIScreen::key(const KeyInput &key) {
	if (root_) {
		return UI::KeyEvent(key, root_);
	}
	return false;
}

bool UIDialogScreen::key(const KeyInput &key) {
	bool retval = UIScreen::key(key);
	if (!retval && (key.flags & KEY_DOWN) && UI::IsEscapeKey(key)) {
		if (finished_) {
			ELOG("Screen already finished");
		} else {
			finished_ = true;
			screenManager()->finishDialog(this, DR_BACK);
		}
		return true;
	}
	return retval;
}

bool UIScreen::axis(const AxisInput &axis) {
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
		return true;
	}
	return (pressed & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN)) != 0;
}

UI::EventReturn UIScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_BACK);
	return UI::EVENT_DONE;
}

UI::EventReturn UIScreen::OnOK(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn UIScreen::OnCancel(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

PopupScreen::PopupScreen(std::string title, std::string button1, std::string button2)
	: box_(0), defaultButton_(nullptr), title_(title) {
	I18NCategory *di = GetI18NCategory("Dialog");
	if (!button1.empty())
		button1_ = di->T(button1.c_str());
	if (!button2.empty())
		button2_ = di->T(button2.c_str());
}

bool PopupScreen::touch(const TouchInput &touch) {
	if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0) {
		return UIDialogScreen::touch(touch);
	}

	if (!box_->GetBounds().Contains(touch.x, touch.y))
		screenManager()->finishDialog(this, DR_BACK);

	return UIDialogScreen::touch(touch);
}

bool PopupScreen::key(const KeyInput &key) {
	if (key.flags & KEY_DOWN) {
		if (key.keyCode == NKCODE_ENTER && defaultButton_) {
			UI::EventParams e;
			defaultButton_->OnClick.Trigger(e);
			return true;
		}
	}

	return UIDialogScreen::key(key);
}

void PopupScreen::CreateViews() {
	using namespace UI;

	UIContext &dc = *screenManager()->getUIContext();

	AnchorLayout *anchor = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	anchor->Overflow(false);
	root_ = anchor;

	float yres = screenManager()->getUIContext()->GetBounds().h;

	box_ = new LinearLayout(ORIENT_VERTICAL,
		new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, dc.GetBounds().centerX(), dc.GetBounds().centerY(), NONE, NONE, true));

	root_->Add(box_);
	box_->SetBG(UI::Drawable(0xFF303030));
	box_->SetHasDropShadow(true);

	View *title = new PopupHeader(title_);
	box_->Add(title);

	CreatePopupContents(box_);
	root_->SetDefaultFocusView(box_);

	if (ShowButtons() && !button1_.empty()) {
		// And the two buttons at the bottom.
		LinearLayout *buttonRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(200, WRAP_CONTENT));
		buttonRow->SetSpacing(0);
		Margins buttonMargins(5, 5);

		// Adjust button order to the platform default.
#if defined(_WIN32)
		defaultButton_ = buttonRow->Add(new Button(button1_, new LinearLayoutParams(1.0f, buttonMargins)));
		defaultButton_->OnClick.Handle(this, &PopupScreen::OnOK);
		if (!button2_.empty())
			buttonRow->Add(new Button(button2_, new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle(this, &PopupScreen::OnCancel);
#else
		if (!button2_.empty())
			buttonRow->Add(new Button(button2_, new LinearLayoutParams(1.0f)))->OnClick.Handle(this, &PopupScreen::OnCancel);
		defaultButton_ = buttonRow->Add(new Button(button1_, new LinearLayoutParams(1.0f)));
		defaultButton_->OnClick.Handle(this, &PopupScreen::OnOK);
#endif

		box_->Add(buttonRow);
	}
}

void MessagePopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	std::vector<std::string> messageLines;
	SplitString(message_, '\n', messageLines);
	for (const auto& lineOfText : messageLines)
		parent->Add(new UI::TextView(lineOfText, ALIGN_LEFT | ALIGN_VCENTER, false));
}

void MessagePopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		if (callback_)
			callback_(true);
	} else {
		if (callback_)
			callback_(false);
	}
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

	listView_ = parent->Add(new ListView(&adaptor_, hidden_)); //, new LinearLayoutParams(1.0)));
	listView_->SetMaxHeight(screenManager()->getUIContext()->GetBounds().h - 140);
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

std::string ChopTitle(const std::string &title) {
	size_t pos = title.find('\n');
	if (pos != title.npos) {
		return title.substr(0, pos);
	}
	return title;
}

UI::EventReturn PopupMultiChoice::HandleClick(UI::EventParams &e) {
	restoreFocus_ = HasFocus();

	I18NCategory *category = category_ ? GetI18NCategory(category_) : nullptr;

	std::vector<std::string> choices;
	for (int i = 0; i < numChoices_; i++) {
		choices.push_back(category ? category->T(choices_[i]) : choices_[i]);
	}

	ListPopupScreen *popupScreen = new ListPopupScreen(ChopTitle(text_), choices, *value_ - minVal_,
		std::bind(&PopupMultiChoice::ChoiceCallback, this, placeholder::_1));
	popupScreen->SetHiddenChoices(hidden_);
	screenManager_->push(popupScreen);
	return UI::EVENT_DONE;
}

void PopupMultiChoice::Update(const InputState &input_state) {
	UpdateText();
}

void PopupMultiChoice::UpdateText() {
	I18NCategory *category = GetI18NCategory(category_);
	// Clamp the value to be safe.
	if (*value_ < minVal_ || *value_ > minVal_ + numChoices_ - 1) {
		valueText_ = "(invalid choice)";  // Shouldn't happen. Should be no need to translate this.
	} else {
		valueText_ = category ? category->T(choices_[*value_ - minVal_]) : choices_[*value_ - minVal_];
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

		if (restoreFocus_) {
			SetFocusedView(this);
		}
	}
}

void PopupMultiChoice::Draw(UIContext &dc) {
	Style style = dc.theme->itemStyle;
	if (!IsEnabled()) {
		style = dc.theme->itemDisabledStyle;
	}
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);

	float ignore;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, valueText_.c_str(), &textPadding_.right, &ignore, ALIGN_RIGHT | ALIGN_VCENTER);
	textPadding_.right += paddingX;

	Choice::Draw(dc);
	dc.DrawText(valueText_.c_str(), bounds_.x2() - paddingX, bounds_.centerY(), style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}

PopupSliderChoice::PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), step_(1), units_(units), screenManager_(screenManager) {
	fmt_ = "%i";
	OnClick.Handle(this, &PopupSliderChoice::HandleClick);
}

PopupSliderChoice::PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, int step, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), step_(step), units_(units), screenManager_(screenManager) {
	fmt_ = "%i";
	OnClick.Handle(this, &PopupSliderChoice::HandleClick);
}

PopupSliderChoiceFloat::PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), step_(1.0f), units_(units), screenManager_(screenManager) {
	fmt_ = "%2.2f";
	OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
}

PopupSliderChoiceFloat::PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, float step, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), step_(step), units_(units), screenManager_(screenManager) {
	fmt_ = "%2.2f";
	OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
}

EventReturn PopupSliderChoice::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	SliderPopupScreen *popupScreen = new SliderPopupScreen(value_, minValue_, maxValue_, ChopTitle(text_), step_, units_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoice::HandleChange);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

EventReturn PopupSliderChoice::HandleChange(EventParams &e) {
	e.v = this;
	OnChange.Trigger(e);

	if (restoreFocus_) {
		SetFocusedView(this);
	}
	return EVENT_DONE;
}

void PopupSliderChoice::Draw(UIContext &dc) {
	Style style = dc.theme->itemStyle;
	if (!IsEnabled()) {
		style = dc.theme->itemDisabledStyle;
	}
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);

	char temp[32];
	if (zeroLabel_.size() && *value_ == 0) {
		strcpy(temp, zeroLabel_.c_str());
	} else {
		sprintf(temp, fmt_, *value_);
	}

	float ignore;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, temp, &textPadding_.right, &ignore, ALIGN_RIGHT | ALIGN_VCENTER);
	textPadding_.right += paddingX;

	Choice::Draw(dc);
	dc.DrawText(temp, bounds_.x2() - paddingX, bounds_.centerY(), style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}

EventReturn PopupSliderChoiceFloat::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	SliderFloatPopupScreen *popupScreen = new SliderFloatPopupScreen(value_, minValue_, maxValue_, ChopTitle(text_), step_, units_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoiceFloat::HandleChange);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

EventReturn PopupSliderChoiceFloat::HandleChange(EventParams &e) {
	e.v = this;
	OnChange.Trigger(e);

	if (restoreFocus_) {
		SetFocusedView(this);
	}
	return EVENT_DONE;
}

void PopupSliderChoiceFloat::Draw(UIContext &dc) {
	Style style = dc.theme->itemStyle;
	if (!IsEnabled()) {
		style = dc.theme->itemDisabledStyle;
	}
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);

	char temp[32];
	if (zeroLabel_.size() && *value_ == 0.0f) {
		strcpy(temp, zeroLabel_.c_str());
	} else {
		sprintf(temp, fmt_, *value_);
	}

	float ignore;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, temp, &textPadding_.right, &ignore, ALIGN_RIGHT | ALIGN_VCENTER);
	textPadding_.right += paddingX;

	Choice::Draw(dc);
	dc.DrawText(temp, bounds_.x2() - paddingX, bounds_.centerY(), style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}

EventReturn SliderPopupScreen::OnDecrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ -= step_;
	slider_->Clamp();
	changing_ = true;
	char temp[64];
	sprintf(temp, "%d", sliderValue_);
	edit_->SetText(temp);
	changing_ = false;
	return EVENT_DONE;
}

EventReturn SliderPopupScreen::OnIncrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ += step_;
	slider_->Clamp();
	changing_ = true;
	char temp[64];
	sprintf(temp, "%d", sliderValue_);
	edit_->SetText(temp);
	changing_ = false;
	return EVENT_DONE;
}

EventReturn SliderPopupScreen::OnSliderChange(EventParams &params) {
	changing_ = true;
	char temp[64];
	sprintf(temp, "%d", sliderValue_);
	edit_->SetText(temp);
	changing_ = false;
	return EVENT_DONE;
}

EventReturn SliderPopupScreen::OnTextChange(EventParams &params) {
	if (!changing_) {
		sliderValue_ = atoi(edit_->GetText().c_str());
		slider_->Clamp();
	}
	return EVENT_DONE;
}

void SliderPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	sliderValue_ = *value_;
	LinearLayout *vert = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(UI::Margins(10, 10))));
	slider_ = new Slider(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 10)));
	slider_->OnChange.Handle(this, &SliderPopupScreen::OnSliderChange);
	vert->Add(slider_);
	LinearLayout *lin = vert->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::Margins(10, 10))));
	lin->Add(new Button(" - "))->OnClick.Handle(this, &SliderPopupScreen::OnDecrease);
	lin->Add(new Button(" + "))->OnClick.Handle(this, &SliderPopupScreen::OnIncrease);
	char temp[64];
	sprintf(temp, "%d", sliderValue_);
	edit_ = new TextEdit(temp, "", new LinearLayoutParams(10.0f));
	edit_->SetMaxLen(16);
	edit_->OnTextChange.Handle(this, &SliderPopupScreen::OnTextChange);
	changing_ = false;
	lin->Add(edit_);
	if (!units_.empty())
		lin->Add(new TextView(units_, new LinearLayoutParams(10.0f)));

	if (IsFocusMovementEnabled())
		UI::SetFocusedView(slider_);
}

void SliderFloatPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	sliderValue_ = *value_;
	LinearLayout *vert = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(UI::Margins(10, 10))));
	slider_ = new SliderFloat(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 10)));
	slider_->OnChange.Handle(this, &SliderFloatPopupScreen::OnSliderChange);
	vert->Add(slider_);
	LinearLayout *lin = vert->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::Margins(10, 10))));
	lin->Add(new Button(" - "))->OnClick.Handle(this, &SliderFloatPopupScreen::OnDecrease);
	lin->Add(new Button(" + "))->OnClick.Handle(this, &SliderFloatPopupScreen::OnIncrease);
	char temp[64];
	sprintf(temp, "%0.3f", sliderValue_);
	edit_ = new TextEdit(temp, "", new LinearLayoutParams(10.0f));
	edit_->SetMaxLen(16);
	edit_->OnTextChange.Handle(this, &SliderFloatPopupScreen::OnTextChange);
	changing_ = false;
	lin->Add(edit_);
	if (!units_.empty())
		lin->Add(new TextView(units_, new LinearLayoutParams(10.0f)));

	// slider_ = parent->Add(new SliderFloat(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 5))));
	if (IsFocusMovementEnabled())
		UI::SetFocusedView(slider_);
}

EventReturn SliderFloatPopupScreen::OnDecrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ -= step_;
	slider_->Clamp();
	changing_ = true;
	char temp[64];
	sprintf(temp, "%0.3f", sliderValue_);
	edit_->SetText(temp);
	changing_ = false;
	return EVENT_DONE;
}

EventReturn SliderFloatPopupScreen::OnIncrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ += step_;
	slider_->Clamp();
	changing_ = true;
	char temp[64];
	sprintf(temp, "%0.3f", sliderValue_);
	edit_->SetText(temp);
	changing_ = false;
	return EVENT_DONE;
}

EventReturn SliderFloatPopupScreen::OnSliderChange(EventParams &params) {
	changing_ = true;
	char temp[64];
	sprintf(temp, "%0.3f", sliderValue_);
	edit_->SetText(temp);
	changing_ = false;
	return EVENT_DONE;
}

EventReturn SliderFloatPopupScreen::OnTextChange(EventParams &params) {
	if (!changing_) {
		sliderValue_ = atof(edit_->GetText().c_str());
		slider_->Clamp();
	}
	return EVENT_DONE;
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

PopupTextInputChoice::PopupTextInputChoice(std::string *value, const std::string &title, const std::string &placeholder, int maxLen, ScreenManager *screenManager, LayoutParams *layoutParams)
: Choice(title, "", false, layoutParams), screenManager_(screenManager), value_(value), placeHolder_(placeholder), maxLen_(maxLen) {
	OnClick.Handle(this, &PopupTextInputChoice::HandleClick);
}

EventReturn PopupTextInputChoice::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	TextEditPopupScreen *popupScreen = new TextEditPopupScreen(value_, placeHolder_, ChopTitle(text_), maxLen_);
	popupScreen->OnChange.Handle(this, &PopupTextInputChoice::HandleChange);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

void PopupTextInputChoice::Draw(UIContext &dc) {
	Style style = dc.theme->itemStyle;
	if (!IsEnabled()) {
		style = dc.theme->itemDisabledStyle;
	}
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);

	float ignore;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, value_->c_str(), &textPadding_.right, &ignore, ALIGN_RIGHT | ALIGN_VCENTER);
	textPadding_.right += paddingX;

	Choice::Draw(dc);
	dc.DrawText(value_->c_str(), bounds_.x2() - 12, bounds_.centerY(), style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}

EventReturn PopupTextInputChoice::HandleChange(EventParams &e) {
	e.v = this;
	OnChange.Trigger(e);

	if (restoreFocus_) {
		SetFocusedView(this);
	}
	return EVENT_DONE;
}

void TextEditPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	textEditValue_ = *value_;
	LinearLayout *lin = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams((UI::Size)300, WRAP_CONTENT)));
	edit_ = new TextEdit(textEditValue_, placeholder_, new LinearLayoutParams(1.0f));
	edit_->SetMaxLen(maxLen_);
	lin->Add(edit_);

	if (IsFocusMovementEnabled())
		UI::SetFocusedView(edit_);
}

void TextEditPopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = edit_->GetText();
		EventParams e;
		e.v = edit_;
		OnChange.Trigger(e);
	}
}

void ChoiceWithValueDisplay::Draw(UIContext &dc) {
	Style style = dc.theme->itemStyle;
	if (!IsEnabled()) {
		style = dc.theme->itemDisabledStyle;
	}
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);

	I18NCategory *category = GetI18NCategory(category_);
	std::ostringstream valueText;
	if (sValue_ != nullptr) {
		if (category)
			valueText << category->T(*sValue_);
		else
			valueText << *sValue_;
	} else if (iValue_ != nullptr) {
		valueText << *iValue_;
	}

	float ignore;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, valueText.str().c_str(), &textPadding_.right, &ignore, ALIGN_RIGHT | ALIGN_VCENTER);
	textPadding_.right += paddingX;

	Choice::Draw(dc);
	dc.DrawText(valueText.str().c_str(), bounds_.x2() - paddingX, bounds_.centerY(), style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}

}  // namespace UI
