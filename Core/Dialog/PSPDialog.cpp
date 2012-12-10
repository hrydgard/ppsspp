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

#include "../Util/PPGeDraw.h"
#include "PSPDialog.h"

PSPDialog::PSPDialog() : status(SCE_UTILITY_STATUS_SHUTDOWN)
, lastButtons(0)
, buttons(0)
{

}

PSPDialog::~PSPDialog() {
}

PSPDialog::DialogStatus PSPDialog::GetStatus()
{
	PSPDialog::DialogStatus retval = status;
	if (status == SCE_UTILITY_STATUS_SHUTDOWN)
		status = SCE_UTILITY_STATUS_NONE;
	if (status == SCE_UTILITY_STATUS_INITIALIZE)
		status = SCE_UTILITY_STATUS_RUNNING;
	return retval;
}

void PSPDialog::StartDraw()
{
	PPGeBegin();
	PPGeDraw4Patch(I_BUTTON, 0, 0, 480, 272, 0xcFFFFFFF);
}
void PSPDialog::EndDraw()
{
	PPGeEnd();
}

void PSPDialog::Shutdown()
{
	status = SCE_UTILITY_STATUS_SHUTDOWN;
}

void PSPDialog::Update()
{

}

bool PSPDialog::IsButtonPressed(int checkButton)
{
	return (!(lastButtons & checkButton)) && (buttons & checkButton);
}


