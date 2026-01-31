#pragma once

#include "Common/System/Request.h"

#include "Common/Data/Text/I18n.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/Notice.h"

// from StringUtils
enum class StringRestriction;
enum class OSDType;  // From OSD

namespace UI {

constexpr float NO_DEFAULT_FLOAT = -1000000.0f;
constexpr int NO_DEFAULT_INT = -1000000;

class PopupScreen : public UIDialogScreen {
public:
	PopupScreen(std::string_view title, std::string_view button1 = "", std::string_view button2 = "");

	virtual void CreatePopupContents(UI::ViewGroup *parent) = 0;
	void CreateViews() override;
	bool isTransparent() const override { return true; }
	void touch(const TouchInput &touch) override;
	bool key(const KeyInput &key) override;

	void TriggerFinish(DialogResult result) override;

	void SetPopupOrigin(const UI::View *view);
	void SetPopupOffset(float y) { offsetY_ = y; }

	void SetAlignTop(bool alignTop) { alignTop_ = alignTop; }

	void SetHasDropShadow(bool has) { hasDropShadow_ = has; }

	// For the postproc param sliders on DisplayLayoutScreen
	bool wantBrightBackground() const override { return !hasDropShadow_; }
	void SetNotification(NoticeLevel noticeLevel, std::string_view str) {
		notificationLevel_ = noticeLevel;
		notificationString_ = str;
	}

protected:
	virtual bool FillVertical() const { return false; }
	virtual UI::Size PopupWidth() const { return 550; }
	virtual bool ShowButtons() const { return true; }
	virtual bool CanComplete(DialogResult result) { return true; }
	virtual void OnCompleted(DialogResult result) {}
	std::string_view Title() { return title_; }

	void update() override;

private:
	UI::LinearLayout *box_ = nullptr;
	UI::Choice *defaultButton_ = nullptr;
	ImageID button1Image_;
	std::string title_;
	std::string button1_;
	std::string button2_;

	enum {
		FRAMES_LEAD_IN = 6,
		FRAMES_LEAD_OUT = 4,
	};

	int frames_ = 0;
	int finishFrame_ = -1;
	DialogResult finishResult_ = DR_CANCEL;
	bool hasPopupOrigin_ = false;
	Point2D popupOrigin_;
	float offsetY_ = 0.0f;
	bool alignTop_ = false;

	bool hasDropShadow_ = true;
	NoticeLevel notificationLevel_{};
	std::string notificationString_;
};

class ListPopupScreen : public PopupScreen {
public:
	ListPopupScreen(std::string_view title) : PopupScreen(title) {}
	ListPopupScreen(std::string_view title, const std::vector<std::string> &items, int selected, std::function<void(int)> callback, bool showButtons = false)
		: PopupScreen(title, "OK", "Cancel"), adaptor_(items, selected), callback_(callback), showButtons_(showButtons) {
	}
	ListPopupScreen(std::string_view title, const std::vector<std::string> &items, int selected, bool showButtons = false)
		: PopupScreen(title, "OK", "Cancel"), adaptor_(items, selected), showButtons_(showButtons) {
	}
	~ListPopupScreen() override;

	int GetChoice() const {
		return listView_->GetSelected();
	}
	std::string GetChoiceString() const {
		return adaptor_.GetTitle(listView_->GetSelected());
	}
	void SetHiddenChoices(const std::set<int> &hidden) {
		hidden_ = hidden;
	}
	void SetChoiceIcons(const std::map<int, ImageID> &icons) {
		icons_ = icons;
	}
	const char *tag() const override { return "listpopup"; }

	UI::Event OnChoice;

protected:
	bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return showButtons_; }
	void CreatePopupContents(UI::ViewGroup *parent) override;
	UI::StringVectorListAdaptor adaptor_;
	UI::ListView *listView_ = nullptr;

private:
	void OnListChoice(UI::EventParams &e);

	std::function<void(int)> callback_;
	bool showButtons_ = false;
	std::set<int> hidden_;
	std::map<int, ImageID> icons_;
};

class MessagePopupScreen : public PopupScreen {
public:
	MessagePopupScreen(std::string_view title, std::string_view message, std::string_view button1, std::string_view button2, std::function<void(bool)> callback)
		: PopupScreen(title, button1, button2), message_(message), callback_(callback) {}

	const char *tag() const override { return "MessagePopupScreen"; }

	UI::Event OnChoice;

protected:
	bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return true; }
	void CreatePopupContents(UI::ViewGroup *parent) override;

private:
	void OnCompleted(DialogResult result) override;
	std::string message_;
	std::function<void(bool)> callback_;
};

class SliderPopupScreen : public PopupScreen {
public:
	SliderPopupScreen(int *value, int minValue, int maxValue, int defaultValue, std::string_view title, int step, std::string_view units, bool liveUpdate)
		: PopupScreen(title, "OK", "Cancel"), units_(units), value_(value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(step), liveUpdate_(liveUpdate) {}
	void CreatePopupContents(ViewGroup *parent) override;

	void SetNegativeDisable(const std::string &str) {
		negativeLabel_ = str;
		disabled_ = *value_ < 0;
	}
	void RestrictChoices(const int *fixedChoices, size_t numFixedChoices) {
		fixedChoices_ = fixedChoices;
		numFixedChoices_ = numFixedChoices;
	}

	const char *tag() const override { return "SliderPopup"; }

	Event OnChange;

private:
	void OnDecrease(EventParams &params);
	void OnIncrease(EventParams &params);
	void OnTextChange(EventParams &params);
	void OnSliderChange(EventParams &params);
	void OnCompleted(DialogResult result) override;
	void UpdateTextBox();
	Slider *slider_ = nullptr;
	UI::TextEdit *edit_ = nullptr;
	std::string units_;
	std::string negativeLabel_;
	int *value_;
	int sliderValue_ = 0;
	int minValue_;
	int maxValue_;
	int defaultValue_;
	int step_;
	bool liveUpdate_;
	bool changing_ = false;
	bool disabled_ = false;
	const int *fixedChoices_ = nullptr;
	size_t numFixedChoices_ = 0;
};

class SliderFloatPopupScreen : public PopupScreen {
public:
	SliderFloatPopupScreen(float *value, float minValue, float maxValue, float defaultValue, std::string_view title, float step = 1.0f, std::string_view units = "", bool liveUpdate = false)
		: PopupScreen(title, "OK", "Cancel"), units_(units), value_(value), originalValue_(*value), minValue_(minValue), maxValue_(maxValue), defaultValue_(defaultValue), step_(step), liveUpdate_(liveUpdate) {}
	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "SliderFloatPopup"; }

	Event OnChange;

private:
	void OnIncrease(EventParams &params);
	void OnDecrease(EventParams &params);
	void OnTextChange(EventParams &params);
	void OnSliderChange(EventParams &params);
	void OnCompleted(DialogResult result) override;
	void UpdateTextBox();
	UI::SliderFloat *slider_ = nullptr;
	UI::TextEdit *edit_ = nullptr;
	std::string units_;
	float sliderValue_ = 0.0f;
	float originalValue_ = 0.0f;
	float *value_;
	float minValue_;
	float maxValue_;
	float defaultValue_;
	float step_;
	bool changing_ = false;
	bool liveUpdate_;
};

class TextEditPopupScreen : public PopupScreen {
public:
	TextEditPopupScreen(std::string *value, std::string_view placeholder, std::string_view title, int maxLen)
		: PopupScreen(title, "OK", "Cancel"), value_(value), placeholder_(placeholder), maxLen_(maxLen) {}
	void CreatePopupContents(ViewGroup *parent) override;

	const char *tag() const override { return "TextEditPopup"; }

	void SetPasswordMasking(bool masking) {
		passwordMasking_ = masking;
	}

	Event OnChange;

private:
	void OnCompleted(DialogResult result) override;
	TextEdit *edit_ = nullptr;
	std::string *value_;
	std::string textEditValue_;
	std::string placeholder_;
	int maxLen_;
	bool passwordMasking_ = false;
};

struct ContextMenuItem {
	const char *text;
	const char *imageID;
};

class AbstractContextMenuScreen : public PopupScreen {
public:
	AbstractContextMenuScreen(UI::View *sourceView) : PopupScreen("", "", ""), sourceView_(sourceView) {}
protected:
	UI::Size PopupWidth() const override {
		return 350;
	}
	UI::View *sourceView_;
	void AlignPopup(UI::View *parent);
};

// Once a selection has been made,
class PopupContextMenuScreen : public AbstractContextMenuScreen {
public:
	PopupContextMenuScreen(const ContextMenuItem *items, size_t itemCount, I18NCat category, UI::View *sourceView);
	const char *tag() const override { return "ContextMenuPopup"; }

	void SetEnabled(size_t index, bool enabled) {
		enabled_[index] = enabled;
	}

	UI::Event OnChoice;

private:
	void CreatePopupContents(ViewGroup *parent) override;
	const ContextMenuItem *items_;
	size_t itemCount_;
	I18NCat category_;
	std::vector<bool> enabled_;
};

class PopupCallbackScreen : public AbstractContextMenuScreen {
public:
	PopupCallbackScreen(std::function<void(UI::ViewGroup *)> createViews, UI::View *sourceView);
	const char *tag() const override { return "ContextMenuCallbackPopup"; }

private:
	void CreatePopupContents(ViewGroup *parent) override;
	std::function<void(UI::ViewGroup *)> createViews_;
};

// Reads and writes value to determine the current selection.
class PopupMultiChoice : public AbstractChoiceWithValueDisplay {
public:
	PopupMultiChoice(int *value, std::string_view text, const char **choices, int minVal, int numChoices,
		I18NCat category, ScreenManager *screenManager, UI::LayoutParams *layoutParams = nullptr);

	void Update() override;

	void HideChoice(int c) {
		hidden_.insert(c);
	}
	bool IsChoiceHidden(int c) const {
		return hidden_.find(c) != hidden_.end();
	}

	void SetPreOpenCallback(std::function<void(PopupMultiChoice *)> callback) {
		preOpenCallback_ = callback;
	}
	void SetChoiceIcon(int c, ImageID id) {
		icons_[c] = id;
	}
	void SetChoiceIcons(std::map<int, ImageID> icons) {
		icons_ = icons;
	}

	UI::Event OnChoice;

protected:
	std::string ValueText() const override;
	ImageID ValueImage() const override {
		auto iter = icons_.find(*value_);
		if (iter != icons_.end()) {
			return iter->second;
		}
		return ImageID::invalid();
	}

	int *value_;
	const char **choices_;
	int minVal_;
	int numChoices_;
	void UpdateText();

private:
	void HandleClick(UI::EventParams &e);

	void ChoiceCallback(int num);
	virtual bool PostChoiceCallback(int num) { return true; }

	I18NCat category_;
	ScreenManager *screenManager_;
	std::string valueText_;
	bool restoreFocus_ = false;
	std::set<int> hidden_;
	std::map<int, ImageID> icons_;

	std::function<void(PopupMultiChoice *)> preOpenCallback_;
	bool callbackExecuted_ = false;
};

// Allows passing in a dynamic vector of strings. Saves the string.
class PopupMultiChoiceDynamic : public PopupMultiChoice {
public:
	// TODO: This all is absolutely terrible, just done this way to be conformant with the internals of PopupMultiChoice.
	PopupMultiChoiceDynamic(std::string *value, std::string_view text, const std::vector<std::string> &choices,
		I18NCat category, ScreenManager *screenManager, std::vector<std::string> *values = nullptr, UI::LayoutParams *layoutParams = nullptr)
		: UI::PopupMultiChoice(&valueInt_, text, nullptr, 0, (int)choices.size(), category, screenManager, layoutParams), valueStr_(value) {
		if (values) {
			_dbg_assert_(choices.size() == values->size());
		}
		choices_ = new const char *[numChoices_];
		valueInt_ = 0;
		for (int i = 0; i < numChoices_; i++) {
			choices_[i] = new char[choices[i].size() + 1];
			memcpy((char *)choices_[i], choices[i].c_str(), choices[i].size() + 1);
			if (values) {
				if (*value == (*values)[i])
					valueInt_ = i;
			}
			if (*value == choices_[i])
				valueInt_ = i;
		}
		value_ = &valueInt_;
		if (values) {
			choiceValues_ = *values;
		}
		UpdateText();
	}
	~PopupMultiChoiceDynamic() {
		for (int i = 0; i < numChoices_; i++) {
			delete[] choices_[i];
		}
		delete[] choices_;
	}

protected:
	bool PostChoiceCallback(int num) override {
		if (!valueStr_) {
			return true;
		}
		const char *value = choices_[num];
		if (choiceValues_.size() == numChoices_) {
			value = choiceValues_[num].c_str();
		}

		if (*valueStr_ != value) {
			*valueStr_ = value;
			return true;
		} else {
			return false;
		}
	}

private:
	int valueInt_;
	std::string *valueStr_;
	std::vector<std::string> choiceValues_;
};

class PopupSliderChoice : public AbstractChoiceWithValueDisplay {
public:
	PopupSliderChoice(int *value, int minValue, int maxValue, int defaultValue, std::string_view text, ScreenManager *screenManager, std::string_view units = "", LayoutParams *layoutParams = 0);
	PopupSliderChoice(int *value, int minValue, int maxValue, int defaultValue, std::string_view text, int step, ScreenManager *screenManager, std::string_view units = "", LayoutParams *layoutParams = 0);

	void SetFormat(std::string_view fmt);
	void SetZeroLabel(std::string_view str) {
		zeroLabel_ = str;
	}
	void SetLiveUpdate(bool update) {
		liveUpdate_ = update;
	}
	void SetNegativeDisable(std::string_view str) {
		negativeLabel_ = str;
	}
	void RestrictChoices(const int *fixedChoices, size_t numFixedChoices) {
		fixedChoices_ = fixedChoices;
		numFixedChoices_ = numFixedChoices;
	}

	Event OnChange;

protected:
	std::string ValueText() const override;

private:
	void HandleClick(EventParams &e);
	void HandleChange(EventParams &e);

	int *value_;
	int minValue_;
	int maxValue_;
	int defaultValue_;
	int step_;
	std::string fmt_;
	std::string zeroLabel_;
	std::string negativeLabel_;
	std::string units_;
	ScreenManager *screenManager_;
	bool restoreFocus_ = false;
	bool liveUpdate_ = false;
	const int *fixedChoices_ = nullptr;
	size_t numFixedChoices_ = 0;
};

class PopupSliderChoiceFloat : public AbstractChoiceWithValueDisplay {
public:
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, float defaultValue, std::string_view text, ScreenManager *screenManager, std::string_view units = "", LayoutParams *layoutParams = 0);
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, float defaultValue, std::string_view text, float step, ScreenManager *screenManager, std::string_view units = "", LayoutParams *layoutParams = 0);

	void SetFormat(std::string_view fmt);
	void SetZeroLabel(const std::string &str) {
		zeroLabel_ = str;
	}
	void SetLiveUpdate(bool update) {
		liveUpdate_ = update;
	}
	void SetHasDropShadow(bool has) {
		hasDropShadow_ = has;
	}

	Event OnChange;

protected:
	std::string ValueText() const override;

private:
	void HandleClick(EventParams &e);
	void HandleChange(EventParams &e);
	float *value_;
	float minValue_;
	float maxValue_;
	float defaultValue_;
	float step_;
	std::string fmt_;
	std::string zeroLabel_;
	std::string units_;
	ScreenManager *screenManager_;
	bool restoreFocus_ = false;
	bool liveUpdate_ = false;
	bool hasDropShadow_ = true;
};

// NOTE: This one will defer to a system-native dialog if possible.
class PopupTextInputChoice : public AbstractChoiceWithValueDisplay {
public:
	PopupTextInputChoice(RequesterToken token, std::string *value, std::string_view title, std::string_view placeholder, int maxLen, ScreenManager *screenManager, LayoutParams *layoutParams = 0);

	Event OnChange;

	void SetRestriction(StringRestriction restriction, int minLength) {
		restriction_ = restriction;
		minLen_ = minLength;
	}

protected:
	std::string ValueText() const override;

private:
	void HandleClick(EventParams &e);
	RequesterToken token_;
	ScreenManager *screenManager_;
	std::string *value_;
	std::string placeHolder_;
	std::string defaultText_;
	int maxLen_;
	int minLen_ = 0;
	bool restoreFocus_ = false;
	StringRestriction restriction_;
};

class ChoiceWithValueDisplay : public AbstractChoiceWithValueDisplay {
public:
	ChoiceWithValueDisplay(int *value, std::string_view text, LayoutParams *layoutParams = 0)
		: AbstractChoiceWithValueDisplay(text, layoutParams), iValue_(value) {}
	ChoiceWithValueDisplay(int *value, ImageID imageId, LayoutParams *layoutParams = 0)
		: AbstractChoiceWithValueDisplay("", imageId, layoutParams), iValue_(value) {}
	ChoiceWithValueDisplay(std::string *value, std::string_view text, I18NCat category, LayoutParams *layoutParams = 0)
		: AbstractChoiceWithValueDisplay(text, layoutParams), sValue_(value), category_(category) {}
	ChoiceWithValueDisplay(std::string *value, std::string_view text, std::string(*translateCallback)(std::string_view value), LayoutParams *layoutParams = 0)
		: AbstractChoiceWithValueDisplay(text, layoutParams), sValue_(value), translateCallback_(translateCallback) {}

private:
	std::string ValueText() const override;

	std::string *sValue_ = nullptr;
	int *iValue_ = nullptr;
	I18NCat category_ = I18NCat::CATEGORY_COUNT;
	std::string(*translateCallback_)(std::string_view value) = nullptr;
};

enum class FileChooserFileType {
	WAVE_FILE,
};

class FileChooserChoice : public AbstractChoiceWithValueDisplay {
public:
	FileChooserChoice(RequesterToken token, std::string *value, std::string_view title, BrowseFileType fileType, LayoutParams *layoutParams = nullptr);
	std::string ValueText() const override;

	Event OnChange;

private:
	std::string *value_;
};

class FolderChooserChoice : public AbstractChoiceWithValueDisplay {
public:
	FolderChooserChoice(RequesterToken token, std::string *value, std::string_view title, LayoutParams *layoutParams = nullptr);
	std::string ValueText() const override;

	Event OnChange;

private:
	std::string *value_;
	RequesterToken token_;
};

}  // namespace UI
