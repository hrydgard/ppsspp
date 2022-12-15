#pragma once

#include <set>

#include "Common/Math/lin/vec3.h"
#include "Common/UI/Screen.h"
#include "Common/UI/ViewGroup.h"

using namespace Lin;

class I18NCategory;
namespace Draw {
	class DrawContext;
}

class UIScreen : public Screen {
public:
	UIScreen();
	~UIScreen();

	void update() override;
	void preRender() override;
	void render() override;
	void postRender() override;
	void deviceLost() override;
	void deviceRestored() override;

	bool touch(const TouchInput &touch) override;
	bool key(const KeyInput &touch) override;
	bool axis(const AxisInput &touch) override;

	TouchInput transformTouch(const TouchInput &touch) override;

	virtual void TriggerFinish(DialogResult result);

	// Some useful default event handlers
	UI::EventReturn OnOK(UI::EventParams &e);
	UI::EventReturn OnCancel(UI::EventParams &e);
	UI::EventReturn OnBack(UI::EventParams &e);

protected:
	virtual void CreateViews() = 0;
	virtual void DrawBackground(UIContext &dc) {}

	void RecreateViews() override { recreateViews_ = true; }
	bool UseVerticalLayout() const;

	UI::ViewGroup *root_ = nullptr;
	Vec3 translation_ = Vec3(0.0f);
	Vec3 scale_ = Vec3(1.0f);
	float alpha_ = 1.0f;
	bool ignoreInsets_ = false;

private:
	void DoRecreateViews();

	bool recreateViews_ = true;
	bool lastVertical_;
};

class UIDialogScreen : public UIScreen {
public:
	UIDialogScreen() : UIScreen(), finished_(false) {}
	bool key(const KeyInput &key) override;
	void sendMessage(const char *msg, const char *value) override;

private:
	bool finished_;
};


class PopupScreen : public UIDialogScreen {
public:
	PopupScreen(std::string title, std::string button1 = "", std::string button2 = "");

	virtual void CreatePopupContents(UI::ViewGroup *parent) = 0;
	void CreateViews() override;
	bool isTransparent() const override { return true; }
	bool touch(const TouchInput &touch) override;
	bool key(const KeyInput &key) override;

	void TriggerFinish(DialogResult result) override;

	void SetPopupOrigin(const UI::View *view);
	void SetPopupOffset(float y);

	void SetHasDropShadow(bool has) { hasDropShadow_ = has; }

protected:
	virtual bool FillVertical() const { return false; }
	virtual UI::Size PopupWidth() const { return 550; }
	virtual bool ShowButtons() const { return true; }
	virtual bool CanComplete(DialogResult result) { return true; }
	virtual void OnCompleted(DialogResult result) {}
	virtual bool HasTitleBar() const { return true; }
	const std::string &Title() { return title_; }

	void update() override;

private:
	UI::ViewGroup *box_;
	UI::Button *defaultButton_;
	std::string title_;
	std::string button1_;
	std::string button2_;

	enum {
		FRAMES_LEAD_IN = 6,
		FRAMES_LEAD_OUT = 4,
	};

	int frames_ = 0;
	int finishFrame_ = -1;
	DialogResult finishResult_;
	bool hasPopupOrigin_ = false;
	Point popupOrigin_;
	float offsetY_ = 0.0f;

	bool hasDropShadow_ = true;
};

class ListPopupScreen : public PopupScreen {
public:
	ListPopupScreen(std::string title) : PopupScreen(title) {}
	ListPopupScreen(std::string title, const std::vector<std::string> &items, int selected, std::function<void(int)> callback, bool showButtons = false)
		: PopupScreen(title, "OK", "Cancel"), adaptor_(items, selected), callback_(callback), showButtons_(showButtons) {
	}
	ListPopupScreen(std::string title, const std::vector<std::string> &items, int selected, bool showButtons = false)
		: PopupScreen(title, "OK", "Cancel"), adaptor_(items, selected), showButtons_(showButtons) {
	}

	int GetChoice() const {
		return listView_->GetSelected();
	}
	std::string GetChoiceString() const {
		return adaptor_.GetTitle(listView_->GetSelected());
	}
	void SetHiddenChoices(std::set<int> hidden) {
		hidden_ = hidden;
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
	UI::EventReturn OnListChoice(UI::EventParams &e);

	std::function<void(int)> callback_;
	bool showButtons_ = false;
	std::set<int> hidden_;
};

class MessagePopupScreen : public PopupScreen {
public:
	MessagePopupScreen(std::string title, std::string message, std::string button1, std::string button2, std::function<void(bool)> callback) 
		: PopupScreen(title, button1, button2), message_(message), callback_(callback) {}
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

// TODO: Need a way to translate OK and Cancel

namespace UI {

class SliderPopupScreen : public PopupScreen {
public:
	SliderPopupScreen(int *value, int minValue, int maxValue, const std::string &title, int step = 1, const std::string &units = "")
		: PopupScreen(title, "OK", "Cancel"), units_(units), value_(value), minValue_(minValue), maxValue_(maxValue), step_(step) {}
	void CreatePopupContents(ViewGroup *parent) override;

	void SetNegativeDisable(const std::string &str) {
		negativeLabel_ = str;
		disabled_ = *value_ < 0;
	}

	const char *tag() const override { return "SliderPopup"; }

	Event OnChange;

private:
	EventReturn OnDecrease(EventParams &params);
	EventReturn OnIncrease(EventParams &params);
	EventReturn OnTextChange(EventParams &params);
	EventReturn OnSliderChange(EventParams &params);
	void OnCompleted(DialogResult result) override;
	Slider *slider_ = nullptr;
	UI::TextEdit *edit_ = nullptr;
	std::string units_;
	std::string negativeLabel_;
	int *value_;
	int sliderValue_ = 0;
	int minValue_;
	int maxValue_;
	int step_;
	bool changing_ = false;
	bool disabled_ = false;
};

class SliderFloatPopupScreen : public PopupScreen {
public:
	SliderFloatPopupScreen(float *value, float minValue, float maxValue, const std::string &title, float step = 1.0f, const std::string &units = "", bool liveUpdate = false)
	: PopupScreen(title, "OK", "Cancel"), units_(units), value_(value), originalValue_(*value), minValue_(minValue), maxValue_(maxValue), step_(step), changing_(false), liveUpdate_(liveUpdate) {}
	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "SliderFloatPopup"; }

	Event OnChange;

private:
	EventReturn OnIncrease(EventParams &params);
	EventReturn OnDecrease(EventParams &params);
	EventReturn OnTextChange(EventParams &params);
	EventReturn OnSliderChange(EventParams &params);
	void OnCompleted(DialogResult result) override;
	UI::SliderFloat *slider_;
	UI::TextEdit *edit_;
	std::string units_;
	float sliderValue_;
	float originalValue_;
	float *value_;
	float minValue_;
	float maxValue_;
	float step_;
	bool changing_;
	bool liveUpdate_;
};

class TextEditPopupScreen : public PopupScreen {
public:
	TextEditPopupScreen(std::string *value, const std::string &placeholder, const std::string &title, int maxLen)
		: PopupScreen(title, "OK", "Cancel"), value_(value), placeholder_(placeholder), maxLen_(maxLen) {}
	void CreatePopupContents(ViewGroup *parent) override;

	const char *tag() const override { return "TextEditPopup"; }

	Event OnChange;

private:
	void OnCompleted(DialogResult result) override;
	TextEdit *edit_;
	std::string *value_;
	std::string textEditValue_;
	std::string placeholder_;
	int maxLen_;
};

// TODO: Break out a lot of popup stuff from UIScreen.h.

struct ContextMenuItem {
	const char *text;
	const char *imageID;
};

// Once a selection has been made,
class PopupContextMenuScreen : public PopupScreen {
public:
	PopupContextMenuScreen(const ContextMenuItem *items, size_t itemCount, I18NCategory *category, UI::View *sourceView);
	void CreatePopupContents(ViewGroup *parent) override;

	const char *tag() const override { return "ContextMenuPopup"; }

	void SetEnabled(size_t index, bool enabled) {
		enabled_[index] = enabled;
	}

	UI::Event OnChoice;

protected:
	bool HasTitleBar() const override { return false; }

private:
	const ContextMenuItem *items_;
	size_t itemCount_;
	I18NCategory *category_;
	UI::View *sourceView_;
	std::vector<bool> enabled_;
};

class AbstractChoiceWithValueDisplay : public UI::Choice {
public:
	AbstractChoiceWithValueDisplay(const std::string &text, LayoutParams *layoutParams = nullptr)
		: Choice(text, layoutParams) {
	}

	void Draw(UIContext &dc) override;
	void GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const override;

protected:
	virtual std::string ValueText() const = 0;

	float CalculateValueScale(const UIContext &dc, const std::string &valueText, float availWidth) const;
};

// Reads and writes value to determine the current selection.
class PopupMultiChoice : public AbstractChoiceWithValueDisplay {
public:
	PopupMultiChoice(int *value, const std::string &text, const char **choices, int minVal, int numChoices,
		const char *category, ScreenManager *screenManager, UI::LayoutParams *layoutParams = nullptr)
		: AbstractChoiceWithValueDisplay(text, layoutParams), value_(value), choices_(choices), minVal_(minVal), numChoices_(numChoices),
		category_(category), screenManager_(screenManager) {
		if (*value >= numChoices + minVal)
			*value = numChoices + minVal - 1;
		if (*value < minVal)
			*value = minVal;
		OnClick.Handle(this, &PopupMultiChoice::HandleClick);
		UpdateText();
	}

	void Update() override;

	void HideChoice(int c) {
		hidden_.insert(c);
	}

	UI::Event OnChoice;

protected:
	std::string ValueText() const override;

	int *value_;
	const char **choices_;
	int minVal_;
	int numChoices_;
	void UpdateText();

private:
	UI::EventReturn HandleClick(UI::EventParams &e);

	void ChoiceCallback(int num);
	virtual void PostChoiceCallback(int num) {}

	const char *category_;
	ScreenManager *screenManager_;
	std::string valueText_;
	bool restoreFocus_ = false;
	std::set<int> hidden_;
};

// Allows passing in a dynamic vector of strings. Saves the string.
class PopupMultiChoiceDynamic : public PopupMultiChoice {
public:
	PopupMultiChoiceDynamic(std::string *value, const std::string &text, std::vector<std::string> choices,
		const char *category, ScreenManager *screenManager, UI::LayoutParams *layoutParams = nullptr)
		: UI::PopupMultiChoice(&valueInt_, text, nullptr, 0, (int)choices.size(), category, screenManager, layoutParams),
		  valueStr_(value) {
		choices_ = new const char *[numChoices_];
		valueInt_ = 0;
		for (int i = 0; i < numChoices_; i++) {
			choices_[i] = new char[choices[i].size() + 1];
			memcpy((char *)choices_[i], choices[i].c_str(), choices[i].size() + 1);
			if (*value == choices_[i])
				valueInt_ = i;
		}
		value_ = &valueInt_;
		UpdateText();
	}
	~PopupMultiChoiceDynamic() {
		for (int i = 0; i < numChoices_; i++) {
			delete[] choices_[i];
		}
		delete[] choices_;
	}

protected:
	void PostChoiceCallback(int num) override {
		*valueStr_ = choices_[num];
	}

private:
	int valueInt_;
	std::string *valueStr_;
};

class PopupSliderChoice : public AbstractChoiceWithValueDisplay {
public:
	PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);
	PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, int step, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);

	void SetFormat(const char *fmt) {
		fmt_ = fmt;
	}
	void SetZeroLabel(const std::string &str) {
		zeroLabel_ = str;
	}
	void SetNegativeDisable(const std::string &str) {
		negativeLabel_ = str;
	}

	Event OnChange;

protected:
	std::string ValueText() const override;

private:
	EventReturn HandleClick(EventParams &e);
	EventReturn HandleChange(EventParams &e);

	int *value_;
	int minValue_;
	int maxValue_;
	int step_;
	const char *fmt_;
	std::string zeroLabel_;
	std::string negativeLabel_;
	std::string units_;
	ScreenManager *screenManager_;
	bool restoreFocus_ = false;
};

class PopupSliderChoiceFloat : public AbstractChoiceWithValueDisplay {
public:
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, float step, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);

	void SetFormat(const char *fmt) {
		fmt_ = fmt;
	}
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
	EventReturn HandleClick(EventParams &e);
	EventReturn HandleChange(EventParams &e);
	float *value_;
	float minValue_;
	float maxValue_;
	float step_;
	const char *fmt_;
	std::string zeroLabel_;
	std::string units_;
	ScreenManager *screenManager_;
	bool restoreFocus_ = false;
	bool liveUpdate_ = false;
	bool hasDropShadow_ = true;
};

class PopupTextInputChoice: public AbstractChoiceWithValueDisplay {
public:
	PopupTextInputChoice(std::string *value, const std::string &title, const std::string &placeholder, int maxLen, ScreenManager *screenManager, LayoutParams *layoutParams = 0);

	Event OnChange;

protected:
	std::string ValueText() const override;

private:
	EventReturn HandleClick(EventParams &e);
	EventReturn HandleChange(EventParams &e);
	ScreenManager *screenManager_;
	std::string *value_;
	std::string placeHolder_;
	std::string defaultText_;
	int maxLen_;
	bool restoreFocus_;
};

class ChoiceWithValueDisplay : public AbstractChoiceWithValueDisplay {
public:
	ChoiceWithValueDisplay(int *value, const std::string &text, LayoutParams *layoutParams = 0)
		: AbstractChoiceWithValueDisplay(text, layoutParams), iValue_(value) {}

	ChoiceWithValueDisplay(std::string *value, const std::string &text, const char *category, LayoutParams *layoutParams = 0)
		: AbstractChoiceWithValueDisplay(text, layoutParams), sValue_(value), category_(category) {}

	ChoiceWithValueDisplay(std::string *value, const std::string &text, std::string (*translateCallback)(const char *value), LayoutParams *layoutParams = 0)
		: AbstractChoiceWithValueDisplay(text, layoutParams), sValue_(value), translateCallback_(translateCallback) {
	}

private:
	std::string ValueText() const override;

	std::string *sValue_ = nullptr;
	int *iValue_ = nullptr;
	const char *category_ = nullptr;
	std::string (*translateCallback_)(const char *value) = nullptr;
};

}  // namespace UI
