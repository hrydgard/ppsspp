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

#define FADE_TIME 0.5

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
	PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0x90606060));
}
void PSPDialog::EndDraw()
{
	PPGeEnd();
}

void PSPDialog::DisplayMessage(std::string text)
{
	PPGeDrawText(text.c_str(), 40, 30, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
}

int PSPDialog::Shutdown()
{
	status = SCE_UTILITY_STATUS_SHUTDOWN;
	return 0;
}

void PSPDialog::StartFade(bool fadeIn_)
{
	isFading = true;
	fadeTimer = 0;
	fadeIn = fadeIn_;
}

void PSPDialog::UpdateFade()
{
	if(isFading)
	{
		fadeTimer += 1.0f/30; // Probably need a more real value of delta time
		if(fadeTimer < FADE_TIME)
		{
			if(fadeIn)
				fadeValue = fadeTimer / FADE_TIME * 255;
			else
				fadeValue = 255 - fadeTimer / FADE_TIME * 255;
		}
		else
		{
			fadeValue = (fadeIn?255:0);
			isFading = false;
			if(!fadeIn)
			{
				status = SCE_UTILITY_STATUS_FINISHED;
			}
		}
	}
}

u32 PSPDialog::CalcFadedColor(u32 inColor)
{
	u32 alpha = inColor >> 24;
	alpha = alpha * fadeValue / 255;
	return (inColor & 0x00FFFFFF) | (alpha << 24);
}

int PSPDialog::Update()
{
	return 0;
}

void PSPDialog::DoState(PointerWrap &p)
{
	p.Do(status);
	p.Do(lastButtons);
	p.Do(buttons);
	p.DoMarker("PSPDialog");
}

bool PSPDialog::IsButtonPressed(int checkButton)
{
	if(isFading) return false;
	return (!(lastButtons & checkButton)) && (buttons & checkButton);
}


