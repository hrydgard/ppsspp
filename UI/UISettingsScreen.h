#pragma once

#include "ppsspp_config.h"

#include "UI/TabbedDialogScreen.h"

class UISettingsScreen : public UITabbedBaseDialogScreen {
public:
	UISettingsScreen(const Path &gamePath);

	void CreateTabs() override;

	const char *tag() const override { return "UISettings"; }

private:
	void CreateGeneralUISettings(UI::LinearLayout *parent);
	void CreateUISoundsSettings(UI::LinearLayout *parent);
	void CreateCustomizationSettings(UI::LinearLayout *parent);
	void CreateAccessibilitySettings(UI::LinearLayout *parent);
};
