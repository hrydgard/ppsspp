#pragma once

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"

// This doesn't have anything to do with the background anymore. It's just a PPSSPP UIScreen
// that knows how handle sendMessage properly. Same for all the below.
class UIBaseScreen : public UIScreen {
public:
	UIBaseScreen() : UIScreen() {}
	ViewLayoutMode LayoutMode() const override {
		return ViewLayoutMode::IgnoreBottomInset;
	}
	bool UseImmersiveMode() const override;
	bool LimitContentArea() const override { return true; }
protected:
	void sendMessage(UIMessage message, const char *value) override;
};

class UIBaseDialogScreen : public UIDialogScreen {
public:
	UIBaseDialogScreen() : UIDialogScreen(), gamePath_() {}
	explicit UIBaseDialogScreen(const Path &gamePath) : UIDialogScreen(), gamePath_(gamePath) {}

	bool LimitContentArea() const override { return true; }
	float MaxContentWidth() const override { return 1400.0f; }

protected:
	ViewLayoutMode LayoutMode() const override;
	bool UseImmersiveMode() const override;

	void sendMessage(UIMessage message, const char *value) override;
	Path gamePath_;
};
