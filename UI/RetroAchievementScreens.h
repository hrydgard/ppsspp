#pragma once

#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/MiscScreens.h"
#include "UI/TabbedDialogScreen.h"
#include "Common/File/Path.h"

class RetroAchievementsListScreen : public UIDialogScreenWithGameBackground {
public:
	RetroAchievementsListScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}
	const char *tag() const override { return "RetroAchievementsListScreen"; }

	void CreateViews() override;
};

class RetroAchievementsSettingsScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	RetroAchievementsSettingsScreen(const Path &gamePath) : TabbedUIDialogScreenWithGameBackground(gamePath) {}
	const char *tag() const override { return "RetroAchievementsSettingsScreen"; }

	void CreateTabs() override;
	void sendMessage(const char *message, const char *value) override;

private:
	void CreateAccountTab(UI::ViewGroup *viewGroup);
};
