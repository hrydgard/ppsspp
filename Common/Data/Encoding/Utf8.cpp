/*
  Basic UTF-8 manipulation routines
  by Jeff Bezanson
  placed in the public domain Fall 2005

  This code is designed to provide the utilities you need to manipulate
  UTF-8 as an internal string encoding. These functions do not perform the
  error checking normally needed when handling UTF-8 data, so if you happen
  to be from the Unicode Consortium you will want to flay me alive.
  I do this because error checking can be performed at the boundaries (I/O),
  with these routines reserved for higher performance on data known to be
  valid.
*/

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <algorithm>
#include <string>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Encoding/Utf16.h"
#include "Common/Log.h"

// is start of UTF sequence
inline bool isutf(char c) {
	return (c & 0xC0) != 0x80;
}

static const uint32_t offsetsFromUTF8[6] = {
	0x00000000UL, 0x00003080UL, 0x000E2080UL,
	0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

static const uint8_t trailingBytesForUTF8[256] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5,
};

int u8_wc_toutf8(char *dest, uint32_t ch)
{
  if (ch < 0x80) {
    dest[0] = (char)ch;
    return 1;
  }
  if (ch < 0x800) {
    dest[0] = (ch>>6) | 0xC0;
    dest[1] = (ch & 0x3F) | 0x80;
    return 2;
  }
  if (ch < 0x10000) {
    dest[0] = (ch>>12) | 0xE0;
    dest[1] = ((ch>>6) & 0x3F) | 0x80;
    dest[2] = (ch & 0x3F) | 0x80;
    return 3;
  }
  if (ch < 0x110000) {
    dest[0] = (ch>>18) | 0xF0;
    dest[1] = ((ch>>12) & 0x3F) | 0x80;
    dest[2] = ((ch>>6) & 0x3F) | 0x80;
    dest[3] = (ch & 0x3F) | 0x80;
    return 4;
  }
  return 0;
}

/* charnum => byte offset */
int u8_offset(const char *str, int charnum)
{
  int offs=0;

  while (charnum > 0 && str[offs]) {
    (void)(isutf(str[++offs]) || isutf(str[++offs]) ||
         isutf(str[++offs]) || ++offs);
    charnum--;
  }
  return offs;
}

/* byte offset => charnum */
int u8_charnum(const char *s, int offset)
{
  int charnum = 0, offs=0;

  while (offs < offset && s[offs]) {
    (void)(isutf(s[++offs]) || isutf(s[++offs]) ||
         isutf(s[++offs]) || ++offs);
    charnum++;
  }
  return charnum;
}

/* reads the next utf-8 sequence out of a string, updating an index */
uint32_t u8_nextchar(const char *s, int *index, size_t size) {
	uint32_t ch = 0;
	_dbg_assert_(*index >= 0 && *index < 100000000);
	int sz = 0;
	int i = *index;
	do {
		ch = (ch << 6) + (unsigned char)s[i++];
		sz++;
	} while (i < size && s[i] && ((s[i]) & 0xC0) == 0x80);
	*index = i;
	return ch - offsetsFromUTF8[sz - 1];
}

uint32_t u8_nextchar_unsafe(const char *s, int *i) {
	uint32_t ch = (unsigned char)s[(*i)++];
	int sz = 1;
	if (ch >= 0xF0) {
		sz++;
		ch &= ~0x10;
	}
	if (ch >= 0xE0) {
		sz++;
		ch &= ~0x20;
	}
	if (ch >= 0xC0) {
		sz++;
		ch &= ~0xC0;
	}

	// Just assume the bytes must be there.  This is the logic used on the PSP.
	for (int j = 1; j < sz; ++j) {
		ch <<= 6;
		ch += ((unsigned char)s[(*i)++]) & 0x3F;
	}
	return ch;
}

void u8_inc(const char *s, int *i) {
	(void)(isutf(s[++(*i)]) || isutf(s[++(*i)]) ||
		isutf(s[++(*i)]) || ++(*i));
}

void u8_dec(const char *s, int *i) {
	(void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) ||
		isutf(s[--(*i)]) || --(*i));
}

bool AnyEmojiInString(std::string_view str, size_t byteCount) {
	int i = 0;
	while (i < byteCount) {
		uint32_t c = u8_nextchar(str.data(), &i, str.size());
		if (CodepointIsProbablyEmoji(c)) {
			return true;
		}
	}
	return false;
}

int UTF8StringNonASCIICount(std::string_view utf8string) {
	UTF8 utf(utf8string);
	int count = 0;
	while (!utf.end()) {
		int c = utf.next();
		if (c > 127)
			++count;
	}
	return count;
}

bool UTF8StringHasNonASCII(std::string_view utf8string) {
	return UTF8StringNonASCIICount(utf8string) > 0;
}

#ifdef _WIN32

std::string ConvertWStringToUTF8(const wchar_t *wstr) {
	int len = (int)wcslen(wstr);
	int size = (int)WideCharToMultiByte(CP_UTF8, 0, wstr, len, 0, 0, NULL, NULL);
	std::string s;
	s.resize(size);
	if (size > 0) {
		WideCharToMultiByte(CP_UTF8, 0, wstr, len, &s[0], size, NULL, NULL);
	}
	return s;
}

std::string ConvertWStringToUTF8(const std::wstring &wstr) {
	int len = (int)wstr.size();
	int size = (int)WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), len, 0, 0, NULL, NULL);
	std::string s;
	s.resize(size);
	if (size > 0) {
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), len, &s[0], size, NULL, NULL);
	}
	return s;
}

void ConvertUTF8ToWString(wchar_t *dest, size_t destSize, std::string_view source) {
	int len = (int)source.size();
	destSize -= 1;  // account for the \0.
	int size = (int)MultiByteToWideChar(CP_UTF8, 0, source.data(), len, NULL, 0);
	MultiByteToWideChar(CP_UTF8, 0, source.data(), len, dest, std::min((int)destSize, size));
	dest[size] = 0;
}

std::wstring ConvertUTF8ToWString(const std::string_view source) {
	int len = (int)source.size();
	int size = (int)MultiByteToWideChar(CP_UTF8, 0, source.data(), len, NULL, 0);
	std::wstring str;
	str.resize(size);
	if (size > 0) {
		MultiByteToWideChar(CP_UTF8, 0, source.data(), (int)source.size(), &str[0], size);
	}
	return str;
}

#endif

std::string ConvertUCS2ToUTF8(const std::u16string &wstr) {
	std::string s;
	// Worst case.
	s.resize(wstr.size() * 4);

	size_t pos = 0;
	for (wchar_t c : wstr) {
		pos += UTF8::encode(&s[pos], c);
	}

	s.resize(pos);
	return s;
}

std::string SanitizeUTF8(std::string_view utf8string) {
	UTF8 utf(utf8string);
	std::string s;
	// Worst case.
	s.resize(utf8string.size() * 4);

	// This stops at invalid start bytes.
	size_t pos = 0;
	while (!utf.end() && !utf.invalid()) {
		int c = utf.next_unsafe();
		pos += UTF8::encode(&s[pos], c);
	}
	s.resize(pos);
	return s;
}

static size_t ConvertUTF8ToUCS2Internal(char16_t *dest, size_t destSize, std::string_view source) {
	const char16_t *const orig = dest;
	const char16_t *const destEnd = dest + destSize;

	UTF8 utf(source);

	char16_t *destw = (char16_t *)dest;
	const char16_t *const destwEnd = destw + destSize;

	// Ignores characters outside the BMP.
	while (uint32_t c = utf.next()) {
		if (destw + UTF16LE::encodeUnitsUCS2(c) >= destwEnd) {
			break;
		}
		destw += UTF16LE::encodeUCS2(destw, c);
	}

	// No ++ to not count the null-terminator in length.
	if (destw < destEnd) {
		*destw = 0;
	}

	return destw - orig;
}

std::u16string ConvertUTF8ToUCS2(std::string_view source) {
	std::u16string dst;
	dst.resize(source.size() + 1, 0);  // multiple UTF-8 chars will be one UCS2 char. But we need to leave space for a terminating null.
	size_t realLen = ConvertUTF8ToUCS2Internal(&dst[0], dst.size(), source);
	dst.resize(realLen);
	return dst;
}

std::string CodepointToUTF8(uint32_t codePoint) {
	char temp[16]{};
	UTF8::encode(temp, codePoint);
	return std::string(temp);
}

// Helper function to encode a Unicode code point into UTF-8, but doesn't support 4-byte output.
size_t encode_utf8_modified(uint32_t code_point, unsigned char* output) {
	if (code_point <= 0x7F) {
		output[0] = (unsigned char)code_point;
		return 1;
	} else if (code_point <= 0x7FF) {
		output[0] = (unsigned char)(0xC0 | (code_point >> 6));
		output[1] = (unsigned char)(0x80 | (code_point & 0x3F));
		return 2;
	} else if (code_point <= 0xFFFF) {
		output[0] = (unsigned char)(0xE0 | (code_point >> 12));
		output[1] = (unsigned char)(0x80 | ((code_point >> 6) & 0x3F));
		output[2] = (unsigned char)(0x80 | (code_point & 0x3F));
		return 3;
	}
	return 0;
}

// A function to convert regular UTF-8 to Java Modified UTF-8. Only used on Android.
// Written by ChatGPT and corrected and modified.
void ConvertUTF8ToJavaModifiedUTF8(std::string *output, std::string_view input) {
	output->resize(input.length() * 6); // worst case: every character is encoded as 6 bytes. shouldn't happen, though.
	size_t out_idx = 0;
	for (size_t i = 0; i < input.length(); ) {
		unsigned char c = input[i];
		if (c == 0) {
			// Encode null character as 0xC0 0x80. TODO: We probably don't need to support this?
			output[out_idx++] = (char)0xC0;
			output[out_idx++] = (char)0x80;
			i++;
		} else if ((c & 0xF0) == 0xF0) { // 4-byte sequence (U+10000 to U+10FFFF)
			// Decode the Unicode code point from the UTF-8 sequence
			uint32_t code_point = ((input[i] & 0x07) << 18) |
				((input[i + 1] & 0x3F) << 12) |
				((input[i + 2] & 0x3F) << 6) |
				(input[i + 3] & 0x3F);

			// Convert to surrogate pair
			uint16_t high_surrogate = ((code_point - 0x10000) / 0x400) + 0xD800;
			uint16_t low_surrogate = ((code_point - 0x10000) % 0x400) + 0xDC00;

			// Encode the surrogates in UTF-8. encode_utf8_modified outputs at most 3 bytes.
			out_idx += encode_utf8_modified(high_surrogate, (unsigned char *)(output->data() + out_idx));
			out_idx += encode_utf8_modified(low_surrogate, (unsigned char *)(output->data() + out_idx));

			i += 4;
		} else {
			// Copy the other UTF-8 sequences (1-3 bytes)
			size_t utf8_len = 1;
			if ((c & 0xE0) == 0xC0) {
				utf8_len = 2; // 2-byte sequence
			} else if ((c & 0xF0) == 0xE0) {
				utf8_len = 3; // 3-byte sequence
			}
			memcpy(output->data() + out_idx, input.data() + i, utf8_len);
			out_idx += utf8_len;
			i += utf8_len;
		}
	}
	output->resize(out_idx);
	_dbg_assert_(output->size() >= input.size());
}

#ifndef _WIN32

// Replacements for the Win32 wstring functions. Not to be used from emulation code!

std::string ConvertWStringToUTF8(const std::wstring &wstr) {
	std::string s;
	// Worst case.
	s.resize(wstr.size() * 4);

	size_t pos = 0;
	for (wchar_t c : wstr) {
		pos += UTF8::encode(&s[pos], c);
	}

	s.resize(pos);
	return s;
}

static size_t ConvertUTF8ToWStringInternal(wchar_t *dest, size_t destSize, std::string_view source) {
	const wchar_t *const orig = dest;
	const wchar_t *const destEnd = dest + destSize;

	UTF8 utf(source);

	if (sizeof(wchar_t) == 2) {
		char16_t *destw = (char16_t *)dest;
		const char16_t *const destwEnd = destw + destSize;
		while (char32_t c = utf.next()) {
			if (destw + UTF16LE::encodeUnits(c) >= destwEnd) {
				break;
			}
			destw += UTF16LE::encode(destw, c);
		}
		dest = (wchar_t *)destw;
	} else {
		while (char32_t c = utf.next()) {
			if (dest + 1 >= destEnd) {
				break;
			}
			*dest++ = c;
		}
	}

	// No ++ to not count the terminal in length.
	if (dest < destEnd) {
		*dest = 0;
	}

	return dest - orig;
}

std::wstring ConvertUTF8ToWString(std::string_view source) {
	std::wstring dst;
	// conservative size estimate for wide characters from utf-8 bytes. Will always reserve too much space.
	dst.resize(source.size());
	size_t realLen = ConvertUTF8ToWStringInternal(&dst[0], source.size(), source);
	dst.resize(realLen);  // no need to write a NUL, it's done for us by resize.
	return dst;
}

#endif
