#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Net/URL.h"

int MultipartFormDataEncoder::seq = 0;

void UrlEncoder::AppendEscaped(const std::string &value)
{
	static const char * const unreservedChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
	static const char * const hexChars = "0123456789ABCDEF";

	for (size_t lastEnd = 0; lastEnd < value.length(); )
	{
		size_t pos = value.find_first_not_of(unreservedChars, lastEnd);
		if (pos == value.npos)
		{
			data += value.substr(lastEnd);
			break;
		}

		if (pos != lastEnd)
			data += value.substr(lastEnd, pos - lastEnd);
		lastEnd = pos;

		// Encode the reserved character.
		char c = value[pos];
		data += '%';
		data += hexChars[(c >> 4) & 15];
		data += hexChars[(c >> 0) & 15];
		++lastEnd;
	}
}

void Url::Split() {
	size_t colonSlashSlash = url_.find("://");
	if (colonSlashSlash == std::string::npos) {
		ERROR_LOG(Log::IO, "Invalid URL: %s", url_.c_str());
		return;
	}

	protocol_ = url_.substr(0, colonSlashSlash);

	size_t sep = url_.find('/', colonSlashSlash + 3);
	if (sep == std::string::npos) {
		sep = url_.size();
	}

	host_ = url_.substr(colonSlashSlash + 3, sep - colonSlashSlash - 3);
	resource_ = url_.substr(sep);  // include the slash!
	if (resource_.empty()) {
		resource_ = "/";  // Assume what was meant was the root.
	}

	size_t portsep = host_.rfind(':');
	if (portsep != host_.npos) {
		port_ = atoi(host_.substr(portsep + 1).c_str());
		host_ = host_.substr(0, portsep);
	} else {
		port_ = protocol_ == "https" ? 443 : 80;
	}

	valid_ = protocol_.size() > 1 && host_.size() > 1;
}

Url Url::Relative(const std::string &next) const {
	if (next.size() > 2 && next[0] == '/' && next[1] == '/') {
		// This means use the same protocol, but the rest is new.
		return Url(protocol_ + ":" + next);
	}

	// Or it could just be a fully absolute URL.
	size_t colonSlashSlash = next.find("://");
	if (colonSlashSlash != std::string::npos) {
		return Url(next);
	}

	// Anything else should be a new resource, but it might be directory relative.
	Url resolved = *this;
	if (next.size() > 1 && next[0] == '/') {
		// Easy, just replace the resource.
		resolved.resource_ = next;
	} else {
		size_t last_slash = resource_.find_last_of('/');
		resolved.resource_ = resource_.substr(0, last_slash + 1) + next;
	}

	resolved.url_ = resolved.ToString();
	return resolved;
}

std::string Url::ToString() const {
	if (!valid_) {
		return "about:invalid-url";
	}

	std::string serialized = protocol_ + "://" + host_;
	bool needsPort = true;
	if (protocol_ == "https") {
		needsPort = port_ != 443;
	} else if (protocol_ == "http") {
		needsPort = port_ != 80;
	}

	if (needsPort) {
		serialized += ":" + StringFromInt(port_);
	}

	return serialized + resource_;
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

std::string UriDecode(std::string_view sSrc)
{
	// Note from RFC1630:  "Sequences which start with a percent sign
	// but are not followed by two hexadecimal characters (0-9, A-F) are reserved
	// for future extension"

	const unsigned char * pSrc = (const unsigned char *)sSrc.data();
	const size_t SRC_LEN = sSrc.length();
	const unsigned char * const SRC_END = pSrc + SRC_LEN;
	const unsigned char * const SRC_LAST_DEC = SRC_END - 2;   // last decodable '%' 

	char * const pStart = new char[SRC_LEN];  // Output will be shorter.
	char * pEnd = pStart;

	while (pSrc < SRC_LAST_DEC) {
		if (*pSrc == '%') {
			char dec1, dec2;
			if (N1 != (dec1 = HEX2DEC[*(pSrc + 1)]) && N1 != (dec2 = HEX2DEC[*(pSrc + 2)])) {
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

// Only alphanum and underscore is safe.
static const char SAFE[256] = {
	/*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
	/* 0 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 1 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 2 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 3 */ 1,1,1,1, 1,1,1,1, 1,1,0,0, 0,0,0,0,

	/* 4 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
	/* 5 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,1,  // last here is underscore. it's ok.
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

std::string UriEncode(std::string_view sSrc) {
	const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
	const unsigned char * pSrc = (const unsigned char *)sSrc.data();
	const size_t SRC_LEN = sSrc.length();
	unsigned char * const pStart = new unsigned char[SRC_LEN * 3];
	unsigned char * pEnd = pStart;
	const unsigned char * const SRC_END = pSrc + SRC_LEN;

	for (; pSrc < SRC_END; ++pSrc) {
		if (SAFE[*pSrc]) {
			*pEnd++ = *pSrc;
		} else {
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
