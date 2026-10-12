#include <cstring>

#include <encodings/deflate.h>

#include "Common/Data/Encoding/Compression.h"
#include "Common/Log.h"

int64_t InflateBuffer(void *stream, int windowBits, const uint8_t *in, size_t inLen, uint8_t *out, size_t outLen, size_t *consumed) {
	rinflate_reset(stream, windowBits);
	rinflate_set_in(stream, in, inLen);
	rinflate_set_out(stream, out, outLen);
	size_t rd = 0, wr = 0;
	// One call runs until the stream ends or a buffer runs out.
	const int status = rinflate_process(stream, &rd, &wr);
	if (consumed)
		*consumed = rd;
	return status == RDEFLATE_PROCESS_END ? (int64_t)wr : -1;
}

int64_t InflateBuffer(int windowBits, const uint8_t *in, size_t inLen, uint8_t *out, size_t outLen, size_t *consumed) {
	void *stream = rinflate_new(windowBits);
	if (!stream)
		return -1;
	const int64_t result = InflateBuffer(stream, windowBits, in, inLen, out, outLen, consumed);
	rinflate_free(stream);
	return result;
}

uint32_t Adler32(uint32_t adler, const uint8_t *buf, size_t len) {
	// 5552 is the most bytes that can be summed before the 32-bit sums overflow.
	uint32_t a = adler & 0xFFFF, b = adler >> 16;
	while (len > 0) {
		size_t n = len < 5552 ? len : 5552;
		len -= n;
		while (n--) {
			a += *buf++;
			b += a;
		}
		a %= 65521;
		b %= 65521;
	}
	return (b << 16) | a;
}

// Runs a stream to its end, growing *dest as output arrives.
template <class Process, class SetOut>
static bool RunToEnd(void *stream, std::string *dest, size_t initial, Process process, SetOut setOut) {
	std::string out;
	out.resize(initial < 4096 ? 4096 : initial);
	size_t used = 0;
	for (;;) {
		setOut(stream, (uint8_t *)&out[used], out.size() - used);
		size_t rd = 0, wr = 0;
		const int status = process(stream, &rd, &wr);
		used += wr;
		if (status == RDEFLATE_PROCESS_END)
			break;
		if (status != RDEFLATE_PROCESS_NEXT)
			return false;
		if (used == out.size())
			out.resize(out.size() * 2);
		else if (rd == 0 && wr == 0)
			return false;  // input ran out before the end of the stream
	}
	out.resize(used);
	*dest = std::move(out);
	return true;
}

bool compress_string(const std::string &str, std::string *dest, int compressionlevel, int windowBits) {
	void *stream = rdeflate_new(compressionlevel, windowBits);
	if (!stream)
		return false;
	rdeflate_set_in(stream, (const uint8_t *)str.data(), str.size());
	rdeflate_finish(stream);
	const bool ok = RunToEnd(stream, dest, str.size() / 2 + 64, rdeflate_process, rdeflate_set_out);
	rdeflate_free(stream);
	if (!ok)
		ERROR_LOG(Log::IO, "deflate failed while compressing.");
	return ok;
}

bool decompress_string(const std::string &str, std::string *dest) {
	if (str.empty())
		return false;
	void *stream = rinflate_new(47);
	if (!stream)
		return false;
	rinflate_set_in(stream, (const uint8_t *)str.data(), str.size());
	// rinflate_set_out treats each new window as following the last, so growing works.
	const bool ok = RunToEnd(stream, dest, str.size() * 4, rinflate_process, rinflate_set_out);
	rinflate_free(stream);
	if (!ok)
		ERROR_LOG(Log::IO, "inflate failed while decompressing.");
	return ok;
}

