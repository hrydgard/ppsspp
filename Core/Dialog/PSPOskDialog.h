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

#pragma once

#include "PSPDialog.h"
#include "../Core/MemMap.h"



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
struct SceUtilityOskData
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
	PSPPointer<u16> desc;
	/** Initial text */
	PSPPointer<u16> intext;
	// Length, in unsigned shorts, including the terminator.
	u32 outtextlength;
	/** Pointer to the output text */
	PSPPointer<u16> outtext;
	/** Result. One of ::SceUtilityOskResult */
	int result;
	// Number of characters to allow, not including terminator (if less than outtextlength - 1.)
	u32 outtextlimit;
};

// Parameters to sceUtilityOskInitStart
struct SceUtilityOskParams
{
	pspUtilityDialogCommon base;
	// Number of fields.
	int fieldCount;
	// Pointer to an array of fields (see SceUtilityOskData.)
	PSPPointer<SceUtilityOskData> fields;
	SceUtilityOskState state;
	// Maybe just padding?
	int unk_60;

};

// Internal enum, not from PSP.
enum OskKeyboardDisplay
{
	OSK_KEYBOARD_LATIN_LOWERCASE,
	OSK_KEYBOARD_LATIN_UPPERCASE,
	OSK_KEYBOARD_HIRAGANA,
	OSK_KEYBOARD_KATAKANA,
	OSK_KEYBOARD_KOREAN,
	OSK_KEYBOARD_RUSSIAN_LOWERCASE,
	OSK_KEYBOARD_RUSSIAN_UPPERCASE,
	// TODO: Something to do native?
	OSK_KEYBOARD_COUNT
};


class PSPOskDialog: public PSPDialog {
public:
	PSPOskDialog();
	virtual ~PSPOskDialog();

	virtual int Init(u32 oskPtr);
	virtual int Update();
	virtual int Shutdown(bool force = false);
	virtual void DoState(PointerWrap &p);
	virtual pspUtilityDialogCommon *GetCommonParam();

private:
	void ConvertUCS2ToUTF8(std::string& _string, const PSPPointer<u16> em_address);
	void ConvertUCS2ToUTF8(std::string& _string, const wchar_t *input);
	void RenderKeyboard();

	std::wstring CombinationString(bool isInput); // for Japanese, Korean
	std::wstring CombinationKorean(bool isInput); // for Korea
	void RemoveKorean(); // for Korean character removal
	
	u32 FieldMaxLength();
	int GetIndex(const wchar_t* src, wchar_t ch);

	PSPPointer<SceUtilityOskParams> oskParams;
	std::string oskDesc;
	std::string oskIntext;
	std::string oskOuttext;

	int selectedChar;
	std::wstring inputChars;
	OskKeyboardDisplay currentKeyboard;
	bool isCombinated;

	int i_level; // for Korean Keyboard support
	int i_value[3]; // for Korean Keyboard support
};

