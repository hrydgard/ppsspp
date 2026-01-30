#include <algorithm>
#include <sstream>
#include <cstring>

#include "Common/UI/PopupScreens.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/UI/Root.h"
#include "Common/UI/Notice.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/Display.h"
#include "Common/Math/curves.h"
#include "Common/Render/DrawBuffer.h"

namespace UI {

PopupScreen::PopupScreen(std::string_view title, std::string_view button1, std::string_view button2)
	: title_(title), button1_(button1), button2_(button2) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	// Auto-assign images. A bit hack to have this here.
	if (button1 == di->T("Delete") || button1 == di->T("Move to trash")) {
		button1Image_ = ImageID("I_TRASHCAN");
	}

	alpha_ = 0.0f;  // inherited
	ignoreInsets_ = true;  // for layout purposes.
}

void PopupScreen::touch(const TouchInput &touch) {
	if (!box_ || (touch.flags & TouchInputFlags::DOWN) == 0) {
		// Handle down-presses here.
		UIDialogScreen::touch(touch);
		return;
	}

	// Extra bounds to avoid closing the dialog while trying to aim for something
	// near the edge. Now that we only close on actual down-events, we can shrink
	// this border a bit.
	if (!box_->GetBounds().Expand(30.0f, 30.0f).Contains(touch.x, touch.y)) {
		TriggerFinish(DR_CANCEL);
	}

	UIDialogScreen::touch(touch);
}

bool PopupScreen::key(const KeyInput &key) {
	if (key.flags & KeyInputFlags::DOWN) {
		if ((key.keyCode == NKCODE_ENTER || key.keyCode == NKCODE_NUMPAD_ENTER) && defaultButton_) {
			UI::EventParams e{};
			defaultButton_->OnClick.Trigger(e);
			return true;
		}
	}

	return UIDialogScreen::key(key);
}

void PopupScreen::update() {
	UIDialogScreen::update();

	if (defaultButton_)
		defaultButton_->SetEnabled(CanComplete(DR_OK));

	float animatePos = 1.0f;

	++frames_;
	if (finishFrame_ >= 0) {
		float leadOut = bezierEaseInOut((frames_ - finishFrame_) * (1.0f / (float)FRAMES_LEAD_OUT));
		animatePos = 1.0f - leadOut;

		if (frames_ >= finishFrame_ + FRAMES_LEAD_OUT) {
			// Actual finish happens here.
			screenManager()->finishDialog(this, finishResult_);
		}
	} else if (frames_ < FRAMES_LEAD_IN) {
		float leadIn = bezierEaseInOut(frames_ * (1.0f / (float)FRAMES_LEAD_IN));
		animatePos = leadIn;
	}

	if (animatePos < 1.0f) {
		alpha_ = animatePos;
		scale_.x = 0.9f + animatePos * 0.1f;
		scale_.y = 0.9f + animatePos * 0.1f;

		if (hasPopupOrigin_) {
			float xoff = popupOrigin_.x - g_display.dp_xres / 2;
			float yoff = popupOrigin_.y - g_display.dp_yres / 2;

			// Pull toward the origin a bit.
			translation_.x = xoff * (1.0f - animatePos) * 0.2f;
			translation_.y = yoff * (1.0f - animatePos) * 0.2f;
		} else {
			translation_.y = -g_display.dp_yres * (1.0f - animatePos) * 0.2f;
		}
	} else {
		alpha_ = 1.0f;
		scale_.x = 1.0f;
		scale_.y = 1.0f;
		translation_.x = 0.0f;
		translation_.y = 0.0f;
	}
}

void PopupScreen::SetPopupOrigin(const UI::View *view) {
	hasPopupOrigin_ = true;
	popupOrigin_ = view->GetBounds().Center();
}

void PopupScreen::TriggerFinish(DialogResult result) {
	if (CanComplete(result)) {
		ignoreInput_ = true;
		finishFrame_ = frames_;
		finishResult_ = result;

		OnCompleted(result);
	}
	// Inform UI that popup close to hide OSK (if visible)
	System_NotifyUIEvent(UIEventNotification::POPUP_CLOSED);
}

void PopupScreen::CreateViews() {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();

	AnchorLayout *anchor = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	anchor->Overflow(false);
	root_ = anchor;

	const float ySize = FillVertical() ? dc.GetLayoutBounds().h - 30 : WRAP_CONTENT;

	int y = dc.GetBounds().centerY() + offsetY_;
	Centering vCentering = Centering::Vertical;
	if (alignTop_) {
		y = 0;
		vCentering = Centering::None;
	}

	const float popupWidth = PopupWidth();

	AnchorLayoutParams *anchorParams;
	// NOTE: We purely use the popup width here to decide the type of layout, instead of the device orientation.
	if (dc.GetLayoutBounds().w < popupWidth + 50) {
		anchorParams = new AnchorLayoutParams(popupWidth, ySize,
			10, y, 10, NONE, vCentering);
	} else {
		anchorParams = new AnchorLayoutParams(popupWidth, ySize,
			dc.GetBounds().centerX(), y, NONE, NONE, vCentering | Centering::Horizontal);
	}

	box_ = new LinearLayout(ORIENT_VERTICAL, anchorParams);

	root_->Add(box_);
	box_->SetBG(dc.GetTheme().popupStyle.background);
	box_->SetHasDropShadow(hasDropShadow_);
	// Since we scale a bit, make the dropshadow bleed past the edges.
	box_->SetDropShadowExpand(std::max(g_display.dp_xres, g_display.dp_yres));
	box_->SetSpacing(0.0f);

	if (!title_.empty()) {
		View *title = new PopupHeader(title_);
		box_->Add(title);
	}

	if (!notificationString_.empty()) {
		box_->Add(new NoticeView(notificationLevel_, notificationString_, "", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))))->SetWrapText(true);
	}

	CreatePopupContents(box_);
	root_->Recurse([](View *view) {
		view->SetPopupStyle(true);
	});

	root_->SetDefaultFocusView(box_);

	if (ShowButtons() && !button1_.empty()) {
		// And the two buttons at the bottom.
		LinearLayout *buttonRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(200, WRAP_CONTENT));
		buttonRow->SetSpacing(0);
		Margins buttonMargins(5, 5);

		// Adjust button order to the platform default.
		if (System_GetPropertyBool(SYSPROP_OK_BUTTON_LEFT)) {
			defaultButton_ = buttonRow->Add(new Choice(button1_, button1Image_, new LinearLayoutParams(1.0f, buttonMargins)));
			defaultButton_->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
			if (!button2_.empty()) {
				buttonRow->Add(new Choice(button2_, new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnCancel);
			}
		} else {
			if (!button2_.empty()) {
				buttonRow->Add(new Choice(button2_, new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnCancel);
			}
			defaultButton_ = buttonRow->Add(new Choice(button1_, button1Image_, new LinearLayoutParams(1.0f, buttonMargins)));
			defaultButton_->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
		}

		box_->Add(buttonRow);
	}
}

void MessagePopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();

	std::vector<std::string_view> messageLines;
	SplitString(message_, '\n', messageLines);
	for (auto lineOfText : messageLines) {
		parent->Add(new UI::TextView(lineOfText, ALIGN_LEFT | ALIGN_VCENTER | FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(8))));
	}
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

ListPopupScreen::~ListPopupScreen() {}

void ListPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	listView_ = parent->Add(new ListView(&adaptor_, hidden_, icons_)); //, new LinearLayoutParams(1.0)));
	listView_->SetMaxHeight(screenManager()->getUIContext()->GetBounds().h - 140);
	listView_->OnChoice.Handle(this, &ListPopupScreen::OnListChoice);
}

void ListPopupScreen::OnListChoice(UI::EventParams &e) {
	adaptor_.SetSelected(e.a);
	if (callback_)
		callback_(adaptor_.GetSelected());
	TriggerFinish(DR_OK);
	OnChoice.Dispatch(e);
}

void AbstractContextMenuScreen::AlignPopup(UI::View *parent) {
	if (!sourceView_) {
		// No menu-like arrangement
		return;
	}

	// Hacky: Override the position to look like a popup menu.

	AnchorLayoutParams *ap = (AnchorLayoutParams *)parent->GetLayoutParams();
	ap->centering = Centering::None;
	// TODO: Some more robust check here...
	if (sourceView_->GetBounds().x2() > g_display.dp_xres - 300) {
		ap->left = NONE;
		// NOTE: Right here is not distance from the left, but distance from the right. Doh.
		ap->right = g_display.dp_xres - sourceView_->GetBounds().x2();
	} else {
		ap->left = sourceView_->GetBounds().x;
		ap->right = NONE;
	}
	ap->top = sourceView_->GetBounds().y2();
	ap->bottom = NONE;
}

PopupContextMenuScreen::PopupContextMenuScreen(const ContextMenuItem *items, size_t itemCount, I18NCat category, UI::View *sourceView)
	: AbstractContextMenuScreen(sourceView), items_(items), itemCount_(itemCount), category_(category)
{
	enabled_.resize(itemCount, true);
	SetPopupOrigin(sourceView);
}

void PopupContextMenuScreen::CreatePopupContents(UI::ViewGroup *parent) {
	auto category = GetI18NCategory(category_);

	for (size_t i = 0; i < itemCount_; i++) {
		Choice *choice;
		if (items_[i].imageID) {
			choice = new Choice(category->T(items_[i].text), ImageID(items_[i].imageID));
		} else {
			choice = new Choice(category->T(items_[i].text));
		}
		parent->Add(choice);
		if (enabled_[i]) {
			choice->OnClick.Add([=](EventParams &p) {
				TriggerFinish(DR_OK);
				p.a = (uint32_t)i;
				OnChoice.Dispatch(p);
			});
		} else {
			choice->SetEnabled(false);
		}
	}

	AlignPopup(parent);
}

PopupCallbackScreen::PopupCallbackScreen(std::function<void(UI::ViewGroup *)> createViews, UI::View *sourceView) : AbstractContextMenuScreen(sourceView), createViews_(createViews) {
	if (sourceView) {
		SetPopupOrigin(sourceView);
	}
}

void PopupCallbackScreen::CreatePopupContents(ViewGroup *parent) {
	createViews_(parent);
	for (int i = 0; i < parent->GetNumSubviews(); i++) {
		parent->GetViewByIndex(i)->SetAutoResult(DialogResult::DR_OK);
	}
	AlignPopup(parent);
}

std::string ChopTitle(const std::string &title) {
	size_t pos = title.find('\n');
	if (pos != title.npos) {
		return title.substr(0, pos);
	}
	return title;
}

PopupMultiChoice::PopupMultiChoice(int *value, std::string_view text, const char **choices, int minVal, int numChoices,
	I18NCat category, ScreenManager *screenManager, UI::LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), choices_(choices), minVal_(minVal), numChoices_(numChoices), category_(category), screenManager_(screenManager) {
	if (choices) {
		// If choices is nullptr, we're being called from PopupMultiChoiceDynamic where value doesn't yet point to anything valid.
		if (*value >= numChoices + minVal)
			*value = numChoices + minVal - 1;
		if (*value < minVal)
			*value = minVal;
		UpdateText();
	}
	OnClick.Handle(this, &PopupMultiChoice::HandleClick);
}

void PopupMultiChoice::HandleClick(UI::EventParams &e) {
	if (!callbackExecuted_ && preOpenCallback_) {
		preOpenCallback_(this);
		callbackExecuted_ = true;
	}

	restoreFocus_ = HasFocus();

	auto category = GetI18NCategory(category_);

	std::vector<std::string> choices;
	for (int i = 0; i < numChoices_; i++) {
		choices.push_back(category ? std::string(category->T(choices_[i])) : std::string(choices_[i]));
	}

	ListPopupScreen *popupScreen = new ListPopupScreen(ChopTitle(text_), choices, *value_ - minVal_, [this](int num) {ChoiceCallback(num);});
	popupScreen->SetHiddenChoices(hidden_);
	popupScreen->SetChoiceIcons(icons_);
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
	screenManager_->push(popupScreen);
}

void PopupMultiChoice::Update() {
	UpdateText();
}

void PopupMultiChoice::UpdateText() {
	if (!choices_)
		return;
	int index = *value_ - minVal_;
	if (index < 0 || index >= numChoices_) {
		valueText_ = "(invalid choice)";  // Shouldn't happen. Should be no need to translate this.
	} else {
		if (choices_[index]) {
			valueText_ = T(category_, choices_[index]);
		} else {
			valueText_ = "";
		}
	}
}

void PopupMultiChoice::ChoiceCallback(int num) {
	if (num != -1) {
		_assert_(value_ != nullptr);

		*value_ = num + minVal_;
		UpdateText();

		UI::EventParams e{};
		e.v = this;
		e.a = num;
		e.b = PostChoiceCallback(num);
		OnChoice.Trigger(e);

		if (restoreFocus_) {
			SetFocusedView(this);
		}
	}
}

std::string PopupMultiChoice::ValueText() const {
	return valueText_;
}

PopupSliderChoice::PopupSliderChoice(int *value, int minValue, int maxValue, int defaultValue, std::string_view text, ScreenManager *screenManager, std::string_view units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(1), units_(units), screenManager_(screenManager) {
	fmt_ = "%d";
	OnClick.Handle(this, &PopupSliderChoice::HandleClick);
}

PopupSliderChoice::PopupSliderChoice(int *value, int minValue, int maxValue, int defaultValue, std::string_view text, int step, ScreenManager *screenManager, std::string_view units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(step), units_(units), screenManager_(screenManager) {
	fmt_ = "%d";
	OnClick.Handle(this, &PopupSliderChoice::HandleClick);
}

void PopupSliderChoice::SetFormat(std::string_view fmt) {
	fmt_ = fmt;
	if (units_.empty()) {
		if (startsWith(fmt_, "%d ")) {
			units_ = fmt_.substr(3);
		}
	}
}

PopupSliderChoiceFloat::PopupSliderChoiceFloat(float *value, float minValue, float maxValue, float defaultValue, std::string_view text, ScreenManager *screenManager, std::string_view units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(1.0f), units_(units), screenManager_(screenManager) {
	_dbg_assert_(maxValue > minValue);
	fmt_ = "%2.2f";
	OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
}

PopupSliderChoiceFloat::PopupSliderChoiceFloat(float *value, float minValue, float maxValue, float defaultValue, std::string_view text, float step, ScreenManager *screenManager, std::string_view units, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(step), units_(units), screenManager_(screenManager) {
	_dbg_assert_(step > 0.0f);
	_dbg_assert_(maxValue > minValue);
	fmt_ = "%2.2f";
	OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
}

void PopupSliderChoiceFloat::SetFormat(std::string_view fmt) {
	fmt_ = fmt;
	if (units_.empty()) {
		if (startsWith(fmt_, "%f ")) {
			units_ = fmt_.substr(3);
		}
	}
}

void PopupSliderChoice::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	SliderPopupScreen *popupScreen = new SliderPopupScreen(value_, minValue_, maxValue_, defaultValue_, ChopTitle(text_), step_, units_, liveUpdate_);
	if (!negativeLabel_.empty())
		popupScreen->SetNegativeDisable(negativeLabel_);
	popupScreen->RestrictChoices(fixedChoices_, numFixedChoices_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoice::HandleChange);
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
	screenManager_->push(popupScreen);
}

void PopupSliderChoice::HandleChange(EventParams &e) {
	e.v = this;
	OnChange.Trigger(e);

	if (restoreFocus_) {
		SetFocusedView(this);
	}
}

static bool IsValidNumberFormatString(std::string_view s) {
	if (s.empty())
		return false;
	size_t percentCount = 0;
	for (int i = 0; i < (int)s.size(); i++) {
		if (s[i] == '%') {
			if (i < s.size() - 1) {
				if (s[i + 1] == 's')
					return false;
				if (s[i + 1] == '%') {
					// Next is another % sign, so it's an escape to emit a % sign, which is fine.
					i++;
					continue;
				}
			}
			percentCount++;
		}
	}
	return percentCount == 1;
}

std::string PopupSliderChoice::ValueText() const {
	// Always good to have space for Unicode.
	char temp[256];
	temp[0] = '\0';
	if (zeroLabel_.size() && *value_ == 0) {
		truncate_cpy(temp, zeroLabel_);
	} else if (negativeLabel_.size() && *value_ < 0) {
		truncate_cpy(temp, negativeLabel_);
	} else {
		// Would normally be dangerous to have user-controlled format strings!
		// However, let's check that there's only one % sign, and that it's not followed by an S.
		// Also, these strings are from translations, which are kinda-fixed (though can be modified in theory).
		if (IsValidNumberFormatString(fmt_)) {
			snprintf(temp, sizeof(temp), fmt_.c_str(), *value_);
		} else {
			truncate_cpy(temp, "(translation error)");
		}
	}
	return std::string(temp);
}

void PopupSliderChoiceFloat::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	SliderFloatPopupScreen *popupScreen = new SliderFloatPopupScreen(value_, minValue_, maxValue_, defaultValue_, ChopTitle(text_), step_, units_, liveUpdate_);
	popupScreen->OnChange.Handle(this, &PopupSliderChoiceFloat::HandleChange);
	popupScreen->SetHasDropShadow(hasDropShadow_);
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
	screenManager_->push(popupScreen);
}

void PopupSliderChoiceFloat::HandleChange(EventParams &e) {
	e.v = this;
	OnChange.Trigger(e);

	if (restoreFocus_) {
		SetFocusedView(this);
	}
}

std::string PopupSliderChoiceFloat::ValueText() const {
	char temp[256];
	temp[0] = '\0';
	if (zeroLabel_.size() && *value_ == 0.0f) {
		truncate_cpy(temp, zeroLabel_);
	} else if (IsValidNumberFormatString(fmt_)) {
		snprintf(temp, sizeof(temp), fmt_.c_str(), *value_);
	} else {
		snprintf(temp, sizeof(temp), "%0.2f", *value_);
	}
	return temp;
}

void SliderPopupScreen::OnDecrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ -= step_;
	slider_->Clamp();
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	disabled_ = false;
}

void SliderPopupScreen::OnIncrease(EventParams &params) {
	if (sliderValue_ > minValue_ && sliderValue_ < maxValue_) {
		sliderValue_ = step_ * floor((sliderValue_ / step_) + 0.5f);
	}
	sliderValue_ += step_;
	slider_->Clamp();
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	disabled_ = false;
}

void SliderPopupScreen::OnSliderChange(EventParams &params) {
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	disabled_ = false;
}

void SliderPopupScreen::OnTextChange(EventParams &params) {
	if (!changing_) {
		sliderValue_ = atoi(edit_->GetText().c_str());
		disabled_ = false;
		slider_->Clamp();
	}
}

void SliderPopupScreen::UpdateTextBox() {
	char temp[128];
	snprintf(temp, sizeof(temp), "%d", sliderValue_);
	edit_->SetText(temp);
	if (liveUpdate_ && *value_ != sliderValue_) {
		*value_ = sliderValue_;
		EventParams e{};
		e.v = nullptr;
		e.a = *value_;
		OnChange.Trigger(e);
	}
}

void SliderPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();

	sliderValue_ = *value_;
	if (disabled_ && sliderValue_ < 0)
		sliderValue_ = 0;

	LinearLayout *vert = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(UI::Margins(10, 10))));
	slider_ = new Slider(&sliderValue_, minValue_, maxValue_, 1, new LinearLayoutParams(UI::Margins(10, 10)));
	slider_->OnChange.Handle(this, &SliderPopupScreen::OnSliderChange);
	slider_->RestrictChoices(fixedChoices_, numFixedChoices_);
	vert->Add(slider_);

	LinearLayout *lin = vert->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::Margins(10, 10))));
	lin->Add(new Button(" - "))->OnClick.Handle(this, &SliderPopupScreen::OnDecrease);
	lin->Add(new Button(" + "))->OnClick.Handle(this, &SliderPopupScreen::OnIncrease);

	edit_ = new TextEdit("", Title(), "", new LinearLayoutParams(1.0f));
	edit_->SetMaxLen(16);
	edit_->OnTextChange.Handle(this, &SliderPopupScreen::OnTextChange);
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	lin->Add(edit_);

	if (!units_.empty())
		lin->Add(new TextView(units_));

	if (defaultValue_ != NO_DEFAULT_FLOAT) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		lin->Add(new Button(di->T("Reset")))->OnClick.Add([=](UI::EventParams &) {
			sliderValue_ = defaultValue_;
			changing_ = true;
			UpdateTextBox();
			changing_ = false;
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
	edit_->SetTextColor(dc.GetTheme().itemStyle.fgColor);
	edit_->SetTextAlign(FLAG_DYNAMIC_ASCII);
	edit_->OnTextChange.Handle(this, &SliderFloatPopupScreen::OnTextChange);
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	lin->Add(edit_);
	if (!units_.empty())
		lin->Add(new TextView(units_))->SetTextColor(dc.GetTheme().itemStyle.fgColor);

	if (defaultValue_ != NO_DEFAULT_FLOAT) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		lin->Add(new Button(di->T("Reset")))->OnClick.Add([=](UI::EventParams &) {
			sliderValue_ = defaultValue_;
			if (liveUpdate_) {
				*value_ = defaultValue_;
			}
		});
	}

	// slider_ = parent->Add(new SliderFloat(&sliderValue_, minValue_, maxValue_, new LinearLayoutParams(UI::Margins(10, 5))));
	if (IsFocusMovementEnabled())
		UI::SetFocusedView(slider_);
}

void SliderFloatPopupScreen::OnDecrease(EventParams &params) {
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
}

void SliderFloatPopupScreen::OnIncrease(EventParams &params) {
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
}

void SliderFloatPopupScreen::OnSliderChange(EventParams &params) {
	changing_ = true;
	UpdateTextBox();
	changing_ = false;
	if (liveUpdate_) {
		*value_ = sliderValue_;
	}
}

void SliderFloatPopupScreen::UpdateTextBox() {
	char temp[128];
	snprintf(temp, sizeof(temp), "%0.3f", sliderValue_);
	edit_->SetText(temp);
}

void SliderFloatPopupScreen::OnTextChange(EventParams &params) {
	if (!changing_) {
		sliderValue_ = atof(edit_->GetText().c_str());
		slider_->Clamp();
		if (liveUpdate_) {
			*value_ = sliderValue_;
		}
	}
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

PopupTextInputChoice::PopupTextInputChoice(RequesterToken token, std::string *value, std::string_view title, std::string_view placeholder, int maxLen, ScreenManager *screenManager, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(title, layoutParams), screenManager_(screenManager), value_(value), placeHolder_(placeholder), maxLen_(maxLen), token_(token), restriction_(StringRestriction::None) {
	OnClick.Handle(this, &PopupTextInputChoice::HandleClick);
}

void PopupTextInputChoice::HandleClick(EventParams &e) {
	restoreFocus_ = HasFocus();

	// Choose method depending on platform capabilities.
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		System_InputBoxGetString(token_, text_, *value_, passwordMasking_, [this](const std::string &enteredValue, int) {
			*value_ = SanitizeString(StripSpaces(enteredValue), restriction_, minLen_, maxLen_);
			EventParams params{};
			params.v = this;
			OnChange.Trigger(params);
		});
		return;
	}

	TextEditPopupScreen *popupScreen = new TextEditPopupScreen(value_, placeHolder_, ChopTitle(text_), maxLen_);
	popupScreen->SetPasswordMasking(passwordMasking_);
	if (System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT)) {
		popupScreen->SetAlignTop(true);
	}
	popupScreen->OnChange.Add([this](EventParams &e) {
		*value_ = StripSpaces(SanitizeString(*value_, restriction_, minLen_, maxLen_));
		EventParams params{};
		params.v = this;
		OnChange.Trigger(params);
		if (restoreFocus_) {
			SetFocusedView(this);
		}
	});
	if (e.v)
		popupScreen->SetPopupOrigin(e.v);
	screenManager_->push(popupScreen);
}

std::string PopupTextInputChoice::ValueText() const {
	return *value_;
}

void TextEditPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();

	textEditValue_ = *value_;
	LinearLayout *lin = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams((UI::Size)300, WRAP_CONTENT)));
	edit_ = new TextEdit(textEditValue_, Title(), placeholder_, new LinearLayoutParams(1.0f));
	edit_->SetMaxLen(maxLen_);
	edit_->SetTextColor(dc.GetTheme().popupStyle.fgColor);
	edit_->SetPasswordMasking(passwordMasking_);
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

	float valueW, valueH;
	dc.MeasureTextRect(dc.GetTheme().uiFont, scale, scale, valueText, availWidth, &valueW, &valueH, ALIGN_RIGHT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
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
	Style style = dc.GetTheme().itemStyle;
	if (!IsEnabled()) {
		style = dc.GetTheme().itemDisabledStyle;
	}
	if (HasFocus()) {
		style = dc.GetTheme().itemFocusedStyle;
	}
	if (down_) {
		style = dc.GetTheme().itemDownStyle;
	}
	int paddingX = 12;
	dc.SetFontStyle(dc.GetTheme().uiFont);

	std::string valueText = ValueText();

	if (passwordMasking_) {
		// Replace all characters with stars.
		memset(&valueText[0], '*', valueText.size());
	}

	// If there is a label, assume we want at least 20% of the size for it, at a minimum.

	if (!text_.empty() && !hideTitle_) {
		float availWidth = (bounds_.w - paddingX * 2) * 0.8f;
		float scale = CalculateValueScale(dc, valueText, availWidth);

		float w, h;
		dc.MeasureTextRect(dc.GetTheme().uiFont, scale, scale, valueText, availWidth, &w, &h, ALIGN_RIGHT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
		textPadding_.right = w + paddingX;

		Choice::Draw(dc);
		int imagePadding = 0;
		if (rightIconImage_.isValid()) {
			imagePadding = bounds_.h;
		}
		dc.SetFontScale(scale, scale);
		Bounds valueBounds(bounds_.x2() - textPadding_.right - imagePadding, bounds_.y, w, bounds_.h);
		dc.DrawTextRect(valueText, valueBounds, style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
		dc.SetFontScale(1.0f, 1.0f);
	} else {
		Choice::Draw(dc);

		if (iconOnly_) {
			// In this case we only display the image of the choice. Useful for small buttons spawning a popup.
			dc.Draw()->DrawImageRotated(ValueImage(), bounds_.centerX(), bounds_.centerY(), imgScale_, imgRot_, style.fgColor, imgFlipH_);
			return;
		}

		float scale = CalculateValueScale(dc, valueText, bounds_.w);
		dc.SetFontScale(scale, scale);
		dc.DrawTextRect(valueText, bounds_.Expand(-paddingX, 0.0f), style.fgColor, ALIGN_LEFT | ALIGN_VCENTER | FLAG_WRAP_TEXT);
		dc.SetFontScale(1.0f, 1.0f);
	}
}

float AbstractChoiceWithValueDisplay::CalculateValueScale(const UIContext &dc, std::string_view valueText, float availWidth) const {
	float actualWidth, actualHeight;
	dc.MeasureTextRect(dc.GetTheme().uiFont, 1.0f, 1.0f, valueText, availWidth, &actualWidth, &actualHeight);
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

FileChooserChoice::FileChooserChoice(RequesterToken token, std::string *value, std::string_view text, BrowseFileType fileType, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value) {
	OnClick.Add([=](UI::EventParams &) {
		System_BrowseForFile(token, text_, fileType, [=](const std::string &returnValue, int) {
			if (*value_ != returnValue) {
				*value = returnValue;
				UI::EventParams e{};
				e.s = *value;
				OnChange.Trigger(e);
			}
		});
	});
}

std::string FileChooserChoice::ValueText() const {
	if (value_->empty()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		return std::string(di->T("Default"));
	}
	Path path(*value_);
	return path.GetFilename();
}

FolderChooserChoice::FolderChooserChoice(RequesterToken token, std::string *value, std::string_view text, LayoutParams *layoutParams)
	: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), token_(token) {
	OnClick.Add([=](UI::EventParams &) {
		System_BrowseForFolder(token_, text_, Path(*value), [=](const std::string &returnValue, int) {
			if (*value_ != returnValue) {
				*value = returnValue;
				UI::EventParams e{};
				e.s = *value;
				OnChange.Trigger(e);
			}
		});
	});
}

std::string FolderChooserChoice::ValueText() const {
	if (value_->empty()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		return std::string(di->T("Default"));
	}
	Path path(*value_);
	return path.ToVisualString();
}

}  // namespace
