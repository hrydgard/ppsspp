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

#include "Common/System/System.h"
#include "Common/VR/PPSSPPVR.h"
#include "Core/Config.h"
#include "UI/SettingsPage.h"
#include "UI/Settings/MainSettingsPages.h"

void BuildVRSettings(SettingsPage *page) {
	using namespace UI;

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);

	if (deviceType == DEVICE_TYPE_VR) {
		page->Header("Virtual reality");
		page->Setting(&g_Config.bEnableVR);
		page->Setting(&g_Config.bEnable6DoF);
		page->Setting(&g_Config.bEnableStereo);
		page->Setting(&g_Config.bEnableImmersiveVR);
		if (IsPassthroughSupported()) {
			page->Setting(&g_Config.bPassthrough);
		}
		page->Setting(&g_Config.bForce72Hz);
	}

	page->Header("VR camera");
	if (deviceType == DEVICE_TYPE_VR) {
		page->Setting(&g_Config.fCanvasDistance);
		page->Setting(&g_Config.fCanvas3DDistance);
	}
	page->Setting(&g_Config.fFieldOfViewPercentage);
	page->Setting(&g_Config.bRescaleHUD);
	page->Setting(&g_Config.fHeadUpDisplayScale).Enabled(&g_Config.bRescaleHUD);
	page->Setting(&g_Config.bManualForceVR);
}
