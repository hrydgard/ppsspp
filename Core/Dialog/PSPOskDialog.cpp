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

const int numKeyCols[OSK_KEYBOARD_COUNT] = {12, 12, 13, 13};
const int numKeyRows[OSK_KEYBOARD_COUNT] = {4, 4, 5, 5};

// Japanese(Kana) diacritics
static const wchar_t diacritics[2][103] =
{
	{L"かがきぎくぐけげこごさざしじすずせぜそぞただちぢつづてでとどはばぱばひびぴびふぶぷぶへべぺべほぼぽぼウヴカガキギクグケゲコゴサザシジスズセゼソゾタダチヂツヅテデトドハバパバヒビピビフブプブヘベペベホボポボ"},
	{L"はぱばぱひぴびぴふぷぶぷへぱべぱほぽぼぽハパバパヒピビピフプブプヘパベパホポボポ"}
};

// Korean(Hangul) consonant
static const wchar_t kor_cons[] = L"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";

// Korean(Hangul) bowels, Some bowels are not used, them will be spacing
static const wchar_t kor_vowel[] = L"ㅏㅐㅑㅒㅓㅔㅕㅖㅗ   ㅛㅜ   ㅠㅡ ㅣ";


static const wchar_t oskKeys[OSK_KEYBOARD_COUNT][5][14] =
{
	{
		// Latin Lowercase
		{L"1234567890-+"},
		{L"qwertyuiop[]"},
		{L"asdfghjkl;@~"},
		{L"zxcvbnm,./?\\"},
	},
	{
		// Latin Uppercase
		{L"!@#$%^&*()_+"},
		{L"QWERTYUIOP{}"},
		{L"ASDFGHJKL:\"`"},
		{L"ZXCVBNM<>/?|"},
	},
	{
		// Hiragana
		{L"あかさたなはまやらわぁゃっ"},
		{L"いきしちにひみ　り　ぃ　　"},
		{L"うくすつぬふむゆるをぅゅ˝"},
		{L"えけせてねへめ　れ　ぇ　˚"},
		{L"おこそとのほもよるんぉょ　"},
	},
	{
		// Katakana
		{L"アカサタナハマヤラワァャッ"},
		{L"イキシチニヒミ　リ　ィ　　"},
		{L"ウクスツヌフムユルヲゥュ˝"},
		{L"エケセテネヘメ　レ　ェ　˚"},
		{L"オコソトノホモヨルンォョ　"},
	},
	/*
	{
		// Korean(Hangul) Lowercase
		{L"1234567890-+"},
		{L"ㅂㅈㄷㄱㅅㅛㅕㅑㅐㅔ[]"},
		{L"ㅁㄴㅇㄹㅎㅗㅓㅏㅣ;@~"},
		{L"ㅋㅌㅊㅍㅠㅜㅡ<>/?|"},
	},
	{
		// Korean(Hangul) Uppercase
		{L"!@#$%^&*()_+"},
		{L"ㅃㅉㄸㄲㅆㅛㅕㅑㅒㅖ{}"},
		{L"ㅁㄴㅇㄹㅎㅗㅓㅏㅣ:\"`"},
		{L"ㅋㅌㅊㅍㅠㅜㅡ<>/?|"},
	},
	*/
};


PSPOskDialog::PSPOskDialog() : PSPDialog() {
	setlocale(LC_ALL, "");
}

PSPOskDialog::~PSPOskDialog() {
}

void PSPOskDialog::ConvertUCS2ToUTF8(std::string& _string, const u32 em_address)
{
	char stringBuffer[2048];
	char *string = stringBuffer;

	if (em_address == 0)
	{
		_string = "";
		return;
	}

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

void PSPOskDialog::ConvertUCS2ToUTF8(std::string& _string, wchar_t* input)
{
	char stringBuffer[2048];
	char *string = stringBuffer;

	int c;
	while (c = *input++)
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

	inputChars = L"";

	u16 *src = (u16 *) Memory::GetPointer(oskData.intextPtr);
	int c;
	while (c = *src++)
	{
		inputChars += c;
		if(c == 0x00)
		{
			break;
		}
	}

	// Eat any keys pressed before the dialog inited.
	__CtrlReadLatch();

	StartFade(true);
	return 0;
}

std::wstring PSPOskDialog::CombinationString()
{
	std::wstring string;

	isCombinated = false;

	int selectedRow = selectedChar / numKeyCols[currentKeyboard];
	int selectedCol = selectedChar % numKeyCols[currentKeyboard];

	if(oskKeys[currentKeyboard][selectedRow][selectedCol] == L'˝')
	{
		for(u32 i = 0; i < inputChars.size(); i++)
		{
			if(i + 1 == inputChars.size())
			{
				for(u32 j = 0; j < wcslen(diacritics[0]); j+=2)
				{
					if(inputChars[i] == diacritics[0][j])
					{
						string += diacritics[0][j + 1];
						isCombinated = true;
						break;
					}
				}

				if(isCombinated == false)
				{
					string += inputChars[i];
				}
			}
			else
			{
				string += inputChars[i];
			}
		}
	}
	else if(oskKeys[currentKeyboard][selectedRow][selectedCol] == L'˚')
	{
		for(u32 i = 0; i < inputChars.size(); i++)
		{
			if(i + 1 == inputChars.size())
			{
				for(u32 j = 0; j < wcslen(diacritics[1]); j+=2)
				{
					if(inputChars[i] == diacritics[1][j])
					{
						string += diacritics[1][j + 1];
						isCombinated = true;
						break;
					}
				}

				if(isCombinated == false)
				{
					string += inputChars[i];
				}
			}
			else
			{
				string += inputChars[i];
			}
		}
	}
	else
	{
		for(u32 i = 0; i < inputChars.size(); i++)
		{
			string += inputChars[i];
		}

		if (string.size() < FieldMaxLength())
		{
			string += oskKeys[currentKeyboard][selectedRow][selectedCol];
		}
		isCombinated = true;
	}

	return string;
}

u32 PSPOskDialog::FieldMaxLength()
{
	if (oskData.outtextlimit > oskData.outtextlength - 1 || oskData.outtextlimit == 0)
		return oskData.outtextlength - 1;
	return oskData.outtextlimit;
}


void PSPOskDialog::RenderKeyboard()
{
	int selectedRow = selectedChar / numKeyCols[currentKeyboard];
	int selectedCol = selectedChar % numKeyCols[currentKeyboard];

	wchar_t temp[2];
	temp[1] = '\0';

	std::string buffer;

	u32 limit = FieldMaxLength();

	const float keyboardLeftSide = (480.0f - (24.0f * numKeyCols[currentKeyboard])) / 2.0f;
	const float characterWidth = 12.0f;
	float previewLeftSide = (480.0f - (12.0f * limit)) / 2.0f;
	float title = (480.0f - (0.5f * limit)) / 2.0f;


	PPGeDrawText(oskDesc.c_str(), title , 20, PPGE_ALIGN_CENTER, 0.5f, CalcFadedColor(0xFFFFFFFF));

	std::wstring result;

	result = CombinationString();

	for (u32 i = 0; i < limit; ++i)
	{
		u32 color = CalcFadedColor(0xFFFFFFFF);
		if (i + 1 < result.size())
		{
			temp[0] = result[i];
			ConvertUCS2ToUTF8(buffer, temp);
			PPGeDrawText(buffer.c_str(), previewLeftSide + (i * characterWidth), 40.0f, PPGE_ALIGN_CENTER, 0.5f, color);
		}
		else
		{
			if (i + 1 == result.size())
			{
				temp[0] = result[i];

				if(isCombinated == true)
				{
					float animStep = (float)(gpuStats.numFrames % 40) / 20.0f;
					// Fade in and out the next character so they know it's not part of the string yet.
					u32 alpha = (0.5f - (cosf(animStep * M_PI) / 2.0f)) * 128 + 127;
					color = CalcFadedColor((alpha << 24) | 0xFFFFFF);

					ConvertUCS2ToUTF8(buffer, temp);

					PPGeDrawText(buffer.c_str(), previewLeftSide + (i * characterWidth), 40.0f, PPGE_ALIGN_CENTER, 0.5f, color);

					// Also draw the underline for the same reason.
					color = CalcFadedColor(0xFFFFFFFF);
					PPGeDrawText("_", previewLeftSide + (i * characterWidth), 40.0f, PPGE_ALIGN_CENTER, 0.5f, color);
				}
				else
				{
					ConvertUCS2ToUTF8(buffer, temp);
					PPGeDrawText(buffer.c_str(), previewLeftSide + (i * characterWidth), 40.0f, PPGE_ALIGN_CENTER, 0.5f, color);
				}
			}
			else
			{
				PPGeDrawText("_", previewLeftSide + (i * characterWidth), 40.0f, PPGE_ALIGN_CENTER, 0.5f, color);
			}
		}
	}

	for (int row = 0; row < numKeyRows[currentKeyboard]; ++row)
	{
		for (int col = 0; col < numKeyCols[currentKeyboard]; ++col)
		{
			u32 color = CalcFadedColor(0xFFFFFFFF);
			if (selectedRow == row && col == selectedCol)
				color = CalcFadedColor(0xFF3060FF);

			temp[0] = oskKeys[currentKeyboard][row][col];

			ConvertUCS2ToUTF8(buffer, temp);
			PPGeDrawText(buffer.c_str(), keyboardLeftSide + (25.0f * col) + characterWidth / 2.0, 70.0f + (25.0f * row), PPGE_ALIGN_HCENTER, 0.6f, color);

			if (selectedRow == row && col == selectedCol)
				PPGeDrawText("_", keyboardLeftSide + (25.0f * col) + characterWidth / 2.0, 70.0f + (25.0f * row), PPGE_ALIGN_HCENTER, 0.6f, CalcFadedColor(0xFFFFFFFF));
		}
	}
}

int PSPOskDialog::Update()
{
	buttons = __CtrlReadLatch();
	int selectedRow = selectedChar / numKeyCols[currentKeyboard];
	int selectedExtra = selectedChar % numKeyCols[currentKeyboard];

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

		I18NCategory *d = GetI18NCategory("Dialog");
		PPGeDrawText(d->T("Select"), 60, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(d->T("Delete"), 180, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Start", 245, 220, PPGE_ALIGN_LEFT, 0.6f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(d->T("Finish"), 290, 222, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText("Select", 365, 220, PPGE_ALIGN_LEFT, 0.6f, CalcFadedColor(0xFFFFFFFF));
		// TODO: Show title of next keyboard?
		PPGeDrawText(d->T("Shift"), 415, 222, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));

		if (IsButtonPressed(CTRL_UP))
		{
			selectedChar -= numKeyCols[currentKeyboard];
		}
		else if (IsButtonPressed(CTRL_DOWN))
		{
			selectedChar += numKeyCols[currentKeyboard];
		}
		else if (IsButtonPressed(CTRL_LEFT))
		{
			selectedChar--;
			if (((selectedChar + numKeyCols[currentKeyboard]) % numKeyCols[currentKeyboard]) == numKeyCols[currentKeyboard] - 1)
				selectedChar += numKeyCols[currentKeyboard];
		}
		else if (IsButtonPressed(CTRL_RIGHT))
		{
			selectedChar++;
			if ((selectedChar % numKeyCols[currentKeyboard]) == 0)
				selectedChar -= numKeyCols[currentKeyboard];
		}

		selectedChar = (selectedChar + (numKeyCols[currentKeyboard] * numKeyRows[currentKeyboard])) % (numKeyCols[currentKeyboard] * numKeyRows[currentKeyboard]);

		if (IsButtonPressed(CTRL_CROSS))
		{	
			inputChars = CombinationString();
		}
		else if (IsButtonPressed(CTRL_SELECT))
		{
			// TODO: Limit by allowed keyboards...
			currentKeyboard = (OskKeyboardDisplay)((currentKeyboard + 1) % OSK_KEYBOARD_COUNT);

			if(selectedRow >= numKeyRows[currentKeyboard])
			{
				selectedRow = numKeyRows[currentKeyboard] - 1;
			}

			if(selectedExtra >= numKeyCols[currentKeyboard])
			{
				selectedExtra = numKeyCols[currentKeyboard] - 1;
			}

			selectedChar = selectedRow * numKeyCols[currentKeyboard] + selectedExtra;
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
