#include <cstring>
#include <cstdio>

#include "Common/CommonTypes.h"
#include "UnitTest.h"

// No header exists for the LZRC decompressor; it's declared locally by callers.
int lzrc_decompress(void *out, int out_len, void *in, int in_len);

// The plain-text path (lc bit 0x80 set) must clamp the copy size to both the
// output buffer and the remaining input, not read past the end of input.
static bool TestLzrcPlainTextClamp() {
	// lc = 0x80 (plain text), code = 0xFFFFFF (huge), then "hello".
	u8 input[] = { 0x80, 0xFF, 0xFF, 0xFF, 0x00, 'h', 'e', 'l', 'l', 'o' };
	u8 output[16];
	memset(output, 0xAA, sizeof(output));

	int result = lzrc_decompress(output, sizeof(output), input, sizeof(input));
	// copySize = min(0xFFFFFF, 16, 10 - 5 = 5) = 5.
	EXPECT_EQ_INT(result, 5);
	EXPECT_TRUE(memcmp(output, "hello", 5) == 0);
	EXPECT_TRUE(output[5] == 0xAA);
	return true;
}

// Truncated input (fewer than the 5 header bytes) must not read past the buffer.
static bool TestLzrcTruncatedInput() {
	u8 input[] = { 0x00 };
	u8 output[16];
	memset(output, 0xAA, sizeof(output));

	int result = lzrc_decompress(output, sizeof(output), input, 1);
	EXPECT_EQ_INT(result, -1);
	// Nothing should have been written.
	for (size_t i = 0; i < sizeof(output); ++i) {
		EXPECT_TRUE(output[i] == 0xAA);
	}
	return true;
}

// A compressed stream that would overflow a tiny output buffer must return an
// error and not write past the buffer.
static bool TestLzrcOutputOverflow() {
	// lc = 0x00 (compressed), code = 0. Feed enough bytes for the decoder to run.
	u8 input[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 output[4];
	memset(output, 0xBB, sizeof(output));

	int result = lzrc_decompress(output, 0, input, sizeof(input));
	// The decoder may bail out as an error (-1) or see end-of-stream (0),
	// but must never write into the (zero-sized) output.
	EXPECT_TRUE(result == -1 || result == 0);
	for (size_t i = 0; i < sizeof(output); ++i) {
		EXPECT_TRUE(output[i] == 0xBB);
	}
	return true;
}

bool TestLzrc() {
	if (!TestLzrcPlainTextClamp())
		return false;
	if (!TestLzrcTruncatedInput())
		return false;
	if (!TestLzrcOutputOverflow())
		return false;
	return true;
}
