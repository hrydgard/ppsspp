#pragma once

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"

// This doesn't have anything to do with the background anymore. It's just a PPSSPP UIScreen
// that knows how handle sendMessage properly. Same for all the below.
class UIBaseScreen : public UIScreen {
public:
	UIBaseScreen() : UIScreen() {}
protected:
	void sendMessage(UIMessage message, const char *value) override;
};

class UIBaseDialogScreen : public UIDialogScreen {
public:
	UIBaseDialogScreen() : UIDialogScreen(), gamePath_() {}
	explicit UIBaseDialogScreen(const Path &gamePath) : UIDialogScreen(), gamePath_(gamePath) {}
protected:
	void sendMessage(UIMessage message, const char *value) override;
	void AddStandardBack(UI::ViewGroup *parent);
	Path gamePath_;
};
