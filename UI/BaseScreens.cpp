#include "UI/BaseScreens.h"
#include "Core/Config.h"

ViewLayoutMode UIBaseScreen::LayoutMode() const {
	return ViewLayoutMode::IgnoreBottomInset;
}

bool UIBaseScreen::UseImmersiveMode() const {
	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;
	if (!portrait && config.bImmersiveMode) {
		return true;
	}
	return false;
}

ViewLayoutMode UIBaseDialogScreen::LayoutMode() const {
	return ViewLayoutMode::IgnoreBottomInset;
}

bool UIBaseDialogScreen::UseImmersiveMode() const {
	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;
	if (!portrait && config.bImmersiveMode) {
		return true;
	}
	return false;
}
