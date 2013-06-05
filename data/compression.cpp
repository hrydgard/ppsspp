// Little utility functions for data compression.
// Taken from http://panthema.net/2007/0328-ZLibString.html


#include <string>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>

#include <zlib.h>

#include "base/logging.h"

/** Compress a STL string using zlib with given compression level and return
* the binary data. */
bool compress_string(const std::string& str, std::string *dest, int compressionlevel) {
	z_stream zs;                        // z_stream is zlib's control structure
	memset(&zs, 0, sizeof(zs));

	if (deflateInit(&zs, compressionlevel) != Z_OK) {
		ELOG("deflateInit failed while compressing.");
		return false;
	}

	zs.next_in = (Bytef*)str.data();
	zs.avail_in = str.size();           // set the z_stream's input

	int ret;
	char outbuffer[32768];
	std::string outstring;

	// retrieve the compressed bytes blockwise
	do {
		zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
		zs.avail_out = sizeof(outbuffer);

		ret = deflate(&zs, Z_FINISH);

		if (outstring.size() < zs.total_out) {
			// append the block to the output string
			outstring.append(outbuffer,
				zs.total_out - outstring.size());
		}
	} while (ret == Z_OK);

	deflateEnd(&zs);

	if (ret != Z_STREAM_END) {          // an error occurred that was not EOF
		std::ostringstream oss;
		oss << "Exception during zlib compression: (" << ret << ") " << zs.msg;
		return false;
	}

	*dest = outstring;
	return true;
}

/** Decompress an STL string using zlib and return the original data. */
bool decompress_string(const std::string& str, std::string *dest) {
	if (!str.size())
		return false;

	z_stream zs;                        // z_stream is zlib's control structure
	memset(&zs, 0, sizeof(zs));

	// modification by hrydgard: inflateInit2, 16+MAXWBITS makes it read gzip data too
	if (inflateInit2(&zs, 32+MAX_WBITS) != Z_OK) {
		ELOG("inflateInit failed while decompressing.");
		return false;
	}

	zs.next_in = (Bytef*)str.data();
	zs.avail_in = str.size();

	int ret;
	char outbuffer[32768];
	std::string outstring;

	// get the decompressed bytes blockwise using repeated calls to inflate
	do {
		zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
		zs.avail_out = sizeof(outbuffer);

		ret = inflate(&zs, 0);

		if (outstring.size() < zs.total_out) {
			outstring.append(outbuffer,
				zs.total_out - outstring.size());
		}

	} while (ret == Z_OK);

	inflateEnd(&zs);

	if (ret != Z_STREAM_END) {          // an error occurred that was not EOF
		std::ostringstream oss;
		ELOG("Exception during zlib decompression: (%i) %s", ret, zs.msg);
		return false;
	}

	*dest = outstring;
	return true;
}
