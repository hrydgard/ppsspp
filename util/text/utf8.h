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

#include "base/basictypes.h"

uint32_t u8_nextchar(const char *s, int *i);
int u8_strlen(const char *s);

class UTF8 {
public:
	UTF8(const char *c) : c_(c), index_(0) {}
	bool end() const { return c_[index_] == 0; }
	uint32_t next() {
		return u8_nextchar(c_, &index_);
	}
	int length() const {
		return u8_strlen(c_);
	}

private:
	const char *c_;
	int index_;
};

int UTF8StringNonASCIICount(const char *utf8string);

bool UTF8StringHasNonASCII(const char *utf8string);