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

#include "PSPPlaceholderDialog.h"

PSPPlaceholderDialog::PSPPlaceholderDialog() : PSPDialog() {

}

PSPPlaceholderDialog::~PSPPlaceholderDialog() {
}


int PSPPlaceholderDialog::Init()
{
	status = SCE_UTILITY_STATUS_INITIALIZE;
	return 0;
}

int PSPPlaceholderDialog::Update(int animSpeed)
{
	//__UtilityUpdate();
	if (status == SCE_UTILITY_STATUS_INITIALIZE)
	{
		status = SCE_UTILITY_STATUS_RUNNING;
	}
	else if (status == SCE_UTILITY_STATUS_RUNNING)
	{
		//Check with JPCSPTrace log of Dream Club Portable
		//But break Project Divx extand and Kenka Banchou Bros when take screenshot
		//They are not call sceUtilityScreenshotContStart;
		//status = SCE_UTILITY_STATUS_SCREENSHOT_UNKNOWN;
		status = SCE_UTILITY_STATUS_FINISHED;
	}
	else if (status == SCE_UTILITY_STATUS_FINISHED)
	{
		status = SCE_UTILITY_STATUS_SHUTDOWN;
	}
	return 0;
}

int PSPPlaceholderDialog::ContStart()
{
	// base on JPCSP http://code.google.com/p/jpcsp/source/detail?r=3381
	// be initialized with sceUtilityScreenshotInitStart and the startupType
	// parameter has to be PSP_UTILITY_SCREENSHOT_TYPE_CONT_AUTO, otherwise, an
	// error is returned.
	if (status != SCE_UTILITY_STATUS_SCREENSHOT_UNKNOWN)
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	// Check with JPCSPTrace log of Dream Club Portable
	status = SCE_UTILITY_STATUS_FINISHED;
	return 0;
}