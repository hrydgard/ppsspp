#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "Common/File/Path.h"
#include "Common/UI/PopupScreens.h"

class LoadStateConfirmScreen : public UI::PopupScreen {
public:
	LoadStateConfirmScreen(std::string_view saveStatePrefix, int slot, std::function<void(bool)> callback);
	const char *tag() const override { return "LoadStateConfirm"; }
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void TriggerFinish(DialogResult result) override;
private:
	std::string saveStatePrefix_;
	int slot_;
	Path screenshotFilename_;
	std::function<void(bool)> callback_;
};
