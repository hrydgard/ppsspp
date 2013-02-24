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

#include "PSPOskDialog.h"
#include "../Util/PPGeDraw.h"
#include "../HLE/sceCtrl.h"
#include "ChunkFile.h"

#define NUMKEYROWS 4
#define KEYSPERROW 12
#define NUMBEROFVALIDCHARS (KEYSPERROW * NUMKEYROWS)
const char oskKeys[NUMKEYROWS][KEYSPERROW + 1] =
{
	{'1','2','3','4','5','6','7','8','9','0','-','+','\0'}, 
	{'Q','W','E','R','T','Y','U','I','O','P','[',']','\0'},
	{'A','S','D','F','G','H','J','K','L',';','@','~','\0'},
	{'Z','X','C','V','B','N','M',',','.','/','?','\\','\0'},
};


PSPOskDialog::PSPOskDialog() : PSPDialog() {

}

PSPOskDialog::~PSPOskDialog() {
}

// Same as get string but read out 16bit
void PSPOskDialog::HackyGetStringWide(std::string& _string, const u32 em_address)
{
	char stringBuffer[2048];
	char *string = stringBuffer;
	char c;
	u32 addr = em_address;
	while ((c = (char)(Memory::Read_U16(addr))))
	{
		*string++ = c;
		addr+=2;
	}
	*string++ = '\0';
	_string = stringBuffer;
}


int PSPOskDialog::Init(u32 oskPtr)
{
	// Ignore if already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN)
	{
		return -1;
	}
	status = SCE_UTILITY_STATUS_INITIALIZE;

	memset(&oskParams, 0, sizeof(oskParams));
	memset(&oskData, 0, sizeof(oskData));
	// TODO: should this be init'd to oskIntext?
	inputChars.clear();
	oskParamsAddr = oskPtr;
	selectedChar = 0;

	if (Memory::IsValidAddress(oskPtr))
	{
		Memory::ReadStruct(oskPtr, &oskParams);
		Memory::ReadStruct(oskParams.SceUtilityOskDataPtr, &oskData);
		HackyGetStringWide(oskDesc, oskData.descPtr);
		HackyGetStringWide(oskIntext, oskData.intextPtr);
		HackyGetStringWide(oskOuttext, oskData.outtextPtr);
		Memory::WriteStruct(oskParams.SceUtilityOskDataPtr, &oskData);
		Memory::WriteStruct(oskPtr, &oskParams);
	}
	else
	{
		return -1;
	}

	// Eat any keys pressed before the dialog inited.
	__CtrlReadLatch();

	StartFade(true);
	return 0;
}


void PSPOskDialog::RenderKeyboard()
{
	int selectedRow = selectedChar / KEYSPERROW;
	int selectedExtra = selectedChar % KEYSPERROW;

	char temp[2];
	temp[1] = '\0';

	u32 limit = oskData.outtextlimit;
	// TODO: Test more thoroughly.  Encountered a game where this was 0.
	if (limit <= 0)
		limit = 16;

	const float keyboardLeftSide = (480.0f - (24.0f * KEYSPERROW)) / 2.0f;
	float previewLeftSide = (480.0f - (16.0f * limit)) / 2.0f;
	float title = (480.0f - limit) / 2.0f;

	PPGeDrawText(oskDesc.c_str(), title , 20, PPGE_ALIGN_CENTER, 0.5f, CalcFadedColor(0xFFFFFFFF));
	for (u32 i = 0; i < limit; ++i)
	{
		u32 color = CalcFadedColor(0xFFFFFFFF);
		if (i < inputChars.size())
			temp[0] = inputChars[i];
		else if (i == inputChars.size())
		{
			temp[0] = oskKeys[selectedRow][selectedExtra];
			color = CalcFadedColor(0xFF3060FF);
		}
		else
			temp[0] = '_';

		PPGeDrawText(temp, previewLeftSide + (i * 16.0f), 40.0f, 0, 0.5f, color);
	}
	for (int row = 0; row < NUMKEYROWS; ++row)
	{
		for (int col = 0; col < KEYSPERROW; ++col)
		{
			u32 color = CalcFadedColor(0xFFFFFFFF);
			if (selectedRow == row && col == selectedExtra)
				color = CalcFadedColor(0xFF7f7f7f);

			temp[0] = oskKeys[row][col];
			PPGeDrawText(temp, keyboardLeftSide + (25.0f * col), 70.0f + (25.0f * row), 0, 0.6f, color);

			if (selectedRow == row && col == selectedExtra)
				PPGeDrawText("_", keyboardLeftSide + (25.0f * col), 70.0f + (25.0f * row), 0, 0.6f, CalcFadedColor(0xFFFFFFFF));
		}
	}

}

int PSPOskDialog::Update()
{
	buttons = __CtrlReadLatch();
	int selectedRow = selectedChar / KEYSPERROW;
	int selectedExtra = selectedChar % KEYSPERROW;

	u32 limit = oskData.outtextlimit;
	// TODO: Test more thoroughly.  Encountered a game where this was 0.
	if (limit <= 0)
		limit = 16;

	if (status == SCE_UTILITY_STATUS_INITIALIZE)
	{
		status = SCE_UTILITY_STATUS_RUNNING;
	}
	else if (status == SCE_UTILITY_STATUS_RUNNING)
	{		
		UpdateFade();

		StartDraw();
		RenderKeyboard();
		PPGeDrawImage(I_CROSS, 100, 220, 20, 20, 0, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Select", 130, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));

		PPGeDrawImage(I_CIRCLE, 200, 220, 20, 20, 0, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Delete", 230, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));

		PPGeDrawImage(I_BUTTON, 290, 220, 50, 20, 0, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Start", 305, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Finish", 350, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));

		if (IsButtonPressed(CTRL_UP))
		{
			selectedChar -= KEYSPERROW;
		}
		else if (IsButtonPressed(CTRL_DOWN))
		{
			selectedChar += KEYSPERROW;
		}
		else if (IsButtonPressed(CTRL_LEFT))
		{
			selectedChar--;
			if (((selectedChar + KEYSPERROW) % KEYSPERROW) == KEYSPERROW - 1)
				selectedChar += KEYSPERROW;
		}
		else if (IsButtonPressed(CTRL_RIGHT))
		{
			selectedChar++;
			if ((selectedChar % KEYSPERROW) == 0)
				selectedChar -= KEYSPERROW;
		}

		selectedChar = (selectedChar + NUMBEROFVALIDCHARS) % NUMBEROFVALIDCHARS;

		if (IsButtonPressed(CTRL_CROSS))
		{
			if (inputChars.size() < limit)
				inputChars += oskKeys[selectedRow][selectedExtra];
		}
		else if (IsButtonPressed(CTRL_CIRCLE))
		{
			if (inputChars.size() > 0)
				inputChars.resize(inputChars.size() - 1);
		}
		else if (IsButtonPressed(CTRL_START))
		{
			StartFade(false);
		}
		EndDraw();
	}
	else if (status == SCE_UTILITY_STATUS_FINISHED)
	{
		status = SCE_UTILITY_STATUS_SHUTDOWN;
	}

	for (u32 i = 0; i < limit; ++i)
	{
		u16 value = 0;
		if (i < inputChars.size())
			value = 0x0000 ^ inputChars[i];
		Memory::Write_U16(value, oskData.outtextPtr + (2 * i));
	}

	oskData.outtextlength = (u32)inputChars.size();
	oskParams.base.result= 0;
	oskData.result = PSP_UTILITY_OSK_RESULT_CHANGED;
	Memory::WriteStruct(oskParams.SceUtilityOskDataPtr, &oskData);
	Memory::WriteStruct(oskParamsAddr, &oskParams);

	return 0;
}

void PSPOskDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);
	p.Do(oskParams);
	p.Do(oskData);
	p.Do(oskDesc);
	p.Do(oskIntext);
	p.Do(oskOuttext);
	p.Do(oskParamsAddr);
	p.Do(selectedChar);
	p.Do(inputChars);
	p.DoMarker("PSPOskDialog");
}
