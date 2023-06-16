#pragma once

#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/MiscScreens.h"
#include "Common/File/Path.h"

class RetroAchievementsListScreen : public UIDialogScreenWithGameBackground {
public:
	RetroAchievementsListScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}
	const char *tag() const override { return "RetroAchievementsListScreen"; }

	void CreateViews() override;
};

class RetroAchievementsSetupScreen : public UIDialogScreenWithGameBackground {
public:
	RetroAchievementsSetupScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}

};
