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


/**
 * Enumeration for input language
 */
enum SceUtilityOskInputLanguage
{
	PSP_UTILITY_OSK_LANGUAGE_DEFAULT =		0x00,
	PSP_UTILITY_OSK_LANGUAGE_JAPANESE =		0x01,
	PSP_UTILITY_OSK_LANGUAGE_ENGLISH =		0x02,
	PSP_UTILITY_OSK_LANGUAGE_FRENCH =		0x03,
	PSP_UTILITY_OSK_LANGUAGE_SPANISH =		0x04,
	PSP_UTILITY_OSK_LANGUAGE_GERMAN =		0x05,
	PSP_UTILITY_OSK_LANGUAGE_ITALIAN =		0x06,
	PSP_UTILITY_OSK_LANGUAGE_DUTCH =		0x07,
	PSP_UTILITY_OSK_LANGUAGE_PORTUGESE =	0x08,
	PSP_UTILITY_OSK_LANGUAGE_RUSSIAN =		0x09,
	PSP_UTILITY_OSK_LANGUAGE_KOREAN =		0x0a
};

/**
 * Enumeration for OSK internal state
 */
enum SceUtilityOskState
{
	PSP_UTILITY_OSK_DIALOG_NONE = 0,	/**< No OSK is currently active */
	PSP_UTILITY_OSK_DIALOG_INITING,		/**< The OSK is currently being initialized */
	PSP_UTILITY_OSK_DIALOG_INITED,		/**< The OSK is initialised */
	PSP_UTILITY_OSK_DIALOG_VISIBLE,		/**< The OSK is visible and ready for use */
	PSP_UTILITY_OSK_DIALOG_QUIT,		/**< The OSK has been cancelled and should be shut down */
	PSP_UTILITY_OSK_DIALOG_FINISHED		/**< The OSK has successfully shut down */	
};

/**
 * Enumeration for OSK field results
 */
enum SceUtilityOskResult
{
	PSP_UTILITY_OSK_RESULT_UNCHANGED =	0,
	PSP_UTILITY_OSK_RESULT_CANCELLED,
	PSP_UTILITY_OSK_RESULT_CHANGED
};

/**
 * Enumeration for input types (these are limited by initial choice of language)
 */
enum SceUtilityOskInputType
{
	PSP_UTILITY_OSK_INPUTTYPE_ALL =						0x00000000,
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_DIGIT =				0x00000001,
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_SYMBOL =			0x00000002,
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_LOWERCASE =			0x00000004,
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_UPPERCASE =			0x00000008,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_DIGIT =			0x00000100,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_SYMBOL =			0x00000200,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_LOWERCASE =		0x00000400,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_UPPERCASE =		0x00000800,
	// http://en.wikipedia.org/wiki/Hiragana
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HIRAGANA =		0x00001000,
	// http://en.wikipedia.org/wiki/Katakana
	// Half-width Katakana
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HALF_KATAKANA =	0x00002000,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_KATAKANA =		0x00004000,
	// http://en.wikipedia.org/wiki/Kanji
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_KANJI =			0x00008000,
	PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_LOWERCASE =		0x00010000,
	PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_UPPERCASE =		0x00020000,
	PSP_UTILITY_OSK_INPUTTYPE_KOREAN =					0x00040000,
	PSP_UTILITY_OSK_INPUTTYPE_URL =						0x00080000
};

/**
 * OSK Field data
 */
typedef struct _SceUtilityOskData
{
    /** Unknown. Pass 0. */
	int unk_00;
	/** Unknown. Pass 0. */
    int unk_04;
	/** One of ::SceUtilityOskInputLanguage */
    int language;
	/** Unknown. Pass 0. */
    int unk_12;
	/** One or more of ::SceUtilityOskInputType (types that are selectable by pressing SELECT) */
    int inputtype;
	/** Number of lines */
    int lines;
	/** Unknown. Pass 0. */
    int unk_24;
	/** Description text */
    u32 descPtr;
	/** Initial text */
    u32 intextPtr;
	/** Length of output text */
    int outtextlength;
	/** Pointer to the output text */
	u32 outtextPtr;
	/** Result. One of ::SceUtilityOskResult */
    int result;
	/** The max text that can be input */
    int outtextlimit;
	
} SceUtilityOskData;

/**
 * OSK parameters
 */
typedef struct _SceUtilityOskParams
{
	pspUtilityDialogCommon base;
	int datacount;		/** Number of input fields */
	u32 SceUtilityOskDataPtr; /** Pointer to the start of the data for the input fields */
	int state;			/** The local OSK state, one of ::SceUtilityOskState */
	int unk_60;/** Unknown. Pass 0 */
	
} SceUtilityOskParams;

SceUtilityOskParams oskParams;
SceUtilityOskData oskData;
std::string oskDesc;
std::string oskIntext;
std::string oskOuttext;
int oskParamsAddr;

#define NUMKEYROWS 4
#define KEYSPERROW 11
const char oskKeys[NUMKEYROWS][KEYSPERROW] = 
{
	{'1','2','3','4','5','6','7','8','9','0',}, 
	{'Q','W','E','R','T','Y','U','I','O','P'},
	{'A','S','D','F','G','H','J','K','L'},
	{'Z','X','C','V','B','N','M',},
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
	oskParamsAddr = oskPtr;
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
	PPGeDrawText(oskDesc.c_str(), 480/2, 20, PPGE_ALIGN_CENTER, 0.5f, 0xFFFFFFFF);
	for (int i=0; i<oskData.outtextlimit; i++)
	{
		PPGeDrawText("_", 20 + (i*16), 40, NULL , 0.5f, 0xFFFFFFFF);
	}

	for (int row = 0; row < NUMKEYROWS; row++)
	{
		PPGeDrawText(oskKeys[row], 20, 60 + (25 * row), NULL , 0.6f, 0xFFFFFFFF);
	}
}

void PSPOskDialog::Update()
{
	buttons = __CtrlPeekButtons();
	//__UtilityUpdate();
	if (status == SCE_UTILITY_STATUS_INITIALIZE)
	{
		status = SCE_UTILITY_STATUS_RUNNING;
	}
	else if (status == SCE_UTILITY_STATUS_RUNNING)
	{		
		StartDraw();
		RenderKeyboard();
		PPGeDrawImage(I_CROSS, 200, 220, 20, 20, 0, 0xFFFFFFFF);
		PPGeDrawText("Ignore", 230, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
		// TODO : Dialogs should take control over input and not send them to the game while displaying
		if (IsButtonPressed(CTRL_CROSS))
		{
			status = SCE_UTILITY_STATUS_FINISHED;
		}
		EndDraw();
	}
	else if (status == SCE_UTILITY_STATUS_FINISHED)
	{
		status = SCE_UTILITY_STATUS_SHUTDOWN;
	}
	// just fake the return values to be "000000" as this will work for most cases e.g. when restricted to entering just numbers
	for (int i=0; i<oskData.outtextlimit; i++)
	{
		Memory::Write_U16(0x0030,oskData.outtextPtr + (2*i));
	}

	oskData.outtextlength = oskData.outtextlimit;
	oskParams.base.result= 0;
	oskData.result = PSP_UTILITY_OSK_RESULT_CHANGED;
	Memory::WriteStruct(oskParams.SceUtilityOskDataPtr, &oskData);
	Memory::WriteStruct(oskParamsAddr, &oskParams);
}
