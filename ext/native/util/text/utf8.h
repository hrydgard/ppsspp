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

// Further modified, and C++ stuff added, by hrydgard@gmail.com.

#pragma once

#include "base/basictypes.h"
#include <string>

uint32_t u8_nextchar(const char *s, int *i);
int u8_wc_toutf8(char *dest, uint32_t ch);
int u8_strlen(const char *s);
void u8_inc(const char *s, int *i);
void u8_dec(const char *s, int *i);

class UTF8 {
public:
	static const uint32_t INVALID = (uint32_t)-1;
	UTF8(const char *c) : c_(c), index_(0) {}
	UTF8(const char *c, int index) : c_(c), index_(index) {}
	bool end() const { return c_[index_] == 0; }
	uint32_t next() {
		return u8_nextchar(c_, &index_);
	}
	uint32_t peek() {
		int tempIndex = index_;
		return u8_nextchar(c_, &tempIndex);
	}
	void fwd() {
		u8_inc(c_, &index_);
	}
	void bwd() {
		u8_dec(c_, &index_);
	}
	int length() const {
		return u8_strlen(c_);
	}
	int byteIndex() const {
		return index_;
	}
	static int encode(char *dest, uint32_t ch) {
		return u8_wc_toutf8(dest, ch);
	}
	static int encodeUnits(uint32_t ch) {
		if (ch < 0x80) {
			return 1;
		} else if (ch < 0x800) {
			return 2;
		} else if (ch < 0x10000) {
			return 3;
		} else if (ch < 0x110000) {
			return 4;
		}
		return 0;
	}

private:
	const char *c_;
	int index_;
};

int UTF8StringNonASCIICount(const char *utf8string);

bool UTF8StringHasNonASCII(const char *utf8string);


// UTF8 to Win32 UTF-16
// Should be used when calling Win32 api calls
#ifdef _WIN32

std::string ConvertWStringToUTF8(const std::wstring &wstr);
std::string ConvertWStringToUTF8(const wchar_t *wstr);

#else

std::string ConvertWStringToUTF8(const std::wstring &wstr);

#endif

// Dest size in units, not bytes.
void ConvertUTF8ToWString(wchar_t *dest, size_t destSize, const std::string &source);
std::wstring ConvertUTF8ToWString(const std::string &source);
