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

#include "i18n/i18n.h"
#include "math/math_util.h"

#include "Core/Dialog/PSPOskDialog.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Reporting.h"
#include "Common/ChunkFile.h"
#include "GPU/GPUState.h"

#ifndef _WIN32
#include <ctype.h>
#include <math.h>
#endif

#define NUMKEYROWS 4
#define KEYSPERROW 12
#define NUMBEROFVALIDCHARS (KEYSPERROW * NUMKEYROWS)
static const char oskKeys[OSK_KEYBOARD_COUNT][NUMKEYROWS][KEYSPERROW + 1] =
{
	{
		{'1','2','3','4','5','6','7','8','9','0','-','+','\0'},
		{'q','w','e','r','t','y','u','i','o','p','[',']','\0'},
		{'a','s','d','f','g','h','j','k','l',';','@','~','\0'},
		{'z','x','c','v','b','n','m',',','.','/','?','\\','\0'},
	},
	{
		{'!','@','#','$','%','^','&','*','(',')','_','+','\0'},
		{'Q','W','E','R','T','Y','U','I','O','P','{','}','\0'},
		{'A','S','D','F','G','H','J','K','L',':','"','`','\0'},
		{'Z','X','C','V','B','N','M','<','>','/','?','|','\0'},
	},
};


PSPOskDialog::PSPOskDialog() : PSPDialog() {

}

PSPOskDialog::~PSPOskDialog() {
}

void PSPOskDialog::ConvertUCS2ToUTF8(std::string& _string, const u32 em_address)
{
	char stringBuffer[2048];
	char *string = stringBuffer;

	u16 *src = (u16 *) Memory::GetPointer(em_address);
	int c;
	while (c = *src++)
	{
		if (c < 0x80)
			*string++ = c;
		else if (c < 0x800)
		{
			*string++ = 0xC0 | (c >> 6);
			*string++ = 0x80 | (c & 0x3F);
		}
		else
		{
			*string++ = 0xE0 | (c >> 12);
			*string++ = 0x80 | ((c >> 6) & 0x3F);
			*string++ = 0x80 | (c & 0x3F);
		}
	}
	*string++ = '\0';
	_string = stringBuffer;
}


int PSPOskDialog::Init(u32 oskPtr)
{
	// Ignore if already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN)
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	// Seems like this should crash?
	if (!Memory::IsValidAddress(oskPtr))
	{
		ERROR_LOG_REPORT(HLE, "sceUtilityOskInitStart: invalid params (%08x)", oskPtr);
		return -1;
	}

	oskParams = Memory::GetStruct<SceUtilityOskParams>(oskPtr);
	if (oskParams->base.size != sizeof(SceUtilityOskParams))
	{
		ERROR_LOG(HLE, "sceUtilityOskInitStart: invalid size (%d)", oskParams->base.size);
		return SCE_ERROR_UTILITY_INVALID_PARAM_SIZE;
	}
	// Also seems to crash.
	if (!Memory::IsValidAddress(oskParams->fieldPtr))
	{
		ERROR_LOG_REPORT(HLE, "sceUtilityOskInitStart: invalid field data (%08x)", oskParams->fieldPtr);
		return -1;
	}

	if (oskParams->unk_60 != 0)
		WARN_LOG_REPORT(HLE, "sceUtilityOskInitStart: unknown param is non-zero (%08x)", oskParams->unk_60);
	if (oskParams->fieldCount != 1)
		WARN_LOG_REPORT(HLE, "sceUtilityOskInitStart: unsupported field count %d", oskParams->fieldCount);

	status = SCE_UTILITY_STATUS_INITIALIZE;
	selectedChar = 0;
	currentKeyboard = OSK_KEYBOARD_LATIN_LOWERCASE;

	Memory::ReadStruct(oskParams->fieldPtr, &oskData);
	ConvertUCS2ToUTF8(oskDesc, oskData.descPtr);
	ConvertUCS2ToUTF8(oskIntext, oskData.intextPtr);
	ConvertUCS2ToUTF8(oskOuttext, oskData.outtextPtr);

	inputChars = oskIntext.substr(0, FieldMaxLength());

	// Eat any keys pressed before the dialog inited.
	__CtrlReadLatch();

	StartFade(true);
	return 0;
}

u32 PSPOskDialog::FieldMaxLength()
{
	if (oskData.outtextlimit > oskData.outtextlength - 1 || oskData.outtextlimit == 0)
		return oskData.outtextlength - 1;
	return oskData.outtextlimit;
}

void PSPOskDialog::RenderKeyboard()
{
	int selectedRow = selectedChar / KEYSPERROW;
	int selectedCol = selectedChar % KEYSPERROW;

	char temp[2];
	temp[1] = '\0';

	u32 limit = FieldMaxLength();

	const float keyboardLeftSide = (480.0f - (24.0f * KEYSPERROW)) / 2.0f;
	const float characterWidth = 12.0f;
	float previewLeftSide = (480.0f - (12.0f * limit)) / 2.0f;
	float title = (480.0f - (0.5f * limit)) / 2.0f;

	PPGeDrawText(oskDesc.c_str(), title , 20, PPGE_ALIGN_CENTER, 0.5f, CalcFadedColor(0xFFFFFFFF));
	for (u32 i = 0; i < limit; ++i)
	{
		u32 color = CalcFadedColor(0xFFFFFFFF);
		if (i < inputChars.size())
			temp[0] = inputChars[i];
		else if (i == inputChars.size())
		{
			temp[0] = oskKeys[currentKeyboard][selectedRow][selectedCol];
			float animStep = (float)(gpuStats.numFrames % 40) / 20.0f;
			// Fade in and out the next character so they know it's not part of the string yet.
			u32 alpha = (0.5f - (cosf(animStep * M_PI) / 2.0f)) * 128 + 127;
			color = CalcFadedColor((alpha << 24) | 0xFFFFFF);

			PPGeDrawText(temp, previewLeftSide + (i * characterWidth), 40.0f, 0, 0.5f, color);

			// Also draw the underline for the same reason.
			color = CalcFadedColor(0xFFFFFFFF);
			temp[0] = '_';
		}
		else
			temp[0] = '_';

		PPGeDrawText(temp, previewLeftSide + (i * characterWidth), 40.0f, 0, 0.5f, color);
	}
	for (int row = 0; row < NUMKEYROWS; ++row)
	{
		for (int col = 0; col < KEYSPERROW; ++col)
		{
			u32 color = CalcFadedColor(0xFFFFFFFF);
			if (selectedRow == row && col == selectedCol)
				color = CalcFadedColor(0xFF3060FF);

			temp[0] = oskKeys[currentKeyboard][row][col];
			PPGeDrawText(temp, keyboardLeftSide + (25.0f * col) + characterWidth / 2.0, 70.0f + (25.0f * row), PPGE_ALIGN_HCENTER, 0.6f, color);

			if (selectedRow == row && col == selectedCol)
				PPGeDrawText("_", keyboardLeftSide + (25.0f * col) + characterWidth / 2.0, 70.0f + (25.0f * row), PPGE_ALIGN_HCENTER, 0.6f, CalcFadedColor(0xFFFFFFFF));
		}
	}
}

int PSPOskDialog::Update()
{
	buttons = __CtrlReadLatch();
	int selectedRow = selectedChar / KEYSPERROW;
	int selectedExtra = selectedChar % KEYSPERROW;

	u32 limit = FieldMaxLength();

	if (status == SCE_UTILITY_STATUS_INITIALIZE)
	{
		status = SCE_UTILITY_STATUS_RUNNING;
	}
	else if (status == SCE_UTILITY_STATUS_RUNNING)
	{		
		UpdateFade();

		StartDraw();
		RenderKeyboard();
		PPGeDrawImage(I_CROSS, 30, 220, 20, 20, 0, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawImage(I_CIRCLE, 150, 220, 20, 20, 0, CalcFadedColor(0xFFFFFFFF));
		//PPGeDrawImage(I_BUTTON, 230, 220, 50, 20, 0, CalcFadedColor(0xFFFFFFFF));
		//PPGeDrawImage(I_BUTTON, 350, 220, 55, 20, 0, CalcFadedColor(0xFFFFFFFF));

		I18NCategory *m = GetI18NCategory("Dialog");
		PPGeDrawText(m->T("Select"), 60, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(m->T("Delete"), 180, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Start", 245, 220, PPGE_ALIGN_LEFT, 0.6f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(m->T("Finish"), 290, 222, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Select", 365, 220, PPGE_ALIGN_LEFT, 0.6f, CalcFadedColor(0xFFFFFFFF));
		// TODO: Show title of next keyboard?
		PPGeDrawText(m->T("Shift"), 415, 222, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));

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
				inputChars += oskKeys[currentKeyboard][selectedRow][selectedExtra];
		}
		else if (IsButtonPressed(CTRL_SELECT))
		{
			// TODO: Limit by allowed keyboards...
			currentKeyboard = (OskKeyboardDisplay)((currentKeyboard + 1) % OSK_KEYBOARD_COUNT);
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

	for (u32 i = 0; i < oskData.outtextlength; ++i)
	{
		u16 value = 0;
		if (i < inputChars.size())
			value = inputChars[i];
		Memory::Write_U16(value, oskData.outtextPtr + (2 * i));
	}

	oskParams->base.result = 0;
	oskData.result = PSP_UTILITY_OSK_RESULT_CHANGED;
	Memory::WriteStruct(oskParams->fieldPtr, &oskData);

	return 0;
}

template <typename T>
static void DoBasePointer(PointerWrap &p, T **ptr)
{
	u32 addr = *ptr == NULL ? 0 : (u8 *) *ptr - Memory::base;
	p.Do(addr);
	if (addr == 0)
		*ptr = NULL;
	else
		*ptr = Memory::GetStruct<T>(addr);

}

void PSPOskDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);
	DoBasePointer(p, &oskParams);
	p.Do(oskData);
	p.Do(oskDesc);
	p.Do(oskIntext);
	p.Do(oskOuttext);
	p.Do(selectedChar);
	p.Do(inputChars);
	p.DoMarker("PSPOskDialog");
}
