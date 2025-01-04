#pragma once

// Internal enum, not from PSP.
enum OskKeyboardDisplay {
	OSK_KEYBOARD_LATIN_LOWERCASE,
	OSK_KEYBOARD_LATIN_UPPERCASE,
	OSK_KEYBOARD_HIRAGANA,
	OSK_KEYBOARD_KATAKANA,
	OSK_KEYBOARD_KOREAN,
	OSK_KEYBOARD_RUSSIAN_LOWERCASE,
	OSK_KEYBOARD_RUSSIAN_UPPERCASE,
	OSK_KEYBOARD_LATIN_FW_LOWERCASE,
	OSK_KEYBOARD_LATIN_FW_UPPERCASE,
	// TODO: Something to do native?
	OSK_KEYBOARD_COUNT
};

const wchar_t *KorCons();
const wchar_t *KorVowel();
const wchar_t *KorLCons();
const wchar_t *JapDiacritics(int index);
char16_t OskKeyAt(int keyboard, int row, int col);
