#pragma once

#include <set>

#include "ui/screen.h"
#include "ui/viewgroup.h"

class I18NCategory;
class Thin3DContext;

class UIScreen : public Screen {
public:
	UIScreen();
	~UIScreen();

	virtual void update(InputState &input) override;
	virtual void preRender() override;
	virtual void render() override;
	virtual void postRender() override;

	virtual bool touch(const TouchInput &touch) override;
	virtual bool key(const KeyInput &touch) override;
	virtual bool axis(const AxisInput &touch) override;

	// Some useful default event handlers
	UI::EventReturn OnOK(UI::EventParams &e);
	UI::EventReturn OnCancel(UI::EventParams &e);
	UI::EventReturn OnBack(UI::EventParams &e);

protected:
	virtual void CreateViews() = 0;
	virtual void DrawBackground(UIContext &dc) {}

	virtual void RecreateViews() override { recreateViews_ = true; }

	UI::ViewGroup *root_;

private:
	void DoRecreateViews();

	bool recreateViews_;

	int hatDown_;
};

class UIDialogScreen : public UIScreen {
public:
	UIDialogScreen() : UIScreen(), finished_(false) {}
	virtual bool key(const KeyInput &key) override;

private:
	bool finished_;
};


class PopupScreen : public UIDialogScreen {
public:
	PopupScreen(std::string title, std::string button1 = "", std::string button2 = "");

	virtual void CreatePopupContents(UI::ViewGroup *parent) = 0;
	virtual void CreateViews() override;
	virtual bool isTransparent() const override { return true; }
	virtual bool touch(const TouchInput &touch) override;
	virtual bool key(const KeyInput &key) override;

protected:
	virtual bool FillVertical() const { return false; }
	virtual UI::Size PopupWidth() const { return 550; }
	virtual bool ShowButtons() const { return true; }
	virtual void OnCompleted(DialogResult result) {}

private:
	UI::EventReturn OnOK(UI::EventParams &e);
	UI::EventReturn OnCancel(UI::EventParams &e);

	UI::ViewGroup *box_;
	UI::Button *defaultButton_;
	std::string title_;
	std::string button1_;
	std::string button2_;
};

class ListPopupScreen : public PopupScreen {
public:
	ListPopupScreen(std::string title) : PopupScreen(title), showButtons_(false) {}
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
	virtual std::string tag() const override { return std::string("listpopup"); }

	UI::Event OnChoice;

protected:
	virtual bool FillVertical() const override { return false; }
	virtual bool ShowButtons() const override { return showButtons_; }
	virtual void CreatePopupContents(UI::ViewGroup *parent) override;
	UI::StringVectorListAdaptor adaptor_;
	UI::ListView *listView_;

private:
	UI::EventReturn OnListChoice(UI::EventParams &e);

	std::function<void(int)> callback_;
	bool showButtons_;
	std::set<int> hidden_;
};

class MessagePopupScreen : public PopupScreen {
public:
	MessagePopupScreen(std::string title, std::string message, std::string button1, std::string button2, std::function<void(bool)> callback) 
		: PopupScreen(title, button1, button2), message_(message), callback_(callback) {}
	UI::Event OnChoice;

protected:
	virtual bool FillVertical() const override { return false; }
	virtual bool ShowButtons() const override { return true; }
	virtual void CreatePopupContents(UI::ViewGroup *parent) override;

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
	: PopupScreen(title, "OK", "Cancel"), units_(units), value_(value), minValue_(minValue), maxValue_(maxValue), step_(step), changing_(false) {}
	virtual void CreatePopupContents(ViewGroup *parent) override;

	Event OnChange;

private:
	EventReturn OnDecrease(EventParams &params);
	EventReturn OnIncrease(EventParams &params);
	EventReturn OnTextChange(EventParams &params);
	EventReturn OnSliderChange(EventParams &params);
	virtual void OnCompleted(DialogResult result) override;
	Slider *slider_;
	UI::TextEdit *edit_;
	std::string units_;
	int *value_;
	int sliderValue_;
	int minValue_;
	int maxValue_;
	int step_;
	bool changing_;
};

class SliderFloatPopupScreen : public PopupScreen {
public:
	SliderFloatPopupScreen(float *value, float minValue, float maxValue, const std::string &title, float step = 1.0f, const std::string &units = "")
	: PopupScreen(title, "OK", "Cancel"), units_(units), value_(value), minValue_(minValue), maxValue_(maxValue), step_(step), changing_(false) {}
	void CreatePopupContents(UI::ViewGroup *parent) override;

	Event OnChange;

private:
	EventReturn OnIncrease(EventParams &params);
	EventReturn OnDecrease(EventParams &params);
	EventReturn OnTextChange(EventParams &params);
	EventReturn OnSliderChange(EventParams &params);
	virtual void OnCompleted(DialogResult result) override;
	UI::SliderFloat *slider_;
	UI::TextEdit *edit_;
	std::string units_;
	float sliderValue_;
	float *value_;
	float minValue_;
	float maxValue_;
	float step_;
	bool changing_;
};

class TextEditPopupScreen : public PopupScreen {
public:
	TextEditPopupScreen(std::string *value, std::string &placeholder, const std::string &title, int maxLen)
		: PopupScreen(title, "OK", "Cancel"), value_(value), placeholder_(placeholder), maxLen_(maxLen) {}
	virtual void CreatePopupContents(ViewGroup *parent) override;

	Event OnChange;

private:
	virtual void OnCompleted(DialogResult result) override;
	TextEdit *edit_;
	std::string *value_;
	std::string textEditValue_;
	std::string placeholder_;
	int maxLen_;
};

// Reads and writes value to determine the current selection.
class PopupMultiChoice : public UI::Choice {
public:
	PopupMultiChoice(int *value, const std::string &text, const char **choices, int minVal, int numChoices,
		const char *category, ScreenManager *screenManager, UI::LayoutParams *layoutParams = 0)
		: UI::Choice(text, "", false, layoutParams), value_(value), choices_(choices), minVal_(minVal), numChoices_(numChoices), 
		category_(category), screenManager_(screenManager) {
		if (*value >= numChoices+minVal) *value = numChoices+minVal-1;
		if (*value < minVal) *value = minVal;
		OnClick.Handle(this, &PopupMultiChoice::HandleClick);
		UpdateText();
	}

	virtual void Draw(UIContext &dc) override;
	virtual void Update(const InputState &input_state) override;

	void HideChoice(int c) {
		hidden_.insert(c);
	}

	UI::Event OnChoice;

private:
	void UpdateText();
	UI::EventReturn HandleClick(UI::EventParams &e);

	void ChoiceCallback(int num);

	int *value_;
	const char **choices_;
	int minVal_;
	int numChoices_;
	const char *category_;
	ScreenManager *screenManager_;
	std::string valueText_;
	bool restoreFocus_;
	std::set<int> hidden_;
};


class PopupSliderChoice : public Choice {
public:
	PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);
	PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, int step, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);

	virtual void Draw(UIContext &dc) override;

	void SetFormat(const char *fmt) {
		fmt_ = fmt;
	}
	void SetZeroLabel(const std::string &str) {
		zeroLabel_ = str;
	}

	Event OnChange;

private:
	EventReturn HandleClick(EventParams &e);
	EventReturn HandleChange(EventParams &e);

	int *value_;
	int minValue_;
	int maxValue_;
	int step_;
	const char *fmt_;
	std::string zeroLabel_;
	std::string units_;
	ScreenManager *screenManager_;
	bool restoreFocus_;
};

class PopupSliderChoiceFloat : public Choice {
public:
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, float step, ScreenManager *screenManager, const std::string &units = "", LayoutParams *layoutParams = 0);

	virtual void Draw(UIContext &dc) override;

	void SetFormat(const char *fmt) {
		fmt_ = fmt;
	}
	void SetZeroLabel(const std::string &str) {
		zeroLabel_ = str;
	}

	Event OnChange;

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
	bool restoreFocus_;
};

class PopupTextInputChoice: public Choice {
public:
	PopupTextInputChoice(std::string *value, const std::string &title, const std::string &placeholder, int maxLen, ScreenManager *screenManager, LayoutParams *layoutParams = 0);

	virtual void Draw(UIContext &dc) override;

	Event OnChange;

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

class ChoiceWithValueDisplay : public UI::Choice {
public:
	ChoiceWithValueDisplay(int *value, const std::string &text, LayoutParams *layoutParams = 0)
		: Choice(text, layoutParams), iValue_(value), category_(nullptr) { sValue_ = nullptr; }

	ChoiceWithValueDisplay(std::string *value, const std::string &text, const char *category, LayoutParams *layoutParams = 0)
		: Choice(text, layoutParams), sValue_(value), category_(category) { iValue_ = nullptr; }

	virtual void Draw(UIContext &dc) override;

private:
	int *iValue_;
	std::string *sValue_;
	const char *category_;
};

}  // namespace UI
