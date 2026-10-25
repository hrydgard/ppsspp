// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <string>

#include "ppsspp_config.h"

#include "Common/Data/Text/I18n.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/View.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/SettingsPage.h"
#include "UI/Settings/MainSettingsPages.h"
#include "UI/TiltAnalogSettingsScreen.h"
#include "UI/TouchControlLayoutScreen.h"

void BuildControlsSettings(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	page->Header({ I18NCat::MAINSETTINGS, "Controls" });
	page->Choice("Control mapping").OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new ControlMappingScreen(host->PageGamePath()));
	});
	page->Choice("Calibrate analog stick").OnClick([host](UI::EventParams &e) {
		host->PageScreenManager()->push(new AnalogCalibrationScreen(host->PageGamePath()));
	});

#if (PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
	page->Setting(&g_Config.bSystemControls);
#endif
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	page->Setting(&g_Config.bGamepadOnlyFocused);
#endif

	if (System_GetPropertyBool(SYSPROP_HAS_ACCELEROMETER)) {
		page->Custom("Tilt control setup", [host](UI::ViewGroup *parent) -> UI::View * {
			// Show the tilt type on the item.
			Choice *customizeTilt = parent->Add(new ChoiceWithCallbackValueDisplay(T(I18NCat::CONTROLS, "Tilt control setup"), []() -> std::string {
				if (g_Config.bTiltInputEnabled && (u32)g_Config.iTiltInputType < (u32)g_numTiltTypes) {
					auto co = GetI18NCategory(I18NCat::CONTROLS);
					return std::string(co->T(g_tiltTypes[g_Config.iTiltInputType]));
				} else {
					auto di = GetI18NCategory(I18NCat::DIALOG);
					return std::string(di->T("Disabled"));
				}
			}));
			customizeTilt->OnClick.Add([host](UI::EventParams &e) {
				host->PageScreenManager()->push(new TiltAnalogSettingsScreen(host->PageGamePath()));
			});
			return customizeTilt;
		});
	} else if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_VR) {  // TODO: This seems like a regression
		page->Setting(&g_Config.bHapticFeedback);
	}

	page->Header("Analog to digital mapping");
	page->Setting(&g_Config.fAnalogTriggerThreshold);
	page->Setting(&g_Config.fAnalogStickThreshold);

	// TVs don't have touch control, at least not yet.
	if ((deviceType != DEVICE_TYPE_TV) && (deviceType != DEVICE_TYPE_VR)) {
		page->Header("On-screen touch controls");
		page->Setting(&g_Config.bShowTouchControls);
		page->Choice("Edit touch control layout").OnClick([host](UI::EventParams &e) {
			host->PageScreenManager()->push(new TouchControlLayoutScreen(host->PageGamePath()));
		}).Enabled(&g_Config.bShowTouchControls);
		page->Choice("Gesture mapping").OnClick([host](UI::EventParams &e) {
			host->PageScreenManager()->push(new GestureMappingScreen(host->PageGamePath()));
		}).Enabled(&g_Config.bShowTouchControls);

		page->Setting(&g_Config.iTouchButtonStyle).Enabled(&g_Config.bShowTouchControls);

		page->Setting(&g_Config.iTouchButtonOpacity).Enabled(&g_Config.bShowTouchControls);
		page->Setting(&g_Config.iTouchButtonHideSeconds).Enabled(&g_Config.bShowTouchControls);

		page->Setting(&g_Config.bTouchGliding).Enabled(&g_Config.bShowTouchControls);

		TouchControlConfig &touch = g_Config.GetTouchControlsConfig(g_display.GetDeviceOrientation());

		// Hide stick background, useful when increasing the size
		page->Setting(&touch.bHideStickBackground).Enabled(&g_Config.bShowTouchControls);

		// Sticky D-pad.
		page->Setting(&g_Config.bStickyTouchDPad).Enabled(&g_Config.bShowTouchControls);
		page->Hint("Easier sweeping movements");

		// Re-centers itself to the touch location on touch-down.
		page->Setting(&g_Config.bAutoCenterTouchAnalog).Enabled(&g_Config.bShowTouchControls);

		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
			page->Setting(&g_Config.bHapticFeedback);
		}

		// The pause button is now a regular on-screen button.

		page->Setting(&g_Config.bDisableDpadDiagonals).Enabled(&g_Config.bShowTouchControls);
	}

#if PPSSPP_PLATFORM(WINDOWS)
	page->Header("Control input sources");
	page->Setting(&g_Config.bAllowHIDInput);
	page->Setting(&g_Config.bAllowXInput);
	page->Setting(&g_Config.bAllowDInput);
#endif

	if (deviceType != DEVICE_TYPE_VR) {
		page->Header({ I18NCat::CONTROLS, "Keyboard", "Keyboard Control Settings" });
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		page->Setting(&g_Config.bIgnoreWindowsKey);
#endif
		page->Setting(&g_Config.fAnalogLimiterDeadzone);
		page->Hint({ I18NCat::CONTROLS, "AnalogLimiter Tip", "When the analog limiter button is pressed" });
		page->Setting(&g_Config.iRapidFireInterval);
#if PPSSPP_PLATFORM(WINDOWS) || defined(SDL) || PPSSPP_PLATFORM(ANDROID)
		bool enableMouseSettings = true;
#if PPSSPP_PLATFORM(ANDROID)
		if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) < 12) {
			enableMouseSettings = false;
		}
#endif
#else
		bool enableMouseSettings = false;
#endif
		if (enableMouseSettings) {
			// The mousewheel button-release setting is independent of actual mouse delta control.
			page->Header({ I18NCat::CONTROLS, "Mouse", "Mouse settings" });
			page->Setting(&g_Config.iMouseWheelUpDelayMs);

			page->Setting(&g_Config.bMouseControl);
			page->Hint({ I18NCat::CONTROLS, "MouseControl Tip", "You can now map mouse in control mapping screen by pressing the 'M' icon." });

#if !PPSSPP_PLATFORM(ANDROID)
			page->Setting(&g_Config.bMouseConfine).Enabled(&g_Config.bMouseControl);
#endif
			page->Setting(&g_Config.fMouseSensitivity).Enabled(&g_Config.bMouseControl).LiveUpdate();
			page->Setting(&g_Config.fMouseSmoothing).Enabled(&g_Config.bMouseControl).LiveUpdate();
		}
	}
}
