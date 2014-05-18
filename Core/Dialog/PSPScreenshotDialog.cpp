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

#include "PSPScreenshotDialog.h"
#include "Core/MemMap.h"
#include "ChunkFile.h"

enum SceUtilityScreenshotType
{
	SCE_UTILITY_SCREENSHOT_TYPE_GUI = 0,
	SCE_UTILITY_SCREENSHOT_TYPE_AUTO = 1,
	SCE_UTILITY_SCREENSHOT_TYPE_SAVE = 2,
	SCE_UTILITY_SCREENSHOT_TYPE_VIEW = 3,
	SCE_UTILITY_SCREENSHOT_TYPE_CONT_FINISH = 4,
	SCE_UTILITY_SCREENSHOT_TYPE_CONT_AUTO = 5,
};

PSPScreenshotDialog::PSPScreenshotDialog() : PSPDialog() {

}

PSPScreenshotDialog::~PSPScreenshotDialog() {
}

int PSPScreenshotDialog::Init(int paramAddr)
{
	// Already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	mode = Memory::Read_U32(paramAddr);
	status = SCE_UTILITY_STATUS_INITIALIZE;

	return 0;
}

int PSPScreenshotDialog::Update(int animSpeed)
{
	PSPDialog::DialogStatus retval = status;
	if (UseAutoStatus()) {
		if (status == SCE_UTILITY_STATUS_INITIALIZE) {
			status = SCE_UTILITY_STATUS_RUNNING;
		} else if (status == SCE_UTILITY_STATUS_RUNNING) {
			// There is some unknown reason that don't work correctly
			// Temp disable
//			if ((mode & 0x7) == SCE_UTILITY_SCREENSHOT_TYPE_CONT_AUTO || (mode & 0x7) == SCE_UTILITY_SCREENSHOT_TYPE_CONT_FINISH) {
				// When screenshot cont. mode is specified , sceUtilityScreenshotContStart will be called in next call.
//				status = SCE_UTILITY_STATUS_SCREENSHOT_UNKNOWN;
//			} else {
				status = SCE_UTILITY_STATUS_FINISHED;
//			}
		} else if (status == SCE_UTILITY_STATUS_FINISHED) {
			status = SCE_UTILITY_STATUS_SHUTDOWN;
		}
	}
	return retval;
}

int PSPScreenshotDialog::ContStart()
{
	// Based on JPCSP http://code.google.com/p/jpcsp/source/detail?r=3381
	if (status != SCE_UTILITY_STATUS_SCREENSHOT_UNKNOWN)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	// Check with JPCSPTrace log of Dream Club Portable
	status = SCE_UTILITY_STATUS_FINISHED;

	return 0;
}

void PSPScreenshotDialog::DoState(PointerWrap &p) {
	PSPDialog::DoState(p);

	auto s = p.Section("PSPScreenshotDialog", 0, 1);
	if (!s)
		return;

	p.Do(mode);
}
