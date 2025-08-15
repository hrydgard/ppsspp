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

#include <cstdint>

#include "Core/Dialog/PSPOskConstants.h"

// WARNING: The encoding of this file is WEIRD and gets destroyed if you save it in MSVC!

// Japanese (Kana) diacritics
static const wchar_t diacritics[2][103] = {
	{L"かがきぎくぐけげこごさざしじすずせぜそぞただちぢつづてでとどはばぱばひびぴびふぶぷぶへべぺべほぼぽぼウヴカガキギクグケゲコゴサザシジスズセゼソゾタダチヂツヅテデトドハバパバヒビピビフブプブヘベペベホボポボ"},
	{L"はぱばぱひぴびぴふぷぶぷへぺべぺほぽぼぽハパバパヒピビピフプブプヘペベペホポボポ"}
};

// Korean (Hangul) consonant
static const wchar_t kor_cons[20] = L"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";

// Korean (Hangul) vowels, Some vowels are not used, they will be spaces
static const wchar_t kor_vowel[22] = L"ㅏㅐㅑㅒㅓㅔㅕㅖㅗ   ㅛㅜ   ㅠㅡ ㅣ";

// Korean (Hangul) vowel Combination key
static const uint8_t kor_vowelCom[21] = {0,8,9,1,8,10,20,8,11,4,13,14,5,13,15,20,13,16,20,18,19};

// Korean (Hangul) last consonant(diacritics)
static const wchar_t kor_lcons[28] = L"ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ";

// Korean (Hangul) last consonant Combination key
static const uint8_t kor_lconsCom[33] = {18,0,2,21,3,4,26,3,5,0,7,8,15,7,9,16,7,10,18,7,11,24,7,12,25,7,13,26,7,14,18,16,17};

// Korean (Hangul) last consonant Separation key
static const uint8_t kor_lconsSpr[33] = {2,1,9,4,4,12,5,4,18,8,8,0,9,8,6,10,8,7,11,8,9,12,8,16,13,8,17,14,8,18,17,17,9};

static const char16_t oskKeys[OSK_KEYBOARD_COUNT][6][14] = {
	{
		// Latin Lowercase
		{u"1234567890-+"},
		{u"qwertyuiop[]"},
		{u"asdfghjkl;@~"},
		{u"zxcvbnm,./?\\"},
	},
	{
		// Latin Uppercase
		{u"!@#$%^&*()_+"},
		{u"QWERTYUIOP{}"},
		{u"ASDFGHJKL:\"`"},
		{u"ZXCVBNM<>/?|"},
	},
	{
		// Hiragana
		{u"あかさたなはまやらわぁゃっ"},
		{u"いきしちにひみ　り　ぃ　　"},
		{u"うくすつぬふむゆるをぅゅ゛"},
		{u"えけせてねへめ　れ　ぇ　゜"},
		{u"おこそとのほもよろんぉょー"},
		{u"・。、「」『』〜     "},
	},
	{
		// Katakana
		{u"アカサタナハマヤラワァャッ"},
		{u"イキシチニヒミ　リ　ィ　　"},
		{u"ウクスツヌフムユルヲゥュ゛"},
		{u"エケセテネヘメ　レ　ェ　゜"},
		{u"オコソトノホモヨロンォョー"},
		{u"・。、「」『』〜     "},
	},
	{
		// Korean(Hangul)
		{u"1234567890-+"},
		{u"ㅃㅉㄸㄲㅆ!@#$%^&"},
		{u"ㅂㅈㄷㄱㅅㅛㅕㅑㅐㅔ[]"},
		{u"ㅁㄴㅇㄹㅎㅗㅓㅏㅣ;@~"},
		{u"ㅋㅌㅊㅍㅠㅜㅡ<>/?|"},
	},
	{
		// Russian Lowercase
		{u"1234567890-+"},
		{u"йцукенгшщзхъ"},
		{u"фывапролджэё"},
		{u"ячсмитьбю/?|"},
	},
	{
		// Russian Uppercase
		{u"!@#$%^&*()_+"},
		{u"ЙЦУКЕНГШЩЗХЪ"},
		{u"ФЫВАПРОЛДЖЭЁ"},
		{u"ЯЧСМИТЬБЮ/?|"},
	},
	{
		// Latin Full-width Lowercase
		{ u"１２３４５６７８９０－＋" },
		{ u"ｑｗｅｒｔｙｕｉｏｐ［］" },
		{ u"ａｓｄｆｇｈｊｋｌ；＠～" },
		{ u"ｚｘｃｖｂｎｍ，．／？￥￥" },
	},
	{
		// Latin Full-width Uppercase
		{ u"！＠＃＄％＾＆＊（）＿＋" },
		{ u"ＱＷＥＲＴＹＵＩＯＰ｛｝" },
		{ u"ＡＳＤＦＧＨＪＫＬ：￥”‘" },
		{ u"ＺＸＣＶＢＮＭ＜＞／？｜" },
	},
};

// Accessors, since for some reason we can't declare the above extern???

const wchar_t *KorCons() { return kor_cons; }
const wchar_t *KorVowel() { return kor_vowel; }
const wchar_t *KorLCons() { return kor_lcons; }
const wchar_t *JapDiacritics(int index) { return diacritics[index]; }
char16_t OskKeyAt(int keyboard, int row, int col) { return oskKeys[keyboard][row][col]; }
