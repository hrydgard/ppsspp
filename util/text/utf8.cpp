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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "base/basictypes.h"
#include "utf8.h"

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
int u8_toutf8(char *dest, int sz, uint32_t *src, int srcsz)
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

/* number of characters */
int u8_strlen(const char *s)
{
  int count = 0;
  int i = 0;

  while (u8_nextchar(s, &i) != 0)
    count++;

  return count;
}

/* reads the next utf-8 sequence out of a string, updating an index */
uint32_t u8_nextchar(const char *s, int *i)
{
  uint32_t ch = 0;
  int sz = 0;

  do {
    ch <<= 6;
    ch += (unsigned char)s[(*i)++];
    sz++;
  } while (s[*i] && !isutf(s[*i]));
  ch -= offsetsFromUTF8[sz-1];

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
  uint32_t ch;
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
  *dest = ch;

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

const char *u8_strchr(const char *s, uint32_t ch, int *charn)
{
  int i = 0, lasti=0;
  uint32_t c;

  *charn = 0;
  while (s[i]) {
    c = u8_nextchar(s, &i);
    if (c == ch) {
      return &s[lasti];
    }
    lasti = i;
    (*charn)++;
  }
  return NULL;
}

const char *u8_memchr(const char *s, uint32_t ch, size_t sz, int *charn)
{
  int i = 0, lasti=0;
  uint32_t c;
  int csz;

  *charn = 0;
  while (i < sz) {
    c = csz = 0;
    do {
      c <<= 6;
      c += (unsigned char)s[i++];
      csz++;
    } while (i < sz && !isutf(s[i]));
    c -= offsetsFromUTF8[csz-1];

    if (c == ch) {
      return &s[lasti];
    }
    lasti = i;
    (*charn)++;
  }
  return NULL;
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

int UTF8StringNonASCIICount(const char *utf8string) {
	UTF8 utf(utf8string);
	int count = 0;
	while (!utf.end()) {
		int c = utf.next();
		if (c > 127)
			++count;
	}
	return count;
}

bool UTF8StringHasNonASCII(const char *utf8string) {
	return UTF8StringNonASCIICount(utf8string) > 0;
}