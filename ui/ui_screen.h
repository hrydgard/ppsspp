#pragma once

#include "ui/screen.h"
#include "ui/viewgroup.h"

class UIScreen : public Screen {
public:
	UIScreen();
	~UIScreen() { delete root_; }

	virtual void update(InputState &input);
	virtual void render();
	virtual void touch(const TouchInput &touch);
	virtual void key(const KeyInput &touch);
	virtual void axis(const AxisInput &touch);

	// Some useful default event handlers
	UI::EventReturn OnBack(UI::EventParams &e);

protected:
	virtual void CreateViews() = 0;
	virtual void DrawBackground(UIContext &dc) {}

	void RecreateViews() { recreateViews_ = true; }

	UI::ViewGroup *root_;

private:
	void DoRecreateViews();
	bool recreateViews_;

	int hatDown_;
};

class DialogScreen : public UIScreen {
public:
	virtual void key(const KeyInput &key);
};


class PopupScreen : public UIScreen {
public:
	PopupScreen(const std::string &title);

	virtual void CreatePopupContents(UI::ViewGroup *parent) = 0;
	virtual void CreateViews();
	virtual bool isTransparent() { return true; }
	virtual void key(const KeyInput &key);

protected:
	virtual bool FillVertical() { return false; }
	virtual bool ShowButtons() { return true; }
	virtual void OnCompleted() {}

private:
	UI::EventReturn OnOK(UI::EventParams &e);
	UI::EventReturn OnCancel(UI::EventParams &e);

	std::string title_;
};

class ListPopupScreen : public PopupScreen {
public:
	ListPopupScreen(const std::string &title) : PopupScreen(title) {}
	ListPopupScreen(const std::string &title, const std::vector<std::string> &items, int selected, std::function<void(int)> callback)
		: PopupScreen(title), adaptor_(items, selected), callback_(callback) {
	}

	UI::Event OnChoice;

protected:
	virtual bool FillVertical() { return true; }
	virtual bool ShowButtons() { return false; }
	void CreatePopupContents(UI::ViewGroup *parent);
	UI::StringVectorListAdaptor adaptor_;
	UI::ListView *listView_;

private:
	UI::EventReturn OnListChoice(UI::EventParams &e);

	std::function<void(int)> callback_;
};

class SliderPopupScreen : public PopupScreen {
public:
	SliderPopupScreen(int *value, int minValue, int maxValue, const std::string &title) : PopupScreen(title), value_(value), minValue_(minValue), maxValue_(maxValue) {}
	void CreatePopupContents(UI::ViewGroup *parent);

private:
	virtual void OnCompleted();
	UI::Slider *slider_;
	int *value_;
	int sliderValue_;
	int minValue_;
	int maxValue_;
};

class SliderFloatPopupScreen : public PopupScreen {
public:
	SliderFloatPopupScreen(float *value, float minValue, float maxValue, const std::string &title) : PopupScreen(title), value_(value), minValue_(minValue), maxValue_(maxValue) {}
	void CreatePopupContents(UI::ViewGroup *parent);

private:
	virtual void OnCompleted();
	UI::SliderFloat *slider_;
	float sliderValue_;
	float *value_;
	float minValue_;
	float maxValue_;
};