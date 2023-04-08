#include <algorithm>
#include <sstream>

#include "Common/UI/PopupScreens.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/UI/Root.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"

namespace UI {

void MessagePopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();

	std::vector<std::string> messageLines;
	SplitString(message_, '\n', messageLines);
	for (const auto &lineOfText : messageLines)
		parent->Add(new UI::TextView(lineOfText, ALIGN_LEFT | ALIGN_VCENTER, false))->SetTextColor(dc.theme->popupStyle.fgColor);
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
	TriggerFinish(DR_OK);
	OnChoice.Dispatch(e);
	return UI::EVENT_DONE;
}

PopupContextMenuScreen::PopupContextMenuScreen(const ContextMenuItem *items, size_t itemCount, I18NCat category, UI::View *sourceView)
	: PopupScreen("", "", ""), items_(items), itemCount_(itemCount), category_(category), sourceView_(sourceView)
{
	enabled_.resize(itemCount, true);
	SetPopupOrigin(sourceView);
}

void PopupContextMenuScreen::CreatePopupContents(UI::ViewGroup *parent) {
	auto category = GetI18NCategory(category_);

	for (size_t i = 0; i < itemCount_; i++) {
		if (items_[i].imageID) {
			Choice *choice = new Choice(category->T(items_[i].text), ImageID(items_[i].imageID));
			parent->Add(choice);
			if (enabled_[i]) {
				choice->OnClick.Add([=](EventParams &p) {
					TriggerFinish(DR_OK);
				p.a = (uint32_t)i;
				OnChoice.Dispatch(p);
				return EVENT_DONE;
					});
			} else {
				choice->SetEnabled(false);
			}
		}
	}

	// Hacky: Override the position to look like a popup menu.
	AnchorLayoutParams *ap = (AnchorLayoutParams *)parent->GetLayoutParams();
	ap->center = false;
	ap->left = sourceView_->GetBounds().x;
	ap->top = sourceView_->GetBounds().y2();
}

std::string ChopTitle(const std::string &title) {
	size_t pos = title.find('\n');
	if (pos != title.npos) {
		return title.substr(0, pos);
	}
	return title;
}

UI::EventReturn PopupMultiChoice::HandleClick(UI::EventParams &e) {
	restoreFocus_ = HasFocus();

	auto category = GetI18NCategory(category_);

	std::vector<std::string> choices;
	for (int i = 0; i < numChoices_; i++) {
		choices.push_back(category ? category->T(choices_[i]) : choices_[i]);
	}

	ListPopupScreen *popupScreen = new ListPopupScreen(ChopTitle(text_), choices, *value_ - minVal_,
		std::bind(&PopupMultiChoice::ChoiceCallback, this, std::placeholders::_1));
	popupScreen->SetHiddenChoices(hidden_);
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
	screenManager_->push(popupScreen);
	return UI::EVENT_DONE;
}

void PopupMultiChoice::Update() {
	UpdateText();
}

void PopupMultiChoice::UpdateText() {
	if (!choices_)
		return;
	auto category = GetI18NCategory(category_);
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

		UI::EventParams e{};
		e.v = this;
		e.a = num;
		OnChoice.Trigger(e);

		if (restoreFocus_) {
			SetFocusedView(this);
		}
		PostChoiceCallback(num);
	}
}

std::string PopupMultiChoice::ValueText() const {
	return valueText_;
}

PopupSliderChoice::PopupSliderChoice(int *value, int minValue, int maxValue, int defaultValue, const std::string &text, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(1), units_(units), screenManager_(screenManager) {
	fmt_ = "%i";
	OnClick.Handle(this, &PopupSliderChoice::HandleClick);
}

PopupSliderChoice::PopupSliderChoice(int *value, int minValue, int maxValue, int defaultValue, const std::string &text, int step, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(step), units_(units), screenManager_(screenManager) {
	fmt_ = "%i";
	OnClick.Handle(this, &PopupSliderChoice::HandleClick);
}

PopupSliderChoiceFloat::PopupSliderChoiceFloat(float *value, float minValue, float maxValue, float defaultValue, const std::string &text, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(1.0f), units_(units), screenManager_(screenManager) {
	fmt_ = "%2.2f";
	OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
}

PopupSliderChoiceFloat::PopupSliderChoiceFloat(float *value, float minValue, float maxValue, float defaultValue, const std::string &text, float step, ScreenManager *screenManager, const std::string &units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(step), units_(units), screenManager_(screenManager) {
	fmt_ = "%2.2f";
	OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
}

EventReturn PopupSliderChoice::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	SliderPopupScreen *popupScreen = new SliderPopupScreen(value_, minValue_, maxValue_, defaultValue_, ChopTitle(text_), step_, units_);
	if (!negativeLabel_.empty())
		popupScreen->SetNegativeDisable(negativeLabel_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoice::HandleChange);
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
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

std::string PopupSliderChoice::ValueText() const {
	// Always good to have space for Unicode.
	char temp[256];
	if (zeroLabel_.size() && *value_ == 0) {
		strcpy(temp, zeroLabel_.c_str());
	} else if (negativeLabel_.size() && *value_ < 0) {
		strcpy(temp, negativeLabel_.c_str());
	} else {
		sprintf(temp, fmt_, *value_);
	}

	return temp;
}

EventReturn PopupSliderChoiceFloat::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	SliderFloatPopupScreen *popupScreen = new SliderFloatPopupScreen(value_, minValue_, maxValue_, defaultValue_, ChopTitle(text_), step_, units_, liveUpdate_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoiceFloat::HandleChange);
	popupScreen->SetHasDropShadow(hasDropShadow_);
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
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

std::string PopupSliderChoiceFloat::ValueText() const {
	char temp[256];
	if (zeroLabel_.size() && *value_ == 0.0f) {
		strcpy(temp, zeroLabel_.c_str());
	} else {
		sprintf(temp, fmt_, *value_);
	}

	return temp;
}

EventReturn SliderPopupScreen::OnDecrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ -= step_;
	slider_->Clamp();
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	disabled_ = false;
	return EVENT_DONE;
}

EventReturn SliderPopupScreen::OnIncrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ += step_;
	slider_->Clamp();
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	disabled_ = false;
	return EVENT_DONE;
}

EventReturn SliderPopupScreen::OnSliderChange(EventParams &params) {
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	disabled_ = false;
	return EVENT_DONE;
}

EventReturn SliderPopupScreen::OnTextChange(EventParams &params) {
	if (!changing_) {
		sliderValue_ = atoi(edit_->GetText().c_str());
		disabled_ = false;
		slider_->Clamp();
	}
	return EVENT_DONE;
}

void SliderPopupScreen::UpdateTextBox() {
	char temp[64];
	sprintf(temp, "%d", sliderValue_);
	edit_->SetText(temp);
}

void SliderPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();
	auto di = GetI18NCategory(I18NCat::DIALOG);

	sliderValue_ = *value_;
	if (disabled_ && sliderValue_ < 0)
		sliderValue_ = 0;
	LinearLayout *vert = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(UI::Margins(10, 10))));
	slider_ = new Slider(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 10)));
	slider_->OnChange.Handle(this, &SliderPopupScreen::OnSliderChange);
	vert->Add(slider_);

	LinearLayout *lin = vert->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::Margins(10, 10))));
	lin->Add(new Button(" - "))->OnClick.Handle(this, &SliderPopupScreen::OnDecrease);
	lin->Add(new Button(" + "))->OnClick.Handle(this, &SliderPopupScreen::OnIncrease);

	edit_ = new TextEdit("", Title(), "", new LinearLayoutParams(1.0f));
	edit_->SetMaxLen(16);
	edit_->SetTextColor(dc.theme->itemStyle.fgColor);
	edit_->SetTextAlign(FLAG_DYNAMIC_ASCII);
	edit_->OnTextChange.Handle(this, &SliderPopupScreen::OnTextChange);
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	lin->Add(edit_);

	if (!units_.empty())
		lin->Add(new TextView(units_))->SetTextColor(dc.theme->itemStyle.fgColor);

	if (defaultValue_ != NO_DEFAULT_FLOAT) {
		//LinearLayout *lin2 = vert->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::Margins(10, 10))));
		lin->Add(new Button(di->T("Reset")))->OnClick.Add([=](UI::EventParams &) {
			sliderValue_ = defaultValue_;
			changing_ = true;
			UpdateTextBox();
			changing_ = false;
			return UI::EVENT_DONE;
			});
	}

	if (!negativeLabel_.empty())
		vert->Add(new CheckBox(&disabled_, negativeLabel_));

	if (IsFocusMovementEnabled())
		UI::SetFocusedView(slider_);
}

void SliderFloatPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();
	auto di = GetI18NCategory(I18NCat::DIALOG);

	sliderValue_ = *value_;
	LinearLayout *vert = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(UI::Margins(10, 10))));
	slider_ = new SliderFloat(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 10)));
	slider_->OnChange.Handle(this, &SliderFloatPopupScreen::OnSliderChange);
	vert->Add(slider_);

	LinearLayout *lin = vert->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::Margins(10, 10))));
	lin->Add(new Button(" - "))->OnClick.Handle(this, &SliderFloatPopupScreen::OnDecrease);
	lin->Add(new Button(" + "))->OnClick.Handle(this, &SliderFloatPopupScreen::OnIncrease);

	edit_ = new TextEdit("", Title(), "", new LinearLayoutParams(1.0f));
	edit_->SetMaxLen(16);
	edit_->SetTextColor(dc.theme->itemStyle.fgColor);
	edit_->SetTextAlign(FLAG_DYNAMIC_ASCII);
	edit_->OnTextChange.Handle(this, &SliderFloatPopupScreen::OnTextChange);
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	lin->Add(edit_);
	if (!units_.empty())
		lin->Add(new TextView(units_))->SetTextColor(dc.theme->itemStyle.fgColor);

	if (defaultValue_ != NO_DEFAULT_FLOAT) {
		lin->Add(new Button(di->T("Reset")))->OnClick.Add([=](UI::EventParams &) {
			sliderValue_ = defaultValue_;
			if (liveUpdate_) {
				*value_ = defaultValue_;
			}
			return UI::EVENT_DONE;
		});
	}

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
	UpdateTextBox();
	changing_ = false;
	if (liveUpdate_) {
		*value_ = sliderValue_;
	}
	return EVENT_DONE;
}

EventReturn SliderFloatPopupScreen::OnIncrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ += step_;
	slider_->Clamp();
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	if (liveUpdate_) {
		*value_ = sliderValue_;
	}
	return EVENT_DONE;
}

EventReturn SliderFloatPopupScreen::OnSliderChange(EventParams &params) {
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	if (liveUpdate_) {
		*value_ = sliderValue_;
	}
	return EVENT_DONE;
}

void SliderFloatPopupScreen::UpdateTextBox() {
	char temp[64];
	sprintf(temp, "%0.3f", sliderValue_);
	edit_->SetText(temp);
}

EventReturn SliderFloatPopupScreen::OnTextChange(EventParams &params) {
	if (!changing_) {
		sliderValue_ = atof(edit_->GetText().c_str());
		slider_->Clamp();
		if (liveUpdate_) {
			*value_ = sliderValue_;
		}
	}
	return EVENT_DONE;
}

void SliderPopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = disabled_ ? -1 : sliderValue_;
		EventParams e{};
		e.v = nullptr;
		e.a = *value_;
		OnChange.Trigger(e);
	}
}

void SliderFloatPopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = sliderValue_;
		EventParams e{};
		e.v = nullptr;
		e.a = (int)*value_;
		e.f = *value_;
		OnChange.Trigger(e);
	} else {
		*value_ = originalValue_;
	}
}

PopupTextInputChoice::PopupTextInputChoice(std::string *value, const std::string &title, const std::string &placeholder, int maxLen, ScreenManager *screenManager, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(title, layoutParams), screenManager_(screenManager), value_(value), placeHolder_(placeholder), maxLen_(maxLen) {
	OnClick.Handle(this, &PopupTextInputChoice::HandleClick);
}

EventReturn PopupTextInputChoice::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	TextEditPopupScreen *popupScreen = new TextEditPopupScreen(value_, placeHolder_, ChopTitle(text_), maxLen_);
	popupScreen->OnChange.Handle(this, &PopupTextInputChoice::HandleChange);
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

std::string PopupTextInputChoice::ValueText() const {
	return *value_;
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
	UIContext &dc = *screenManager()->getUIContext();

	textEditValue_ = *value_;
	LinearLayout *lin = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams((UI::Size)300, WRAP_CONTENT)));
	edit_ = new TextEdit(textEditValue_, Title(), placeholder_, new LinearLayoutParams(1.0f));
	edit_->SetMaxLen(maxLen_);
	edit_->SetTextColor(dc.theme->popupStyle.fgColor);
	lin->Add(edit_);

	UI::SetFocusedView(edit_);
}

void TextEditPopupScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = StripSpaces(edit_->GetText());
		EventParams e{};
		e.v = edit_;
		OnChange.Trigger(e);
	}
}

void AbstractChoiceWithValueDisplay::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	const std::string valueText = ValueText();
	int paddingX = 12;
	// Assume we want at least 20% of the size for the label, at a minimum.
	float availWidth = (horiz.size - paddingX * 2) * (text_.empty() ? 1.0f : 0.8f);
	if (availWidth < 0) {
		availWidth = 65535.0f;
	}
	float scale = CalculateValueScale(dc, valueText, availWidth);
	Bounds availBounds(0, 0, availWidth, vert.size);

	float valueW, valueH;
	dc.MeasureTextRect(dc.theme->uiFont, scale, scale, valueText.c_str(), (int)valueText.size(), availBounds, &valueW, &valueH, ALIGN_RIGHT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
	valueW += paddingX;

	// Give the choice itself less space to grow in, so it shrinks if needed.
	// MeasureSpec horizLabel = horiz;
	// horizLabel.size -= valueW;
	Choice::GetContentDimensionsBySpec(dc, horiz, vert, w, h);

	w += valueW;
	// Fill out anyway if there's space.
	if (horiz.type == AT_MOST && w < horiz.size) {
		w = horiz.size;
	}
	h = std::max(h, valueH);
}

void AbstractChoiceWithValueDisplay::Draw(UIContext &dc) {
	Style style = dc.theme->itemStyle;
	if (!IsEnabled()) {
		style = dc.theme->itemDisabledStyle;
	}
	if (HasFocus()) {
		style = dc.theme->itemFocusedStyle;
	}
	if (down_) {
		style = dc.theme->itemDownStyle;
	}
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);

	const std::string valueText = ValueText();

	// If there is a label, assume we want at least 20% of the size for it, at a minimum.

	if (!text_.empty()) {
		float availWidth = (bounds_.w - paddingX * 2) * 0.8f;
		float scale = CalculateValueScale(dc, valueText, availWidth);

		float w, h;
		Bounds availBounds(0, 0, availWidth, bounds_.h);
		dc.MeasureTextRect(dc.theme->uiFont, scale, scale, valueText.c_str(), (int)valueText.size(), availBounds, &w, &h, ALIGN_RIGHT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
		textPadding_.right = w + paddingX;

		Choice::Draw(dc);
		dc.SetFontScale(scale, scale);
		Bounds valueBounds(bounds_.x2() - textPadding_.right, bounds_.y, w, bounds_.h);
		dc.DrawTextRect(valueText.c_str(), valueBounds, style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
		dc.SetFontScale(1.0f, 1.0f);
	} else {
		Choice::Draw(dc);
		float scale = CalculateValueScale(dc, valueText, bounds_.w);
		dc.SetFontScale(scale, scale);
		dc.DrawTextRect(valueText.c_str(), bounds_.Expand(-paddingX, 0.0f), style.fgColor, ALIGN_LEFT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
		dc.SetFontScale(1.0f, 1.0f);
	}
}

float AbstractChoiceWithValueDisplay::CalculateValueScale(const UIContext &dc, const std::string &valueText, float availWidth) const {
	float actualWidth, actualHeight;
	Bounds availBounds(0, 0, availWidth, bounds_.h);
	dc.MeasureTextRect(dc.theme->uiFont, 1.0f, 1.0f, valueText.c_str(), (int)valueText.size(), availBounds, &actualWidth, &actualHeight);
	if (actualWidth > availWidth) {
		return std::max(0.8f, availWidth / actualWidth);
	}
	return 1.0f;
}

std::string ChoiceWithValueDisplay::ValueText() const {
	auto category = GetI18NCategory(category_);
	std::ostringstream valueText;
	if (translateCallback_ && sValue_) {
		valueText << translateCallback_(sValue_->c_str());
	} else if (sValue_ != nullptr) {
		if (category)
			valueText << category->T(*sValue_);
		else
			valueText << *sValue_;
	} else if (iValue_ != nullptr) {
		valueText << *iValue_;
	}

	return valueText.str();
}

}  // namespace
