#pragma once

#include "ppsspp_config.h"

#include "UI/TabbedDialogScreen.h"

class UISettingsScreen : public UITabbedBaseDialogScreen {
public:
	UISettingsScreen(const Path &gamePath);

	void CreateTabs() override;

	const char *tag() const override { return "UISettings"; }

private:
	void CreateGeneralUISettings(UI::ViewGroup *generalUISettings);
	void CreateUISoundsSettings(UI::ViewGroup *uiSoundsSettings);
	void CreateCustomizationSettings(UI::ViewGroup *customizationSettings);
	void CreateAccessibilitySettings(UI::ViewGroup *accessibilitySettings);
	void OnImmersiveModeChange(UI::EventParams &e);
	void OnChangeBackground(UI::EventParams &e);
};
