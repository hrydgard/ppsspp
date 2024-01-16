#pragma once

#include "ppsspp_config.h"

#include "Common/UI/UIScreen.h"
#include "UI/MiscScreens.h"
#include "UI/TabbedDialogScreen.h"

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class DriverManagerScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	DriverManagerScreen(const Path &gamePath);

	const char *tag() const override { return "DriverManagerScreen"; }

protected:
	void CreateTabs() override;
	bool ShowSearchControls() const override { return false; }

private:
	UI::EventReturn OnCustomDriverInstall(UI::EventParams &e);
	UI::EventReturn OnCustomDriverUninstall(UI::EventParams &e);
	UI::EventReturn OnCustomDriverChange(UI::EventParams &e);

	void CreateDriverTab(UI::ViewGroup *drivers);
};
