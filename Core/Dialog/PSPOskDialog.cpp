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

#define NUMKEYROWS 4
#define KEYSPERROW 13
#define NUMBEROFVALIDCHARS 44
const char oskKeys[NUMKEYROWS][KEYSPERROW] = 
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
	memset(inputChars, 0x00, sizeof(inputChars));
	oskParamsAddr = oskPtr;
	selectedChar = 0;
	currentInputChar = 0;

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

	return 0;
}


void PSPOskDialog::RenderKeyboard()
{
	int selectedRow = selectedChar / (KEYSPERROW-1);
	int selectedExtra = selectedChar % (KEYSPERROW-1);
	char SelectedLine[KEYSPERROW];

	PPGeDrawText(oskDesc.c_str(), 480/2, 20, PPGE_ALIGN_CENTER, 0.5f, 0xFFFFFFFF);
	for (int i=0; i<oskData.outtextlimit; i++)
	{
		if (inputChars[i] != 0)
		{
			const char aa = inputChars[i];
			PPGeDrawText(&aa, 20 + (i*16), 40, NULL , 0.5f, 0xFFFFFFFF);
		}
		else
		{
			if (currentInputChar == i)
			{
				char key = oskKeys[selectedRow][selectedExtra];
				PPGeDrawText(&key, 20 + (i*16), 40, NULL , 0.5f, 0xFF3060FF);	
			}
			else
			{
				PPGeDrawText("_", 20 + (i*16), 40, NULL , 0.5f, 0xFFFFFFFF);	
			}
		}
	}
	for (int row = 0; row < NUMKEYROWS; row++)
	{
		if (selectedRow == row)
		{
			PPGeDrawText(oskKeys[row], 20, 70 + (25 * row), NULL , 0.6f, 0xFF7f7f7f);
		}
		else
		{
			PPGeDrawText(oskKeys[row], 20, 70 + (25 * row), NULL , 0.6f, 0xFFFFFFFF);
		}
	}
	for (int selectedItemCounter = 0; selectedItemCounter < KEYSPERROW; selectedItemCounter++ )
	{
		if (selectedItemCounter!=selectedExtra)
		{
			SelectedLine[selectedItemCounter] = oskKeys[selectedRow][selectedItemCounter];
		}
		else
		{
			SelectedLine[selectedItemCounter] = '_';
		}
	}

	PPGeDrawText(SelectedLine, 20, 71 + (25 * selectedRow), NULL , 0.6f, 0xFFFFFFFF);

}

void PSPOskDialog::Update()
{
	buttons = __CtrlPeekButtons();
	int selectedRow = selectedChar / (KEYSPERROW-1);
	int selectedExtra = selectedChar % (KEYSPERROW-1);

	if (status == SCE_UTILITY_STATUS_INITIALIZE)
	{
		status = SCE_UTILITY_STATUS_RUNNING;
	}
	else if (status == SCE_UTILITY_STATUS_RUNNING)
	{		
		StartDraw();
		RenderKeyboard();
		PPGeDrawImage(I_CROSS, 100, 220, 20, 20, 0, 0xFFFFFFFF);
		PPGeDrawText("Select", 130, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);

		PPGeDrawImage(I_CIRCLE, 200, 220, 20, 20, 0, 0xFFFFFFFF);
		PPGeDrawText("Delete", 230, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);

		PPGeDrawImage(I_BUTTON, 290, 220, 50, 20, 0, 0xFFFFFFFF);
		PPGeDrawText("Start", 305, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
		PPGeDrawText("Finish", 350, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);

		if (IsButtonPressed(CTRL_UP))
		{
			selectedChar += 10;
		}
		else if (IsButtonPressed(CTRL_DOWN))
		{
			selectedChar -=10;
		}
		else if (IsButtonPressed(CTRL_LEFT))
		{
			selectedChar--;
		}
		else if (IsButtonPressed(CTRL_RIGHT))
		{
			selectedChar++;
		}

		if (selectedChar < 0)
		{
			selectedChar = NUMBEROFVALIDCHARS;
		}
		if (selectedChar > NUMBEROFVALIDCHARS)
		{
			selectedChar = 0;
		}

		// TODO : Dialogs should take control over input and not send them to the game while displaying
		if (IsButtonPressed(CTRL_CROSS))
		{
			if (currentInputChar < oskData.outtextlimit)
			{
				inputChars[currentInputChar]= oskKeys[selectedRow][selectedExtra];
				currentInputChar++;
			}
			else
			{
				currentInputChar = oskData.outtextlimit; // just in case
			}
		}
		else if (IsButtonPressed(CTRL_CIRCLE))
		{
			inputChars[currentInputChar] = 0x00;
			currentInputChar--;
		}
		else if (IsButtonPressed(CTRL_START))
		{
			status = SCE_UTILITY_STATUS_FINISHED;
		}
		EndDraw();
	}
	else if (status == SCE_UTILITY_STATUS_FINISHED)
	{
		status = SCE_UTILITY_STATUS_SHUTDOWN;
	}

	for (int i=0; i<oskData.outtextlimit; i++)
	{
		Memory::Write_U16(0x0000^inputChars[i],oskData.outtextPtr + (2*i));
	}

	oskData.outtextlength = currentInputChar;
	oskParams.base.result= 0;
	oskData.result = PSP_UTILITY_OSK_RESULT_CHANGED;
	Memory::WriteStruct(oskParams.SceUtilityOskDataPtr, &oskData);
	Memory::WriteStruct(oskParamsAddr, &oskParams);
}
