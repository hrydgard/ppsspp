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

#include "ppsspp_config.h"

#include <cctype>
#include <cmath>
#include <algorithm>
#include "Common/Data/Text/I18n.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/Request.h"
#include "Common/Serialize/Serializer.h"

#include "Core/Dialog/PSPOskDialog.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HW/Display.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

// These are rough, it seems to take a long time to init, and probably depends on threads.
// TODO: This takes like 700ms on a PSP but that's annoyingly long.
const static int OSK_INIT_DELAY_US = 300000;
const static int OSK_SHUTDOWN_DELAY_US = 40000;

static std::map<std::string, std::pair<std::string, int>, std::less<>> languageMapping;

const uint8_t numKeyCols[OSK_KEYBOARD_COUNT] = {12, 12, 13, 13, 12, 12, 12, 12, 12};
const uint8_t numKeyRows[OSK_KEYBOARD_COUNT] = {4, 4, 6, 6, 5, 4, 4, 4, 4};

// Korean (Hangul) vowel Combination key
static const uint8_t kor_vowelCom[21] = { 0,8,9,1,8,10,20,8,11,4,13,14,5,13,15,20,13,16,20,18,19 };

// Korean (Hangul) last consonant Combination key
static const uint8_t kor_lconsCom[33] = { 18,0,2,21,3,4,26,3,5,0,7,8,15,7,9,16,7,10,18,7,11,24,7,12,25,7,13,26,7,14,18,16,17 };

// Korean (Hangul) last consonant Separation key
static const uint8_t kor_lconsSpr[33] = { 2,1,9,4,4,12,5,4,18,8,8,0,9,8,6,10,8,7,11,8,9,12,8,16,13,8,17,14,8,18,17,17,9 };

static const std::string_view OskKeyboardNames[] =
{
	"en_US",
	"ja_JP",
	"ko_KR",
	"ru_RU",
	"English Full-width",
};

// This isn't a complete representation of these flags, it just helps ensure we show the right keyboards.
static const int allowedInputFlagsMap[OSK_KEYBOARD_COUNT] = {
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_LATIN_UPPERCASE | PSP_UTILITY_OSK_INPUTTYPE_LATIN_SYMBOL | PSP_UTILITY_OSK_INPUTTYPE_LATIN_DIGIT,
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_LATIN_UPPERCASE | PSP_UTILITY_OSK_INPUTTYPE_LATIN_SYMBOL,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HIRAGANA,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_KATAKANA | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HALF_KATAKANA,
	PSP_UTILITY_OSK_INPUTTYPE_KOREAN,
	PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_UPPERCASE,
	PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_UPPERCASE,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_UPPERCASE | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_SYMBOL | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_DIGIT,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_UPPERCASE | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_SYMBOL,
};
static const int defaultInputFlagsMap[OSK_KEYBOARD_COUNT] = {
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_LATIN_SYMBOL | PSP_UTILITY_OSK_INPUTTYPE_LATIN_DIGIT,
	PSP_UTILITY_OSK_INPUTTYPE_LATIN_UPPERCASE | PSP_UTILITY_OSK_INPUTTYPE_LATIN_SYMBOL,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HIRAGANA,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_KATAKANA | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HALF_KATAKANA,
	PSP_UTILITY_OSK_INPUTTYPE_KOREAN,
	PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_LOWERCASE,
	PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_UPPERCASE,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_LOWERCASE | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_SYMBOL | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_DIGIT,
	PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_UPPERCASE | PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_SYMBOL,
};

PSPOskDialog::PSPOskDialog(UtilityDialogType type) : PSPDialog(type) {
	// This can break all kinds of stuff, changing the decimal point in sprintf for example.
	// Not sure what the intended effect is so commented out for now.
	// setlocale(LC_ALL, "");
}

PSPOskDialog::~PSPOskDialog() {
}

void PSPOskDialog::ConvertUCS2ToUTF8(std::string& _string, const PSPPointer<u16_le>& em_address)
{
	if (!em_address.IsValid())
	{
		_string.clear();
		return;
	}

	const size_t maxLength = 2047;
	char stringBuffer[maxLength + 1];
	char *string = stringBuffer;

	u16_le *input = &em_address[0];
	int c;
	while ((c = *input++) != 0 && string < stringBuffer + maxLength)
	{
		if (c < 0x80)
			*string++ = c;
		else if (c < 0x800) {
			*string++ = 0xC0 | (c >> 6);
			*string++ = 0x80 | (c & 0x3F);
		} else {
			*string++ = 0xE0 | (c >> 12);
			*string++ = 0x80 | ((c >> 6) & 0x3F);
			*string++ = 0x80 | (c & 0x3F);
		}
	}
	*string++ = '\0';
	_string = stringBuffer;
}

void GetWideStringFromPSPPointer(std::u16string& _string, const PSPPointer<u16_le>& em_address)
{
	if (!em_address.IsValid())
	{
		_string.clear();
		return;
	}

	const size_t maxLength = 2047;
	char16_t stringBuffer[maxLength + 1];
	char16_t *string = stringBuffer;

	u16_le *input = &em_address[0];
	int c;
	while ((c = *input++) != 0 && string < stringBuffer + maxLength)
		*string++ = c;
	*string++ = '\0';
	_string = stringBuffer;
}

void PSPOskDialog::ConvertUCS2ToUTF8(std::string& _string, const char16_t *input)
{
	char stringBuffer[2048];
	char *string = stringBuffer;

	int c;
	while ((c = *input++) != 0)
	{
		if (c < 0x80)
			*string++ = c;
		else if (c < 0x800) {
			*string++ = 0xC0 | (c >> 6);
			*string++ = 0x80 | (c & 0x3F);
		} else {
			*string++ = 0xE0 | (c >> 12);
			*string++ = 0x80 | ((c >> 6) & 0x3F);
			*string++ = 0x80 | (c & 0x3F);
		}
	}
	*string++ = '\0';
	_string = stringBuffer;
}

static void FindValidKeyboard(s32 inputType, int direction, OskKeyboardLanguage &lang, OskKeyboardDisplay &disp) {
	OskKeyboardLanguage origLang = lang;
	OskKeyboardDisplay origDisp = disp;

	if (inputType == 0) {
		return;
	}
	// We use direction = 0 for default, but we actually move "forward".
	const int *matchMap = allowedInputFlagsMap;
	if (direction == 0) {
		direction = 1;
		matchMap = defaultInputFlagsMap;
	}

	// TODO: Limit by allowed keyboards properly... this is just an approximation.
	int tries = OSK_LANGUAGE_COUNT * 2;
	while (!(inputType & matchMap[disp]) && tries > 0) {
		if ((--tries % 2) == 0) {
			lang = (OskKeyboardLanguage)((OSK_LANGUAGE_COUNT + lang + direction) % OSK_LANGUAGE_COUNT);
			disp = OskKeyboardCases[lang][LOWERCASE];
		} else {
			disp = OskKeyboardCases[lang][UPPERCASE];
		}
	}

	if (tries == 0) {
		// In case of error, let's just fall back to allowing all.
		lang = origLang;
		disp = origDisp;
	}
}

static bool IsKeyboardShiftValid(s32 inputType, OskKeyboardLanguage lang, OskKeyboardDisplay disp) {
	// Swap disp and check if it's valid.
	if (disp == OskKeyboardCases[lang][UPPERCASE])
		disp = OskKeyboardCases[lang][LOWERCASE];
	else
		disp = OskKeyboardCases[lang][UPPERCASE];

	return inputType == 0 || (inputType & allowedInputFlagsMap[disp]) != 0;
}

int PSPOskDialog::Init(u32 oskPtr) {
	// Ignore if already running
	if (GetStatus() != SCE_UTILITY_STATUS_NONE) {
		ERROR_LOG_REPORT(Log::sceUtility, "sceUtilityOskInitStart: invalid status");
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}
	// Seems like this should crash?
	if (!Memory::IsValidAddress(oskPtr)) {
		ERROR_LOG_REPORT(Log::sceUtility, "sceUtilityOskInitStart: invalid params (%08x)", oskPtr);
		return -1;
	}

	oskParams = oskPtr;
	if (oskParams->base.size != sizeof(SceUtilityOskParams))
	{
		ERROR_LOG_REPORT(Log::sceUtility, "sceUtilityOskInitStart: invalid size %d", oskParams->base.size);
		return SCE_ERROR_UTILITY_INVALID_PARAM_SIZE;
	}
	// Also seems to crash.
	if (!oskParams->fields.IsValid())
	{
		ERROR_LOG_REPORT(Log::sceUtility, "sceUtilityOskInitStart: invalid field data (%08x)", oskParams->fields.ptr);
		return -1;
	}

	if (oskParams->unk_60 != 0)
		WARN_LOG_REPORT(Log::sceUtility, "sceUtilityOskInitStart: unknown param is non-zero (%08x)", oskParams->unk_60);
	if (oskParams->fieldCount != 1)
		WARN_LOG_REPORT(Log::sceUtility, "sceUtilityOskInitStart: unsupported field count %d", oskParams->fieldCount);

	ChangeStatusInit(OSK_INIT_DELAY_US);
	selectedChar = 0;
	currentKeyboardLanguage = OSK_LANGUAGE_ENGLISH;
	currentKeyboard = OSK_KEYBOARD_LATIN_LOWERCASE;
	FindValidKeyboard(oskParams->fields[0].inputtype, 0, currentKeyboardLanguage, currentKeyboard);

	ConvertUCS2ToUTF8(oskDesc, oskParams->fields[0].desc);
	ConvertUCS2ToUTF8(oskIntext, oskParams->fields[0].intext);
	ConvertUCS2ToUTF8(oskOuttext, oskParams->fields[0].outtext);

	i_level = 0;

	inputChars.clear();

	if (oskParams->fields[0].intext.IsValid()) {
		auto src = oskParams->fields[0].intext;
		int c;
		while ((c = *src++) != 0)
			inputChars += c;
	}

	languageMapping = g_Config.GetLangValuesMapping();

	// Eat any keys pressed before the dialog inited.
	UpdateButtons();
	InitCommon();

	std::lock_guard<std::mutex> guard(nativeMutex_);
	nativeStatus_ = PSPOskNativeStatus::IDLE;

	StartFade(true);
	return 0;
}

std::u16string PSPOskDialog::CombinationKorean(bool isInput)
{
	std::u16string string;

	isCombinated = true;

	int selectedRow = selectedChar / numKeyCols[currentKeyboard];
	int selectedCol = selectedChar % numKeyCols[currentKeyboard];

	if (inputChars.size() == 0) {
		wchar_t sw = OskKeyAt(currentKeyboard, selectedRow, selectedCol);

		if (inputChars.size() < FieldMaxLength()) {
			string += sw;

			i_value[0] = GetIndex(KorCons(), sw);

			if(i_value[0] != -1 && isInput == true)
				i_level = 1;
		} else {
			isCombinated = false;
		}
	} else {
		for(u32 i = 0; i < inputChars.size(); i++) {
			if(i + 1 == inputChars.size()) {
				wchar_t sw = OskKeyAt(currentKeyboard, selectedRow, selectedCol);

				if(i_level == 0) {
					string += inputChars[i];
					if (inputChars.size() < FieldMaxLength()) {
						string += sw;

						i_value[0] = GetIndex(KorCons(), sw);

						if(i_value[0] != -1 && isInput == true)
							i_level = 1;
					} else {
						isCombinated = false;
					}
				} else if(i_level == 1) {
					i_value[1] = GetIndex(KorVowel(), sw);

					if(i_value[1] == -1) {
						string += inputChars[i];
						if (inputChars.size() < FieldMaxLength()) {
							string += sw;

							if(isInput == true) {
								i_value[0] = GetIndex(KorCons(), sw);

								if(i_value[0] != -1)
									i_level = 1;
								else
									i_level = 0;
							}
						} else {
							isCombinated = false;
						}
					} else {
						u16 code = 0xAC00 + i_value[0] * 0x24C + i_value[1] * 0x1C;
						string += code;

						if(isInput == true) {
							i_level = 2;
						}
					}
				} else if(i_level == 2) {
					int tmp = GetIndex(KorVowel(), sw);
					if(tmp != -1) {
						int tmp2 = -1;
						for(size_t j = 0; j < sizeof(kor_vowelCom) / 4; j+=3) {
							if(kor_vowelCom[j] == tmp && kor_vowelCom[j + 1] == i_value[1]) {
								tmp2 = kor_vowelCom[j + 2];
								break;
							}
						}
						if(tmp2 != -1) {
							if(isInput == true) {
								i_value[1] = tmp2;
							}

							u16 code = 0xAC00 + i_value[0] * 0x24C + tmp2 * 0x1C;

							string += code;
						} else {
							string += inputChars[i];
							if (inputChars.size() < FieldMaxLength()) {
								string += sw;

								if(isInput == true) {
									i_level = 0;
								}
							} else {
								isCombinated = false;
							}
						}
					} else {
						int tmp2 = GetIndex(KorLCons(), sw);

						if (tmp2 == -1) {
							string += inputChars[i];
							if (inputChars.size() < FieldMaxLength()) {
								string += sw;

								if (isInput == true) {
									i_value[0] = GetIndex(KorCons(), sw);

									if(i_value[0] != -1)
										i_level = 1;
									else
										i_level = 0;
								}
							} else {
								isCombinated = false;
							}
						} else {
							u16 code = 0xAC00 + i_value[0] * 0x24C + i_value[1] * 0x1C + tmp2 + 1;

							string += code;

							if (isInput == true) {
								i_level = 3;
								i_value[2] = tmp2;
							}
						}
					}
				} else if(i_level == 3) {
					int tmp = GetIndex(KorLCons(), sw);
					if(tmp != -1) {
						int tmp2 = -1;
						for(size_t j = 0; j < sizeof(kor_lconsCom) / 4; j+=3) {
							if(kor_lconsCom[j] == tmp && kor_lconsCom[j + 1] == i_value[2]) {
								tmp2 = kor_lconsCom[j + 2];
								break;
							}
						}
						if(tmp2 != -1) {
							if(isInput == true) {
								i_value[2] = tmp2;
							}

							u16 code = 0xAC00 + i_value[0] * 0x24C + tmp2 * 0x1C + i_value[2] + 1;

							string += code;
						} else {
							string += inputChars[i];
							if (inputChars.size() < FieldMaxLength()) {
								string += sw;

								if(isInput == true) {
									i_value[0] = GetIndex(KorCons(), sw);

									if(i_value[0] != -1)
										i_level = 1;
									else
										i_level = 0;
								}
							} else {
								isCombinated = false;
							}
						}
					} else {
						int tmp3 = GetIndex(KorVowel(), sw);
						if (tmp3 == -1) {
							string += inputChars[i];
							if (inputChars.size() < FieldMaxLength()) {
								string += sw;

								if(isInput == true) {
									i_value[0] = GetIndex(KorCons(), sw);

									if(i_value[0] != -1)
										i_level = 1;
									else
										i_level = 0;
								}
							} else {
								isCombinated = false;
							}
						} else {
							if (inputChars.size() < FieldMaxLength()) {
								int tmp2 = -1;
								for(size_t j = 0; j < sizeof(kor_lconsSpr) / 4; j+=3) {
									if(kor_lconsSpr[j] == i_value[2]) {
										tmp2 = (int)j;
										break;
									}
								}
								if(tmp2 != -1) {
									u16 code = 0xAC00 + i_value[0] * 0x24C + i_value[1] * 0x1C + kor_lconsSpr[tmp2 + 1];
									string += code;

									code = 0xAC00 + kor_lconsSpr[tmp2 + 2] * 0x24C + tmp3 * 0x1C;
									string += code;

									if(isInput == true) {
										i_value[0] = kor_lconsSpr[tmp2 + 2];
										i_value[1] = tmp3;
										i_level = 2;
									}
								} else {
									int tmp4 = GetIndex(KorCons(), KorLCons()[i_value[2]]);

									if (tmp4 != -1) {
										u16 code = 0xAC00 + i_value[0] * 0x24C + i_value[1] * 0x1C;

										string += code;

										code = 0xAC00 + tmp4 * 0x24C + tmp3 * 0x1C;

										string += code;

										if(isInput == true) {
											i_value[0] = tmp4;
											i_value[1] = tmp3;
											i_level = 2;
										}
									} else {
										string += inputChars[i];
										string += sw;

										if(isInput == true) {
											i_level = 0;
										}
									}
								}
							} else {
								string += inputChars[i];
								isCombinated = false;
							}
						}
					}
				}
			} else {
				string += inputChars[i];
			}
		}
	}

	return string;
}

std::u16string PSPOskDialog::CombinationString(bool isInput)
{
	std::u16string string;

	isCombinated = false;

	int selectedRow = selectedChar / numKeyCols[currentKeyboard];
	int selectedCol = selectedChar % numKeyCols[currentKeyboard];

	if(currentKeyboard == OSK_KEYBOARD_KOREAN)
	{
		string = CombinationKorean(isInput);
	}
	else
	{
		if(isInput == true)
		{
			i_level = 0;
		}

		if(OskKeyAt(currentKeyboard, selectedRow, selectedCol) == L'゛')
		{
			for(u32 i = 0; i < inputChars.size(); i++)
			{
				if(i + 1 == inputChars.size())
				{
					for(u32 j = 0; j < wcslen(JapDiacritics(0)); j+=2)
					{
						if(inputChars[i] == JapDiacritics(0)[j])
						{
							string += JapDiacritics(0)[j + 1];
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
		else if(OskKeyAt(currentKeyboard, selectedRow, selectedCol) == L'゜')
		{
			for(u32 i = 0; i < inputChars.size(); i++)
			{
				if(i + 1 == inputChars.size())
				{
					for(u32 j = 0; j < wcslen(JapDiacritics(1)); j+=2)
					{
						if(inputChars[i] == JapDiacritics(1)[j])
						{
							string += JapDiacritics(1)[j + 1];
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
				string += OskKeyAt(currentKeyboard, selectedRow, selectedCol);
			}
			isCombinated = true;
		}
	}

	return string;
}

void PSPOskDialog::RemoveKorean()
{
	if(i_level == 1)
	{
		i_level = 0;
	}
	else if(i_level == 2)
	{
		int tmp = -1;
		for(size_t i = 2; i < sizeof(kor_vowelCom) / 4; i+=3)
		{
			if(kor_vowelCom[i] == i_value[1])
			{
				tmp = kor_vowelCom[i - 1];
				break;
			}
		}

		if(tmp != -1)
		{
			i_value[1] = tmp;
			u16 code = 0xAC00 + i_value[0] * 0x24C + i_value[1] * 0x1C;
			inputChars += code;
		}
		else
		{
			i_level = 1;
			inputChars += KorCons()[i_value[0]];
		}
	}
	else if(i_level == 3)
	{
		int tmp = -1;
		for(size_t i = 2; i < sizeof(kor_lconsCom) / 4; i+=3)
		{
			if(kor_lconsCom[i] == i_value[2])
			{
				tmp = kor_lconsCom[i - 1];
				break;
			}
		}

		if(tmp != -1)
		{
			i_value[2] = tmp;
			u16 code = 0xAC00 + i_value[0] * 0x24C + i_value[1] * 0x1C + i_value[2] + 1;
			inputChars += code;
		}
		else
		{
			i_level = 2;
			u16 code = 0xAC00 + i_value[0] * 0x24C + i_value[1] * 0x1C;
			inputChars += code;
		}
	}
}

int PSPOskDialog::GetIndex(const wchar_t* src, wchar_t ch)
{
	for(int i = 0, end = (int)wcslen(src); i < end; i++)
	{
		if(src[i] == ch)
		{
			return i;
		}
	}

	return -1;
}

u32 PSPOskDialog::FieldMaxLength()
{
	if ((oskParams->fields[0].outtextlimit > oskParams->fields[0].outtextlength - 1) || oskParams->fields[0].outtextlimit == 0)
		return oskParams->fields[0].outtextlength - 1;
	return oskParams->fields[0].outtextlimit;
}

void PSPOskDialog::RenderKeyboard()
{
	// Sanity check that a valid keyboard is selected.
	if ((int)currentKeyboard < 0 || (int)currentKeyboard >= OSK_KEYBOARD_COUNT) {
		return;
	}

	int selectedRow = selectedChar / numKeyCols[currentKeyboard];
	int selectedCol = selectedChar % numKeyCols[currentKeyboard];

	char16_t temp[2];
	temp[1] = '\0';

	std::string buffer;

	static const u32 FIELDDRAWMAX = 16;
	u32 limit = FieldMaxLength();
	u32 drawLimit = std::min(FIELDDRAWMAX, limit);   // Field drew length limit.

	const float keyboardLeftSide = (480.0f - (24.0f * numKeyCols[currentKeyboard])) / 2.0f;
	const float characterWidth = 12.0f;
	float previewLeftSide = (480.0f - (12.0f * drawLimit)) / 2.0f;
	float title = (480.0f - (0.5f * drawLimit)) / 2.0f;

	PPGeStyle descStyle = FadedStyle(PPGeAlign::BOX_CENTER, 0.5f);
	PPGeDrawText(oskDesc, title, 20, descStyle);

	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_HCENTER, 0.5f);

	PPGeStyle keyStyle = FadedStyle(PPGeAlign::BOX_HCENTER, 0.6f);
	PPGeStyle selectedKeyStyle = FadedStyle(PPGeAlign::BOX_HCENTER, 0.6f);
	selectedKeyStyle.color = CalcFadedColor(0xFF3060FF);

	std::u16string result;

	result = CombinationString(false);

	u32 drawIndex = (u32)(result.size() > drawLimit ? result.size() - drawLimit : 0);
	drawIndex = result.size() == limit + 1 ? drawIndex - 1 : drawIndex;  // When the length reached limit, the last character don't fade in and out.
	for (u32 i = 0; i < drawLimit; ++i, ++drawIndex)
	{
		if (drawIndex + 1 < result.size())
		{
			temp[0] = result[drawIndex];
			ConvertUCS2ToUTF8(buffer, temp);
			PPGeDrawText(buffer, previewLeftSide + (i * characterWidth), 40.0f, textStyle);
		}
		else
		{
			if (drawIndex + 1 == result.size())
			{
				temp[0] = result[drawIndex];

				if(isCombinated == true)
				{
					float animStep = (float)(__DisplayGetNumVblanks() % 40) / 20.0f;
					// Fade in and out the next character so they know it's not part of the string yet.
					u32 alpha = (0.5f - (cosf(animStep * M_PI) / 2.0f)) * 128 + 127;
					PPGeStyle animStyle = textStyle;
					animStyle.color = CalcFadedColor((alpha << 24) | 0x00FFFFFF);

					ConvertUCS2ToUTF8(buffer, temp);

					PPGeDrawText(buffer, previewLeftSide + (i * characterWidth), 40.0f, animStyle);

					// Also draw the underline for the same reason.
					PPGeDrawText("_", previewLeftSide + (i * characterWidth), 40.0f, textStyle);
				}
				else
				{
					ConvertUCS2ToUTF8(buffer, temp);
					PPGeDrawText(buffer, previewLeftSide + (i * characterWidth), 40.0f, textStyle);
				}
			}
			else
			{
				PPGeDrawText("_", previewLeftSide + (i * characterWidth), 40.0f, textStyle);
			}
		}
	}

	for (int row = 0; row < numKeyRows[currentKeyboard]; ++row)
	{
		for (int col = 0; col < numKeyCols[currentKeyboard]; ++col)
		{
			temp[0] = OskKeyAt(currentKeyboard, row, col);

			ConvertUCS2ToUTF8(buffer, temp);

			if (selectedRow == row && col == selectedCol) {
				PPGeDrawText(buffer, keyboardLeftSide + (25.0f * col) + characterWidth / 2.0, 70.0f + (25.0f * row), selectedKeyStyle);
				PPGeDrawText("_", keyboardLeftSide + (25.0f * col) + characterWidth / 2.0, 70.0f + (25.0f * row), keyStyle);
			} else {
				PPGeDrawText(buffer, keyboardLeftSide + (25.0f * col) + characterWidth / 2.0, 70.0f + (25.0f * row), keyStyle);
			}
		}
	}
}

// TODO: Why does this have a 2 button press lag/delay when
// re-opening the dialog box? I don't get it.
int PSPOskDialog::NativeKeyboard() {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

#if defined(USING_WIN_UI) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID)
	bool beginInputBox = false;
	if (nativeStatus_ == PSPOskNativeStatus::IDLE) {
		std::lock_guard<std::mutex> guard(nativeMutex_);
		if (nativeStatus_ == PSPOskNativeStatus::IDLE) {
			nativeStatus_ = PSPOskNativeStatus::WAITING;
			beginInputBox = true;
		}
	}

	if (beginInputBox) {
		std::u16string titleText;
		GetWideStringFromPSPPointer(titleText, oskParams->fields[0].desc);

		std::u16string defaultText;
		GetWideStringFromPSPPointer(defaultText, oskParams->fields[0].intext);

		if (defaultText.empty())
			defaultText.assign(u"VALUE");

		// There's already ConvertUCS2ToUTF8 in this file. Should we use that instead of the global ones?
		System_InputBoxGetString(NON_EPHEMERAL_TOKEN, ::ConvertUCS2ToUTF8(titleText), ::ConvertUCS2ToUTF8(defaultText), false,
			[&](const std::string &value, int) {
				// Success callback
				std::lock_guard<std::mutex> guard(nativeMutex_);
				if (nativeStatus_ != PSPOskNativeStatus::WAITING) {
					return;
				}
				nativeValue_ = value;
				nativeStatus_ = PSPOskNativeStatus::SUCCESS;
			},
			[&]() {
				// Failure callback
				std::lock_guard<std::mutex> guard(nativeMutex_);
				if (nativeStatus_ != PSPOskNativeStatus::WAITING) {
					return;
				}
				nativeValue_ = "";
				nativeStatus_ = PSPOskNativeStatus::FAILURE;
			}
		);
	} else if (nativeStatus_ == PSPOskNativeStatus::SUCCESS) {
		inputChars = ConvertUTF8ToUCS2(nativeValue_);
		nativeValue_.clear();

		u32 maxLength = FieldMaxLength();
		if (inputChars.length() > maxLength) {
			ERROR_LOG(Log::sceUtility, "NativeKeyboard: input text too long(%d characters/glyphs max), truncating to game-requested length.", maxLength);
			inputChars.erase(maxLength, std::string::npos);
		}
		ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
		nativeStatus_ = PSPOskNativeStatus::DONE;
	} else if (nativeStatus_ == PSPOskNativeStatus::FAILURE) {
		ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
		nativeStatus_ = PSPOskNativeStatus::DONE;
	}
#endif
	
	u16_le *outText = oskParams->fields[0].outtext;

	size_t end = oskParams->fields[0].outtextlength;
	if (end > inputChars.size())
		end = inputChars.size() + 1;
	// Only write the bytes of the output and the null terminator, don't write the rest.
	for (size_t i = 0; i < end; ++i) {
		u16 value = 0;
		if (i < FieldMaxLength() && i < inputChars.size())
			value = inputChars[i];
		outText[i] = value;
	}

	oskParams->base.result = 0;
	oskParams->fields[0].result = PSP_UTILITY_OSK_RESULT_CHANGED;

	return 0;
}

int PSPOskDialog::Update(int animSpeed) {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	int cancelButton = GetCancelButton();
	int confirmButton = GetConfirmButton();

	static int cancelBtnFramesHeld = 0;
	static int confirmBtnFramesHeld = 0;
	static int leftBtnFramesHeld = 0;
	static int upBtnFramesHeld = 0;
	static int downBtnFramesHeld = 0;
	static int rightBtnFramesHeld = 0;
	const int framesHeldThreshold = 10;
	const int framesHeldRepeatRate = 5;

	UpdateButtons();
	UpdateCommon();
	int selectedRow = selectedChar / numKeyCols[currentKeyboard];
	int selectedExtra = selectedChar % numKeyCols[currentKeyboard];

#if defined(USING_WIN_UI) || defined(USING_QT_UI) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH)
	// Windows: Fall back to the OSK/continue normally if we're in fullscreen.
	// The dialog box doesn't work right if in fullscreen.
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD)) {
		if (g_Config.bBypassOSKWithKeyboard && !g_Config.UseFullScreen())
			return NativeKeyboard();
	}
#endif

	UpdateFade(animSpeed);

	StartDraw();
	PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0x63636363));
	RenderKeyboard();

	auto di = GetI18NCategory(I18NCat::DIALOG);

	PPGeStyle actionStyle = FadedStyle(PPGeAlign::BOX_LEFT, 0.5f);
	PPGeStyle guideStyle = FadedStyle(PPGeAlign::BOX_LEFT, 0.6f);

	PPGeDrawImage(ImageID("I_SQUARE"), 365, 222, 16, 16, guideStyle);
	PPGeDrawText(di->T("Space"), 390, 222, actionStyle);

	if (GetConfirmButton() != CTRL_CIRCLE) {
		PPGeDrawImage(ImageID("I_CROSS"), 45, 222, 16, 16, guideStyle);
		PPGeDrawImage(ImageID("I_CIRCLE"), 45, 247, 16, 16, guideStyle);
	} else {
		PPGeDrawImage(ImageID("I_CIRCLE"), 45, 222, 16, 16, guideStyle);
		PPGeDrawImage(ImageID("I_CROSS"), 45, 247, 16, 16, guideStyle);
	}

	PPGeDrawText(di->T("Select"), 75, 222, actionStyle);
	PPGeDrawText(di->T("Delete"), 75, 247, actionStyle);

	PPGeDrawText("Start", 135, 220, guideStyle);
	PPGeDrawText(di->T("Finish"), 185, 222, actionStyle);

	auto lookupLangName = [&](int direction) {
		// First, find the valid one...
		OskKeyboardLanguage lang = (OskKeyboardLanguage)((OSK_LANGUAGE_COUNT + currentKeyboardLanguage + direction) % OSK_LANGUAGE_COUNT);
		OskKeyboardDisplay disp = OskKeyboardCases[lang][LOWERCASE];
		FindValidKeyboard(oskParams->fields[0].inputtype, direction, lang, disp);

		if (lang == currentKeyboardLanguage) {
			return (const char *)nullptr;
		}

		// Now, let's grab the name.
		std::string_view countryCode = OskKeyboardNames[lang];
		const char *language = languageMapping[std::string(countryCode)].first.c_str();

		// It seems like this is a "fake" country code for extra keyboard purposes.
		if (countryCode == "English Full-width")
			language = "English Full-width";

		return language;
	};

	if (OskKeyboardNames[currentKeyboardLanguage] != "ko_KR" && IsKeyboardShiftValid(oskParams->fields[0].inputtype, currentKeyboardLanguage, currentKeyboard)) {
		PPGeDrawText("Select", 135, 245, guideStyle);
		PPGeDrawText(di->T("Shift"), 185, 247, actionStyle);
	}

	const char *prevLang = lookupLangName(-1);
	if (prevLang) {
		PPGeDrawText("L", 235, 220, guideStyle);
		PPGeDrawText(prevLang, 255, 222, actionStyle);
	}

	const char *nextLang = lookupLangName(1);
	if (nextLang) {
		PPGeDrawText("R", 235, 245, guideStyle);
		PPGeDrawText(nextLang, 255, 247, actionStyle);
	}

	if (IsButtonPressed(CTRL_UP) || IsButtonHeld(CTRL_UP, upBtnFramesHeld, framesHeldThreshold, framesHeldRepeatRate)) {
		selectedChar -= numKeyCols[currentKeyboard];
	} else if (IsButtonPressed(CTRL_DOWN) || IsButtonHeld(CTRL_DOWN, downBtnFramesHeld, framesHeldThreshold, framesHeldRepeatRate)) {
		selectedChar += numKeyCols[currentKeyboard];
	} else if (IsButtonPressed(CTRL_LEFT) || IsButtonHeld(CTRL_LEFT, leftBtnFramesHeld, framesHeldThreshold, framesHeldRepeatRate)) {
		selectedChar--;
		if (((selectedChar + numKeyCols[currentKeyboard]) % numKeyCols[currentKeyboard]) == numKeyCols[currentKeyboard] - 1)
			selectedChar += numKeyCols[currentKeyboard];
	} else if (IsButtonPressed(CTRL_RIGHT) || IsButtonHeld(CTRL_RIGHT, rightBtnFramesHeld, framesHeldThreshold, framesHeldRepeatRate)) {
		selectedChar++;
		if ((selectedChar % numKeyCols[currentKeyboard]) == 0)
			selectedChar -= numKeyCols[currentKeyboard];
	}

	selectedChar = (selectedChar + (numKeyCols[currentKeyboard] * numKeyRows[currentKeyboard])) % (numKeyCols[currentKeyboard] * numKeyRows[currentKeyboard]);

	if (IsButtonPressed(confirmButton) || IsButtonHeld(confirmButton, confirmBtnFramesHeld, framesHeldThreshold, framesHeldRepeatRate)) {
		inputChars = CombinationString(true);
	} else if (IsButtonPressed(CTRL_SELECT)) {
		// Select now swaps case.
		if (IsKeyboardShiftValid(oskParams->fields[0].inputtype, currentKeyboardLanguage, currentKeyboard)) {
			if (currentKeyboard == OskKeyboardCases[currentKeyboardLanguage][UPPERCASE])
				currentKeyboard = OskKeyboardCases[currentKeyboardLanguage][LOWERCASE];
			else
				currentKeyboard = OskKeyboardCases[currentKeyboardLanguage][UPPERCASE];
		}

		if (selectedRow >= numKeyRows[currentKeyboard]) {
			selectedRow = numKeyRows[currentKeyboard] - 1;
		}

		if (selectedExtra >= numKeyCols[currentKeyboard]) {
			selectedExtra = numKeyCols[currentKeyboard] - 1;
		}

		selectedChar = selectedRow * numKeyCols[currentKeyboard] + selectedExtra;
	} else if (IsButtonPressed(CTRL_RTRIGGER)) {
		// RTRIGGER now cycles languages forward.
		currentKeyboardLanguage = (OskKeyboardLanguage)((currentKeyboardLanguage + 1) % OSK_LANGUAGE_COUNT);
		currentKeyboard = OskKeyboardCases[currentKeyboardLanguage][LOWERCASE];
		FindValidKeyboard(oskParams->fields[0].inputtype, 1, currentKeyboardLanguage, currentKeyboard);

		if (selectedRow >= numKeyRows[currentKeyboard]) {
			selectedRow = numKeyRows[currentKeyboard] - 1;
		}

		if (selectedExtra >= numKeyCols[currentKeyboard]) {
			selectedExtra = numKeyCols[currentKeyboard] - 1;
		}

		selectedChar = selectedRow * numKeyCols[currentKeyboard] + selectedExtra;
	} else if (IsButtonPressed(CTRL_LTRIGGER)) {
		// LTRIGGER now cycles languages backward.
		if (currentKeyboardLanguage - 1 >= 0)
			currentKeyboardLanguage = (OskKeyboardLanguage)((currentKeyboardLanguage - 1) % OSK_LANGUAGE_COUNT);
		else
			currentKeyboardLanguage = (OskKeyboardLanguage)(OSK_LANGUAGE_COUNT - 1);
		currentKeyboard = OskKeyboardCases[currentKeyboardLanguage][LOWERCASE];
		FindValidKeyboard(oskParams->fields[0].inputtype, -1, currentKeyboardLanguage, currentKeyboard);

		if (selectedRow >= numKeyRows[currentKeyboard]) {
			selectedRow = numKeyRows[currentKeyboard] - 1;
		}

		if (selectedExtra >= numKeyCols[currentKeyboard]) {
			selectedExtra = numKeyCols[currentKeyboard] - 1;
		}

		selectedChar = selectedRow * numKeyCols[currentKeyboard] + selectedExtra;
	} else if (IsButtonPressed(cancelButton) || IsButtonHeld(cancelButton, cancelBtnFramesHeld, framesHeldThreshold, framesHeldRepeatRate)) {
		if (inputChars.size() > 0) {
			inputChars.resize(inputChars.size() - 1);
			if (i_level != 0) {
				RemoveKorean();
			}
		}
	} else if (IsButtonPressed(CTRL_START)) {
		StartFade(false);
	} else if (IsButtonPressed(CTRL_SQUARE) && inputChars.size() < FieldMaxLength()) {
		// Use a regular space if the current keyboard isn't Japanese nor full-width English
		if (currentKeyboardLanguage != OSK_LANGUAGE_JAPANESE && currentKeyboardLanguage != OSK_LANGUAGE_ENGLISH_FW)
			inputChars += u" ";
		else
			inputChars += u"　";
	}

	EndDraw();

	u16_le *outText = oskParams->fields[0].outtext;
	size_t end = oskParams->fields[0].outtextlength;
	// Only write the bytes of the output and the null terminator, don't write the rest.
	if (end > inputChars.size())
		end = inputChars.size() + 1;
	for (size_t i = 0; i < end; ++i)
	{
		u16 value = 0;
		if (i < FieldMaxLength() && i < inputChars.size())
			value = inputChars[i];
		outText[i] = value;
	}

	oskParams->base.result = 0;
	oskParams->fields[0].result = PSP_UTILITY_OSK_RESULT_CHANGED;
	return 0;
}

int PSPOskDialog::Shutdown(bool force)
{
	if (GetStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(OSK_SHUTDOWN_DELAY_US);
	}
	nativeStatus_ = PSPOskNativeStatus::IDLE;

	return 0;
}

void PSPOskDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);

	auto s = p.Section("PSPOskDialog", 1, 2);
	if (!s)
		return;

	// TODO: Should we save currentKeyboard/currentKeyboardLanguage?

	Do(p, oskParams);
	Do(p, oskDesc);
	Do(p, oskIntext);
	Do(p, oskOuttext);
	Do(p, selectedChar);
	if (s >= 2) {
		Do(p, inputChars);
	} else {
		// Discard the wstring.
		std::wstring wstr;
		Do(p, wstr);
	}
	// Don't need to save state native status or value.
}

pspUtilityDialogCommon *PSPOskDialog::GetCommonParam()
{
	return &oskParams->base;
}
