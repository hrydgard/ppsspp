#include "base/logging.h"
#include "net/url.h"

const char *UrlEncoder::unreservedChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
const char *UrlEncoder::hexChars = "0123456789ABCDEF";

void Url::Split() {
	size_t colonSlashSlash = url_.find("://");
	if (colonSlashSlash == std::string::npos) {
		ELOG("Invalid URL: %s", url_.c_str());
		return;
	}

	protocol_ = url_.substr(0, colonSlashSlash);

	size_t sep = url_.find('/', colonSlashSlash + 3);

	host_ = url_.substr(colonSlashSlash + 3, sep - colonSlashSlash - 3);
	resource_ = url_.substr(sep);  // include the slash!

	valid_ = protocol_.size() > 1 && host_.size() > 1;
}

// UriDecode and UriEncode are from http://www.codeguru.com/cpp/cpp/string/conversions/print.php/c12759
// by jinq0123 (November 2, 2006)

// Uri encode and decode.
// RFC1630, RFC1738, RFC2396

// Some compilers don't like to assume (int)-1 will safely cast to (char)-1 as
// the MSBs aren't 0's. Workaround the issue while maintaining table spacing.
#define N1 (char)-1
const char HEX2DEC[256] =
{
	/*       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F */
	/* 0 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* 1 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* 2 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* 3 */  0, 1, 2, 3,  4, 5, 6, 7,  8, 9,N1,N1, N1,N1,N1,N1,

	/* 4 */ N1,10,11,12, 13,14,15,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* 5 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* 6 */ N1,10,11,12, 13,14,15,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* 7 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,

	/* 8 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* 9 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* A */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* B */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,

	/* C */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* D */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* E */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
	/* F */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1
};

std::string UriDecode(const std::string & sSrc)
{
	// Note from RFC1630:  "Sequences which start with a percent sign
	// but are not followed by two hexadecimal characters (0-9, A-F) are reserved
	// for future extension"

	const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
	const size_t SRC_LEN = sSrc.length();
	const unsigned char * const SRC_END = pSrc + SRC_LEN;
	const unsigned char * const SRC_LAST_DEC = SRC_END - 2;   // last decodable '%' 

	char * const pStart = new char[SRC_LEN];
	char * pEnd = pStart;

	while (pSrc < SRC_LAST_DEC)
	{
		if (*pSrc == '%')
		{
			char dec1, dec2;
			if (-1 != (dec1 = HEX2DEC[*(pSrc + 1)])
				&& -1 != (dec2 = HEX2DEC[*(pSrc + 2)]))
			{
				*pEnd++ = (dec1 << 4) + dec2;
				pSrc += 3;
				continue;
			}
		}

		*pEnd++ = *pSrc++;
	}

	// the last 2- chars
	while (pSrc < SRC_END)
		*pEnd++ = *pSrc++;

	std::string sResult(pStart, pEnd);
	delete [] pStart;
	return sResult;
}

// Only alphanum is safe.
const char SAFE[256] =
{
	/*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
	/* 0 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 1 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 2 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 3 */ 1,1,1,1, 1,1,1,1, 1,1,0,0, 0,0,0,0,

	/* 4 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
	/* 5 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,
	/* 6 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
	/* 7 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,

	/* 8 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 9 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* A */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* B */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

	/* C */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* D */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* E */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* F */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
};

std::string UriEncode(const std::string & sSrc)
{
	const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
	const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
	const size_t SRC_LEN = sSrc.length();
	unsigned char * const pStart = new unsigned char[SRC_LEN * 3];
	unsigned char * pEnd = pStart;
	const unsigned char * const SRC_END = pSrc + SRC_LEN;

	for (; pSrc < SRC_END; ++pSrc)
	{
		if (SAFE[*pSrc]) 
			*pEnd++ = *pSrc;
		else
		{
			// escape this char
			*pEnd++ = '%';
			*pEnd++ = DEC2HEX[*pSrc >> 4];
			*pEnd++ = DEC2HEX[*pSrc & 0x0F];
		}
	}

	std::string sResult((char *)pStart, (char *)pEnd);
	delete [] pStart;
	return sResult;
}
