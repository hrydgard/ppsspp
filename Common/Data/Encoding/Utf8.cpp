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
#include <windows.h>
#undef min
#undef max
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

/* returns length of next utf-8 sequence */
int u8_seqlen(const char *s)
{
  return trailingBytesForUTF8[(unsigned int)(unsigned char)s[0]] + 1;
}

/* conversions without error checking
   only works for valid UTF-8, i.e. no 5- or 6-byte sequences
   srcsz = source size in bytes, or -1 if 0-terminated
   sz = dest size in # of wide characters

   returns # characters converted
   dest will always be L'\0'-terminated, even if there isn't enough room
   for all the characters.
   if sz = srcsz+1 (i.e. 4*srcsz+4 bytes), there will always be enough space.
*/
int u8_toucs(uint32_t *dest, int sz, const char *src, int srcsz)
{
  uint32_t ch;
  const char *src_end = src + srcsz;
  int nb;
  int i=0;

  while (i < sz-1) {
    nb = trailingBytesForUTF8[(unsigned char)*src];
    if (srcsz == -1) {
      if (*src == 0)
        goto done_toucs;
    }
    else {
      if (src + nb >= src_end)
        goto done_toucs;
    }
    ch = 0;
    switch (nb) {
      /* these fall through deliberately */
    case 3: ch += (unsigned char)*src++; ch <<= 6;
    case 2: ch += (unsigned char)*src++; ch <<= 6;
    case 1: ch += (unsigned char)*src++; ch <<= 6;
    case 0: ch += (unsigned char)*src++;
    }
    ch -= offsetsFromUTF8[nb];
    dest[i++] = ch;
  }
 done_toucs:
  dest[i] = 0;
  return i;
}

/* srcsz = number of source characters, or -1 if 0-terminated
   sz = size of dest buffer in bytes

   returns # characters converted
   dest will only be '\0'-terminated if there is enough space. this is
   for consistency; imagine there are 2 bytes of space left, but the next
   character requires 3 bytes. in this case we could NUL-terminate, but in
   general we can't when there's insufficient space. therefore this function
   only NUL-terminates if all the characters fit, and there's space for
   the NUL as well.
   the destination string will never be bigger than the source string.
*/
int u8_toutf8(char *dest, int sz, const uint32_t *src, int srcsz)
{
  uint32_t ch;
  int i = 0;
  char *dest_end = dest + sz;

  while (srcsz<0 ? src[i]!=0 : i < srcsz) {
    ch = src[i];
    if (ch < 0x80) {
      if (dest >= dest_end)
        return i;
      *dest++ = (char)ch;
    }
    else if (ch < 0x800) {
      if (dest >= dest_end-1)
        return i;
      *dest++ = (ch>>6) | 0xC0;
      *dest++ = (ch & 0x3F) | 0x80;
    }
    else if (ch < 0x10000) {
      if (dest >= dest_end-2)
        return i;
      *dest++ = (ch>>12) | 0xE0;
      *dest++ = ((ch>>6) & 0x3F) | 0x80;
      *dest++ = (ch & 0x3F) | 0x80;
    }
    else if (ch < 0x110000) {
      if (dest >= dest_end-3)
        return i;
      *dest++ = (ch>>18) | 0xF0;
      *dest++ = ((ch>>12) & 0x3F) | 0x80;
      *dest++ = ((ch>>6) & 0x3F) | 0x80;
      *dest++ = (ch & 0x3F) | 0x80;
    }
    i++;
  }
  if (dest < dest_end)
    *dest = '\0';
  return i;
}

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

void u8_inc(const char *s, int *i)
{
  (void)(isutf(s[++(*i)]) || isutf(s[++(*i)]) ||
       isutf(s[++(*i)]) || ++(*i));
}

void u8_dec(const char *s, int *i)
{
  (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) ||
       isutf(s[--(*i)]) || --(*i));
}

int octal_digit(char c)
{
  return (c >= '0' && c <= '7');
}

int hex_digit(char c)
{
  return ((c >= '0' && c <= '9') ||
      (c >= 'A' && c <= 'F') ||
      (c >= 'a' && c <= 'f'));
}

/* assumes that src points to the character after a backslash
   returns number of input characters processed */
int u8_read_escape_sequence(const char *str, uint32_t *dest)
{
  long ch;
  char digs[9]="\0\0\0\0\0\0\0\0";
  int dno=0, i=1;

  ch = (uint32_t)str[0];  /* take literal character */
  if (str[0] == 'n')
    ch = L'\n';
  else if (str[0] == 't')
    ch = L'\t';
  else if (str[0] == 'r')
    ch = L'\r';
  else if (str[0] == 'b')
    ch = L'\b';
  else if (str[0] == 'f')
    ch = L'\f';
  else if (str[0] == 'v')
    ch = L'\v';
  else if (str[0] == 'a')
    ch = L'\a';
  else if (octal_digit(str[0])) {
    i = 0;
    do {
      digs[dno++] = str[i++];
    } while (octal_digit(str[i]) && dno < 3);
    ch = strtol(digs, NULL, 8);
  }
  else if (str[0] == 'x') {
    while (hex_digit(str[i]) && dno < 2) {
      digs[dno++] = str[i++];
    }
    if (dno > 0)
      ch = strtol(digs, NULL, 16);
  }
  else if (str[0] == 'u') {
    while (hex_digit(str[i]) && dno < 4) {
      digs[dno++] = str[i++];
    }
    if (dno > 0)
      ch = strtol(digs, NULL, 16);
  }
  else if (str[0] == 'U') {
    while (hex_digit(str[i]) && dno < 8) {
      digs[dno++] = str[i++];
    }
    if (dno > 0)
      ch = strtol(digs, NULL, 16);
  }
  *dest = (uint32_t)ch;

  return i;
}

/* convert a string with literal \uxxxx or \Uxxxxxxxx characters to UTF-8
   example: u8_unescape(mybuf, 256, "hello\\u220e")
   note the double backslash is needed if called on a C string literal */
int u8_unescape(char *buf, int sz, char *src)
{
  int c=0, amt;
  uint32_t ch;
  char temp[4];

  while (*src && c < sz) {
    if (*src == '\\') {
      src++;
      amt = u8_read_escape_sequence(src, &ch);
    }
    else {
      ch = (uint32_t)*src;
      amt = 1;
    }
    src += amt;
    amt = u8_wc_toutf8(temp, ch);
    if (amt > sz-c)
      break;
    memcpy(&buf[c], temp, amt);
    c += amt;
  }
  if (c < sz)
    buf[c] = '\0';
  return c;
}

int u8_is_locale_utf8(const char *locale)
{
  /* this code based on libutf8 */
  const char* cp = locale;

  for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++) {
    if (*cp == '.') {
      const char* encoding = ++cp;
      for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++)
        ;
      if ((cp-encoding == 5 && !strncmp(encoding, "UTF-8", 5))
        || (cp-encoding == 4 && !strncmp(encoding, "utf8", 4)))
        return 1; /* it's UTF-8 */
      break;
    }
  }
  return 0;
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

void ConvertUTF8ToUCS2(char16_t *dest, size_t destSize, const std::string &source) {
	ConvertUTF8ToUCS2Internal(dest, destSize, source);
}

std::u16string ConvertUTF8ToUCS2(std::string_view source) {
	std::u16string dst;
	// utf-8 won't be less bytes than there are characters.
	dst.resize(source.size(), 0);
	size_t realLen = ConvertUTF8ToUCS2Internal(&dst[0], source.size(), source);
	dst.resize(realLen);
	return dst;
}

std::string CodepointToUTF8(uint32_t codePoint) {
	char temp[16]{};
	UTF8::encode(temp, codePoint);
	return std::string(temp);
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
