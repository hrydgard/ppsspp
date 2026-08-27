// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// UnitTests
//
// This is a program to directly test various functions, without going
// through a PSP. Especially useful for things like opcode emitters,
// hashes, and various data conversion utility function.
//
// TODO: Make a test of nice unittest asserts and count successes etc.
// Or just integrate with an existing testing framework.
//
// To use, set command line parameter to one or more of the tests below, or "all".
// Search for "availableTests".
//
// Example of how to run with CMake:
//
// ./b.sh --unittest
// build/unittest EscapeMenuString

#include "ppsspp_config.h"

#include <typeinfo>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>

#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
#endif

#include "Common/Data/Collections/TinySet.h"
#include "Common/Data/Collections/FastVec.h"
#include "Common/Data/Collections/CharQueue.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Buffer.h"
#include "Common/File/Path.h"
#include "Common/Log/LogManager.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/Math/CrossSIMD.h"
// Get some more instructions for testing
#if PPSSPP_ARCH(SSE2)
#include <immintrin.h>
#endif

#include "Common/Input/InputState.h"
#include "Common/Math/math_util.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/TimeUtil.h"

#include "Common/ArmEmitter.h"
#include "Common/BitScan.h"
#include "Common/CPUDetect.h"
#include "Common/ExceptionHandlerSetup.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/Math/fast/fast_matrix.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Serialize/SerializeSet.h"
#include "Common/Serialize/SerializeList.h"
#include <map>
#include <set>
#include <list>
#include "Core/CmdLine.h"
#include "Common/Data/Collections/Hashmaps.h"
#include "Core/Util/BlockAllocator.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/MemMap.h"
#include "Core/KeyMap.h"
#include "Core/Util/PathUtil.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Math3D.h"

#include "Common/File/AndroidContentURI.h"

#include "unittest/JitHarness.h"
#include "unittest/TestVertexJit.h"
#include "unittest/UnitTest.h"

// Set to true for more verbose unit tests.
bool g_testLog = false;

std::string System_GetProperty(SystemProperty prop) { return ""; }
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) { return std::vector<std::string>(); }
int64_t System_GetPropertyInt(SystemProperty prop) {
	return -1;
}
float System_GetPropertyFloat(SystemProperty prop) {
	return -1;
}
bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_CAN_JIT:
		return true;
	default:
		return false;
	}
}
void System_Notify(SystemNotification notification) {}
void System_PostUIMessage(UIMessage message, std::string_view param) {}
void System_RunOnMainThread(std::function<void()>) {}
void System_AudioGetDebugStats(char *buf, size_t bufSize) { if (buf) buf[0] = '\0'; }
void System_AudioClear() {}
void System_AudioPushSamples(const s32 *audio, int numSamples, float volume) {}
std::vector<std::string> System_GetCameraDeviceList() { return std::vector<std::string>(); }

// Temporary hacks around annoying linking errors.  Copied from Headless.
void NativeFrame(GraphicsContext *graphicsContext) {}
void NativeResized() {}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) { return false; }
// Pulled in via Core/WebServer.cpp's OpenWebDebugger(), which CmdLine.cpp now references.
void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {}
void System_InputBoxGetString(const std::string &title, const std::string &defaultValue, std::function<void(bool, const std::string &)> cb) { cb(false, ""); }
void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

// TODO: To avoid having to define these here, these should probably be turned into system "requests".
// To clear the secret entirely, just save an empty string.
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) { return false; }
std::string NativeLoadSecret(std::string_view nameOfSecret) { return ""; }

#if PPSSPP_PLATFORM(ANDROID)
JNIEnv *getEnv() {
	return nullptr;
}

jclass findClass(const char *name) {
	return nullptr;
}

bool System_AudioRecordingIsAvailable() { return false; }
bool System_AudioRecordingState() { return false; }
#endif

#ifndef M_PI_2
#define M_PI_2     1.57079632679489661923
#endif

// asin acos atan: https://github.com/michaldrobot/ShaderFastLibs/blob/master/ShaderFastMathLib.h

// TODO:
// Fast approximate sincos for NEON
// http://blog.julien.cayzac.name/2009/12/fast-sinecosine-for-armv7neon.html
// Fast sincos
// http://www.dspguru.com/dsp/tricks/parabolic-approximation-of-sin-and-cos

// minimax (surprisingly terrible! something must be wrong)
// double asin_plus_sqrtthing = .9998421793 + (1.012386649 + (-.6575341673 + .8999841642 + (-1.669668977 + (1.571945105 - .5860008052 * x) * x) * x) * x) * x;

// VERY good. 6 MAD, one division.
// double asin_plus_sqrtthing = (1.807607311 + (.191900116 + (-2.511278506 + (1.062519236 + (-.3572142480 + .1087063463 * x) * x) * x) * x) * x) / (1.807601897 - 1.615203794 * x);
// float asin_plus_sqrtthing_correct_ends =
// 	(1.807607311f + (.191900116f + (-2.511278506f + (1.062519236f + (-.3572142480f + .1087063463f * x) * x) * x) * x) * x) / (1.807607311f - 1.615195094 * x);

// Unfortunately this is very serial.
// At least there are only 8 constants needed - load them into two low quads and go to town.
// For every step, VDUP the constant into a new register (out of two alternating), then VMLA or VFMA into it.

// http://www.ecse.rpi.edu/~wrf/Research/Short_Notes/arcsin/
// minimax polynomial rational approx, pretty good, get four digits consistently.
// unfortunately fastasin(1.0) / M_PI_2  != 1.0f, but it's pretty close.
float fastasin(double x) {
	float sign = x >= 0.0f ? 1.0f : -1.0f;
	x = fabs(x);
	float sqrtthing = sqrt(1.0f - x * x);
	// note that the sqrt can run parallel while we do the rest
	// if the hardware supports it

	float y = -.3572142480f + .1087063463f * x;
	y = y * x + 1.062519236f;
	y = y * x + -2.511278506f;
	y = y * x + .191900116f;
	y = y * x + 1.807607311f;
	y /= (1.807607311f - 1.615195094 * x);
	return sign * (y - sqrtthing);
}

double atan_66s(double x) {
	const double c1=1.6867629106;
	const double c2=0.4378497304;
	const double c3=1.6867633134;

	double x2; // The input argument squared

	x2 = x * x;
	return (x*(c1 + x2*c2)/(c3 + x2));
}

// Terrible.
double fastasin2(double x) {
	return atan_66s(x / sqrt(1 - x * x));
}

// Also terrible.
float fastasin3(float x) {
	return x + x * x * x * x * x * 0.4971;
}

// Great! This is the one we'll use. Can be easily rescaled to get the right range for free.
// http://mathforum.org/library/drmath/view/54137.html
// http://www.musicdsp.org/showone.php?id=115
float fastasin4(float x) {
	float sign = x >= 0.0f ? 1.0f : -1.0f;
	x = fabs(x);
	x = M_PI/2 - sqrtf(1.0f - x) * (1.5707288 + -0.2121144*x + 0.0742610*x*x + -0.0187293*x*x*x);
	return sign * x;
}

// Or this:
float fastasin5(float x)
{
	float sign = x >= 0.0f ? 1.0f : -1.0f;
	x = fabs(x);
	float fRoot = sqrtf(1.0f - x);
	float fResult = 0.0742610f + -0.0187293f  * x;
	fResult = -0.2121144f + fResult * x;
	fResult = 1.5707288f + fResult * x;
	fResult = M_PI/2 - fRoot*fResult;
	return sign * fResult;
}


// This one is unfortunately not very good. But lets us avoid PI entirely
// thanks to the special arguments of the PSP functions.
// http://www.dspguru.com/dsp/tricks/parabolic-approximation-of-sin-and-cos
#define C            0.70710678118654752440f    // 1.0f / sqrt(2.0f)
// Some useful constants (PI and <math.h> are not part of algo)
#define BITSPERQUARTER (20)
void fcs(float angle, float &sinout, float &cosout) {
	int phasein = angle * (1 << BITSPERQUARTER);
	// Modulo phase into quarter, convert to float 0..1
	float modphase = (phasein & ((1<<BITSPERQUARTER)-1)) * (1.0f / (1<<BITSPERQUARTER));
	// Extract quarter bits
	int quarter = phasein >> BITSPERQUARTER;
	// Recognize quarter
	if (!quarter) {
		// First quarter, angle = 0 .. pi/2
		float x = modphase - 0.5f;      // 1 sub
		float temp = (2 - 4*C)*x*x + C; // 2 mul, 1 add
		sinout = temp + x;              // 1 add
		cosout = temp - x;              // 1 sub
	} else if (quarter == 1) {
		// Second quarter, angle = pi/2 .. pi
		float x = 0.5f - modphase;      // 1 sub
		float temp = (2 - 4*C)*x*x + C; // 2 mul, 1 add
		sinout = x + temp;              // 1 add
		cosout = x - temp;              // 1 sub
	} else if (quarter == 2) {
		// Third quarter, angle = pi .. 1.5pi
		float x = modphase - 0.5f;      // 1 sub
		float temp = (4*C - 2)*x*x - C; // 2 mul, 1 sub
		sinout = temp - x;              // 1 sub
		cosout = temp + x;              // 1 add
	} else if (quarter == 3) {
		// Fourth quarter, angle = 1.5pi..2pi
		float x = modphase - 0.5f;      // 1 sub
		float temp = (2 - 4*C)*x*x + C; // 2 mul, 1 add
		sinout = x - temp;              // 1 sub
		cosout = x + temp;              // 1 add
	}
}
#undef C


const float PI_SQR      = 9.86960440108935861883449099987615114f;

//https://code.google.com/p/math-neon/source/browse/trunk/math_floorf.c?r=18
// About 2 correct decimals. Not great.
void fcs2(float theta, float &outsine, float &outcosine) {
	float gamma = theta + 1;
	gamma += 2;
	gamma /= 4;
	theta += 2;
	theta /= 4;
	//theta -= (float)(int)theta;
	//gamma -= (float)(int)gamma;
	theta -= floorf(theta);
	gamma -= floorf(gamma);
	theta *= 4;
	theta -= 2;
	gamma *= 4;
	gamma -= 2;

	float x = 2 * gamma - gamma * fabs(gamma);
	float y = 2 * theta - theta * fabs(theta);
	const float P = 0.225f;
	outsine = P * (y * fabsf(y) - y) + y;   // Q * y + P * y * abs(y)
	outcosine = P * (x * fabsf(x) - x) + x;   // Q * y + P * y * abs(y)
}



void fastsincos(float x, float &sine, float &cosine) {
	fcs2(x, sine, cosine);
}

bool TestSinCos() {
	for (int i = -100; i <= 100; i++) {
		float f = i / 30.0f;

		// The PSP sin/cos take as argument angle * M_PI_2.
		// We need to match that.
		float slowsin = sinf(f * M_PI_2), slowcos = cosf(f * M_PI_2);
		float fastsin, fastcos;
		fastsincos(f, fastsin, fastcos);
		if (g_testLog) {
			printf("%f: slow: %0.8f, %0.8f fast: %0.8f, %0.8f\n", f, slowsin, slowcos, fastsin, fastcos);
		}
	}
	return true;
}


bool TestAsin() {
	for (int i = -100; i <= 100; i++) {
		float f = i / 100.0f;
		float slowval = asinf(f) / M_PI_2;
		float fastval = fastasin5(f) / M_PI_2;
		if (g_testLog) {
			printf("slow: %0.16f fast: %0.16f\n", slowval, fastval);
		}
		float diff = fabsf(slowval - fastval);
		// EXPECT_TRUE(diff < 0.0001f);
	}
	// EXPECT_TRUE(fastasin(1.0) / M_PI_2 <= 1.0f);
	return true;
}

bool TestMathUtil() {
	EXPECT_FALSE(my_isinf(1.0));
	volatile float zero = 0.0f;
	EXPECT_TRUE(my_isinf(1.0f/zero));
	EXPECT_FALSE(my_isnan(1.0f/zero));
	return true;
}

bool TestParsers() {
	const char *macstr = "01:02:03:ff:fe:fd";
	uint8_t mac[6];
	ParseMacAddress(macstr, mac);
	EXPECT_TRUE(mac[0] == 1);
	EXPECT_TRUE(mac[1] == 2);
	EXPECT_TRUE(mac[2] == 3);
	EXPECT_TRUE(mac[3] == 255);
	EXPECT_TRUE(mac[4] == 254);
	EXPECT_TRUE(mac[5] == 253);
	return true;
}

bool TestTruncateCpy() {
	// Normal in-bounds copy.
	char buf[8];
	size_t len = truncate_cpy_len(buf, "abc", 3);
	EXPECT_EQ_INT((int)len, 3);
	EXPECT_TRUE(strcmp(buf, "abc") == 0);

	// Exact fit (source length is Count - 1).
	len = truncate_cpy_len(buf, "abcdefg", 7);
	EXPECT_EQ_INT((int)len, 7);
	EXPECT_TRUE(strcmp(buf, "abcdefg") == 0);

	// Overflow - truncated to Count - 1 chars.
	len = truncate_cpy_len(buf, "abcdefghij", 10);
	EXPECT_EQ_INT((int)len, 7);
	EXPECT_TRUE(strcmp(buf, "abcdefg") == 0);

	// Zero-length source used to underflow to out[-1].
	buf[0] = 'X';
	len = truncate_cpy_len(buf, "", 0);
	EXPECT_EQ_INT((int)len, 0);
	EXPECT_EQ_INT((int)buf[0], 0);

	// Simple concatenation.
	char catBuf[16];
	len = truncate_cat(catBuf, sizeof(catBuf), "abc", 3, "def", 3);
	EXPECT_EQ_INT((int)len, 6);
	EXPECT_TRUE(strcmp(catBuf, "abcdef") == 0);

	// Truncation when the combined length exceeds the buffer.
	len = truncate_cat(catBuf, 8, "abcd", 4, "efghij", 6);
	EXPECT_EQ_INT((int)len, 7);
	EXPECT_TRUE(strcmp(catBuf, "abcdefg") == 0);

	// src1 alone already fills/overflows the buffer.
	len = truncate_cat(catBuf, 4, "abcdefg", 7, "xyz", 3);
	EXPECT_EQ_INT((int)len, 3);
	EXPECT_TRUE(strcmp(catBuf, "abc") == 0);

	// Both empty used to underflow to out[-1].
	catBuf[0] = 'X';
	len = truncate_cat(catBuf, sizeof(catBuf), "", 0, "", 0);
	EXPECT_EQ_INT((int)len, 0);
	EXPECT_EQ_INT((int)catBuf[0], 0);
	return true;
}

bool TestUtf8() {
	// Valid multi-byte UTF-8 (ASCII + 2-byte 'é' + 3-byte '€') round-trips unchanged.
	const std::string valid = "abc \xC3\xA9 \xE2\x82\xAC";
	EXPECT_TRUE(SanitizeUTF8(valid) == valid);

	// u8_nextchar must stop at the end of the buffer instead of reading past a
	// truncated multi-byte sequence (a lead byte with no continuation bytes).
	{
		std::string s = "abc";
		s += (char)0xF4;
		int index = 3;
		int size = (int)s.size();
		uint32_t c = u8_nextchar(s.data(), &index, size);
		EXPECT_EQ_INT(index, size);
		EXPECT_EQ_INT((int)c, 0xF4);
	}

	// A long run of stray continuation bytes must not walk off the end of the
	// internal offsetsFromUTF8 table (used to read arbitrarily far out of bounds).
	{
		std::string s(32, (char)0x80);
		int index = 0;
		int size = (int)s.size();
		uint32_t c = u8_nextchar(s.data(), &index, size);
		EXPECT_TRUE(index > 0 && index <= size);
	}

	// SanitizeUTF8 on a string that ends mid-sequence must not read or write past
	// the buffer, and must preserve the well-formed leading portion.
	{
		std::string truncated = "abc";
		truncated += (char)0xF4;
		std::string sanitized = SanitizeUTF8(truncated);
		EXPECT_TRUE(sanitized.substr(0, 3) == "abc");
	}

	// ConvertUTF8ToJavaModifiedUTF8 must simply drop an incomplete trailing
	// sequence rather than asserting or crashing.
	{
		std::string input = "abc";
		input += (char)0xF0;
		std::string output;
		ConvertUTF8ToJavaModifiedUTF8(&output, input);
		EXPECT_TRUE(output == "abc");
	}

	// ReplaceInvalidUTF8 must always return well-formed UTF-8, keeping the good parts.  This one
	// guards a WebSocket text frame (memory.readString reads arbitrary emulated memory), where a
	// single bad byte getting through disconnects conforming clients.
	{
		const std::string replacement = "\xEF\xBF\xBD";  // U+FFFD

		// Valid input is returned untouched, including 1/2/3/4-byte sequences.
		const std::string allValid = "abc \xC3\xA9 \xE2\x82\xAC \xF0\x9F\x8E\xAE";
		EXPECT_TRUE(ReplaceInvalidUTF8(allValid) == allValid);
		EXPECT_TRUE(ReplaceInvalidUTF8("") == "");

		// Unlike SanitizeUTF8, it keeps going past the bad byte instead of truncating there.
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("ab\xFF" "cd")) == "ab" + replacement + "cd");

		// One replacement per bad byte, and resynchronization on the next valid sequence.
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("\x80\x80")) == replacement + replacement);
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("\xC3")) == replacement);
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("\xC3?")) == replacement + "?");

		// Sequences that lenient decoders accept but that aren't legal UTF-8: overlong encodings,
		// surrogates, and anything past U+10FFFF.
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("\xC0\xAF")) == replacement + replacement);
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("\xE0\x80\xAF")) == replacement + replacement + replacement);
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("\xED\xA0\x80")) == replacement + replacement + replacement);
		EXPECT_TRUE(ReplaceInvalidUTF8(std::string("\xF4\x90\x80\x80")) == replacement + replacement + replacement + replacement);

		// Whatever the input, the output must itself survive a re-run unchanged - i.e. be valid.
		for (int b = 0; b < 256; ++b) {
			std::string input = "a";
			input += (char)b;
			input += "b";
			const std::string once = ReplaceInvalidUTF8(input);
			EXPECT_TRUE(ReplaceInvalidUTF8(once) == once);
		}
	}

	return true;
}

// PointerWrap is the savestate serializer. The same DoState() code runs in MEASURE, WRITE and READ
// mode, so mistakes here don't show up as compile errors - they show up as savestates that don't
// load, or worse. Everything read back came off disk and is therefore attacker-controllable, so the
// corrupt-input cases below matter as much as the round trips.

struct SerializerPOD {
	u32 a;
	s16 b;
	u8 c;
	float d;
};

// Held by pointer in a map below, the way a lot of HLE state is (sceMpeg's contexts, sceFont's
// fonts, sceKernelThread's pending calls, ...).
struct SerializerTestObj {
	u32 value = 0;
	void DoState(PointerWrap &p) {
		Do(p, value);
	}
};

// Shaped like real DoState() code: a versioned section, a few fields, and one field that only
// exists from version 2 on. Set version to 1 before serializing to produce an old-format buffer.
struct SerializerTestState {
	int version = 2;
	u32 a = 0;
	std::string name;
	std::vector<u32> values;
	int addedInV2 = 0;

	void DoState(PointerWrap &p) {
		PointerWrapSection s = p.Section("TestState", 1, version);
		if (!s)
			return;
		Do(p, a);
		Do(p, name);
		Do(p, values);
		if (s >= 2)
			Do(p, addedInV2);
	}
};

// Measures, then rewinds into a buffer of exactly the measured size - the same sequence
// CChunkFileReader::MeasureAndSavePtr() uses, so the measure-vs-write checkpoint machinery gets
// exercised as well. Returns false if either pass reported an error or the two disagreed.
template <class Func>
static bool SerializerWrite(std::vector<u8> *out, Func f) {
	u8 *ptr = nullptr;
	PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);
	f(p);
	if (p.Failed())
		return false;
	// Fill with junk so a field the write pass forgets shows up as garbage rather than zero.
	out->assign(p.Offset(), 0xCD);
	p.RewindForWrite(out->empty() ? nullptr : &(*out)[0]);
	f(p);
	return p.CheckAfterWrite() && !p.Failed();
}

// Reads out of a copy of the buffer with the read end set, which is what LoadPtr() does and what
// all the bounds checks depend on.
template <class Func>
static PointerWrap::Error SerializerRead(const std::vector<u8> &buf, Func f) {
	std::vector<u8> copy = buf;
	u8 *ptr = copy.empty() ? nullptr : &copy[0];
	PointerWrap p(&ptr, PointerWrap::MODE_READ);
	if (!copy.empty())
		p.SetReadEnd(&copy[0] + copy.size());
	f(p);
	return p.error;
}

// A buffer whose first four bytes are a length/count field, for feeding hand-corrupted values in.
static std::vector<u8> SerializerBufferWithCount(int count, size_t totalSize) {
	std::vector<u8> buf(totalSize < sizeof(int) ? sizeof(int) : totalSize, 0);
	memcpy(&buf[0], &count, sizeof(int));
	return buf;
}

bool TestSerializer() {
	// Plain values and PODs survive a measure/write/read round trip, and the measure pass agrees
	// with the write pass about the size.
	{
		SerializerPOD pod{ 0x12345678, -1234, 0xAB, 1.5f };
		u32 plain = 0xDEADBEEF;
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) {
			Do(p, plain);
			Do(p, pod);
		}));
		EXPECT_EQ_INT((int)buf.size(), (int)(sizeof(u32) + sizeof(SerializerPOD)));

		u32 outPlain = 0;
		SerializerPOD outPod{};
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) {
			Do(p, outPlain);
			Do(p, outPod);
		}), (int)PointerWrap::ERROR_NONE);
		EXPECT_EQ_HEX(outPlain, plain);
		EXPECT_EQ_HEX(outPod.a, pod.a);
		EXPECT_EQ_INT(outPod.b, pod.b);
		EXPECT_EQ_INT(outPod.c, pod.c);
		EXPECT_EQ_FLOAT(outPod.d, pod.d);
	}

	// Strings, including the empty one and one with an embedded NUL - the length is serialized
	// separately, so the NUL shouldn't truncate anything.
	{
		std::string empty;
		std::string normal = "hello savestate";
		std::string embedded("a\0b", 3);
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) {
			Do(p, empty);
			Do(p, normal);
			Do(p, embedded);
		}));

		std::string outEmpty = "junk", outNormal, outEmbedded;
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) {
			Do(p, outEmpty);
			Do(p, outNormal);
			Do(p, outEmbedded);
		}), (int)PointerWrap::ERROR_NONE);
		EXPECT_TRUE(outEmpty.empty());
		EXPECT_EQ_STR(outNormal, normal);
		EXPECT_EQ_INT((int)outEmbedded.size(), 3);
		EXPECT_TRUE(outEmbedded == embedded);
	}

	// The containers that DoState() code actually uses.
	{
		std::vector<u32> vec{ 1, 2, 3, 0xFFFFFFFF };
		std::vector<std::string> strs{ "one", "", "three" };
		std::map<u32, u32> map{ { 5, 50 }, { 1, 10 }, { 9, 90 } };
		std::set<u32> set{ 7, 3, 11 };
		std::list<u32> list{ 4, 5, 6 };
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) {
			Do(p, vec);
			Do(p, strs);
			Do(p, map);
			Do(p, set);
			Do(p, list);
		}));

		// Deliberately non-empty to start with, so a load that forgets to clear shows up.
		std::vector<u32> outVec{ 99, 99 };
		std::vector<std::string> outStrs{ "junk" };
		std::map<u32, u32> outMap{ { 123, 456 } };
		std::set<u32> outSet{ 123 };
		std::list<u32> outList{ 99 };
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) {
			Do(p, outVec);
			Do(p, outStrs);
			Do(p, outMap);
			Do(p, outSet);
			Do(p, outList);
		}), (int)PointerWrap::ERROR_NONE);
		EXPECT_TRUE(outVec == vec);
		EXPECT_TRUE(outStrs == strs);
		EXPECT_TRUE(outMap == map);
		EXPECT_TRUE(outSet == set);
		EXPECT_TRUE(outList == list);
	}

	// Sections: a matching title and an acceptable version give a usable section, and the marker
	// the section destructor writes lines up on read.
	{
		SerializerTestState state;
		state.a = 0x1234;
		state.name = "statename";
		state.values = { 10, 20 };
		state.addedInV2 = 77;
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) { state.DoState(p); }));

		SerializerTestState out;
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { out.DoState(p); }), (int)PointerWrap::ERROR_NONE);
		EXPECT_EQ_HEX(out.a, state.a);
		EXPECT_EQ_STR(out.name, state.name);
		EXPECT_TRUE(out.values == state.values);
		EXPECT_EQ_INT(out.addedInV2, state.addedInV2);

		// A section written by a newer build than we understand must be refused, not
		// misinterpreted - this is what stops a future savestate from being read as garbage.
		bool sectionUsable = true;
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) {
			PointerWrapSection s = p.Section("TestState", 1, 1);
			sectionUsable = (bool)s;
		}), (int)PointerWrap::ERROR_FAILURE);
		EXPECT_FALSE(sectionUsable);

		// So must a different section title where we expected this one.
		sectionUsable = true;
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) {
			PointerWrapSection s = p.Section("SomethingElse", 1, 2);
			sectionUsable = (bool)s;
		}), (int)PointerWrap::ERROR_FAILURE);
		EXPECT_FALSE(sectionUsable);
	}

	// The backwards compatibility mechanism itself: a version 1 buffer read by version 2 code
	// yields a version 1 section, and the field that didn't exist yet keeps its default.
	{
		SerializerTestState old;
		old.version = 1;
		old.a = 0xAAAA;
		old.name = "old";
		old.values = { 1 };
		old.addedInV2 = 12345;  // Not written at version 1.
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) { old.DoState(p); }));

		SerializerTestState out;  // version 2, addedInV2 defaults to 0
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { out.DoState(p); }), (int)PointerWrap::ERROR_NONE);
		EXPECT_EQ_HEX(out.a, old.a);
		EXPECT_EQ_STR(out.name, old.name);
		EXPECT_EQ_INT(out.addedInV2, 0);
	}

	// A truncated savestate has to fail cleanly at every possible cut point rather than read past
	// the end of the buffer. This is the case a corrupt file on disk actually produces.
	{
		SerializerTestState state;
		state.a = 0x5555;
		state.name = "truncate me";
		state.values = { 1, 2, 3, 4, 5 };
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) { state.DoState(p); }));

		for (size_t cut = 1; cut < buf.size(); ++cut) {
			std::vector<u8> truncated(buf.begin(), buf.begin() + cut);
			SerializerTestState out;
			const PointerWrap::Error err = SerializerRead(truncated, [&](PointerWrap &p) { out.DoState(p); });
			if (err != PointerWrap::ERROR_FAILURE) {
				printf("Truncating to %d of %d bytes was accepted\n", (int)cut, (int)buf.size());
				return false;
			}
		}
	}

	// Hand-corrupted counts and lengths. In each case there is nowhere near enough buffer left for
	// what the header claims, so the load must be refused before anything is allocated or copied.
	{
		// A vector claiming four billion elements.
		{
			std::vector<u8> buf = SerializerBufferWithCount((int)0xFFFFFFFF, 64);
			std::vector<u32> out;
			EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { Do(p, out); }), (int)PointerWrap::ERROR_FAILURE);
			EXPECT_TRUE(out.empty());
		}
		// A map, a set and a list claiming the same.
		{
			std::vector<u8> buf = SerializerBufferWithCount((int)0xFFFFFFFF, 64);
			std::map<u32, u32> outMap;
			std::set<u32> outSet;
			std::list<u32> outList;
			EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { Do(p, outMap); }), (int)PointerWrap::ERROR_FAILURE);
			EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { Do(p, outSet); }), (int)PointerWrap::ERROR_FAILURE);
			EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { Do(p, outList); }), (int)PointerWrap::ERROR_FAILURE);
			EXPECT_TRUE(outMap.empty());
			EXPECT_TRUE(outSet.empty());
			EXPECT_TRUE(outList.empty());
		}
		// Strings: negative, absurd, zero (there is always at least a NUL byte), and merely longer
		// than what's left in the buffer.
		{
			const int lengths[] = { -1, 0x7FFFFFFF, 0, 1000 };
			for (size_t i = 0; i < ARRAY_SIZE(lengths); ++i) {
				std::vector<u8> buf = SerializerBufferWithCount(lengths[i], 64);
				std::string out = "untouched";
				const PointerWrap::Error err = SerializerRead(buf, [&](PointerWrap &p) { Do(p, out); });
				if (err != PointerWrap::ERROR_FAILURE) {
					printf("String length %d was accepted\n", lengths[i]);
					return false;
				}
			}
		}
		// u16strings are measured in bytes, so on top of the above they can also claim a length
		// that isn't a whole number of characters.
		{
			const int lengths[] = { -1, 0x7FFFFFFF, 0, 1, 3, 1000 };
			for (size_t i = 0; i < ARRAY_SIZE(lengths); ++i) {
				std::vector<u8> buf = SerializerBufferWithCount(lengths[i], 64);
				std::u16string out = u"untouched";
				const PointerWrap::Error err = SerializerRead(buf, [&](PointerWrap &p) { Do(p, out); });
				if (err != PointerWrap::ERROR_FAILURE) {
					printf("u16string byte length %d was accepted\n", lengths[i]);
					return false;
				}
			}
		}
	}

	// Maps of pointers, which is how most HLE contexts are savestated. Loading deletes whatever
	// was in the map before reading the new contents, so bailing out on a corrupt count must not
	// leave the freed pointers behind - the next access to them, or the destructor, would be a
	// use-after-free.
	{
		std::map<u32, SerializerTestObj *> ptrMap;
		ptrMap[1] = new SerializerTestObj();
		ptrMap[1]->value = 0x1111;
		ptrMap[7] = new SerializerTestObj();
		ptrMap[7]->value = 0x7777;
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) { Do(p, ptrMap); }));

		std::map<u32, SerializerTestObj *> outMap;
		outMap[99] = new SerializerTestObj();
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { Do(p, outMap); }), (int)PointerWrap::ERROR_NONE);
		EXPECT_EQ_INT((int)outMap.size(), 2);
		EXPECT_EQ_HEX(outMap[1]->value, (u32)0x1111);
		EXPECT_EQ_HEX(outMap[7]->value, (u32)0x7777);

		std::vector<u8> badBuf = SerializerBufferWithCount((int)0xFFFFFFFF, 64);
		EXPECT_EQ_INT((int)SerializerRead(badBuf, [&](PointerWrap &p) { Do(p, outMap); }), (int)PointerWrap::ERROR_FAILURE);
		const bool leftDangling = !outMap.empty();
		outMap.clear();  // Must not delete these - the loader already did.
		EXPECT_FALSE(leftDangling);

		for (const std::pair<const u32, SerializerTestObj *> &entry : ptrMap)
			delete entry.second;
	}

	// Valid u16strings still round trip, including the empty one.
	{
		std::u16string empty;
		std::u16string text = u"unicode";
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) {
			Do(p, empty);
			Do(p, text);
		}));
		std::u16string outEmpty = u"junk", outText;
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) {
			Do(p, outEmpty);
			Do(p, outText);
		}), (int)PointerWrap::ERROR_NONE);
		EXPECT_TRUE(outEmpty.empty());
		EXPECT_TRUE(outText == text);
	}

	// Once a failure is latched the serializer drops to MODE_NOOP and stops touching the caller's
	// data, so the rest of a broken savestate can be walked without doing damage. A warning, on the
	// other hand, must not stop anything.
	{
		std::vector<u8> buf = SerializerBufferWithCount(-1, 64);
		u32 shouldBeUntouched = 0x11111111;
		std::string alsoUntouched = "keepme";
		bool wentNoop = false;
		const PointerWrap::Error err = SerializerRead(buf, [&](PointerWrap &p) {
			std::string bad;
			Do(p, bad);  // fails: negative length
			wentNoop = p.mode == PointerWrap::MODE_NOOP;
			Do(p, shouldBeUntouched);
			Do(p, alsoUntouched);
		});
		EXPECT_EQ_INT((int)err, (int)PointerWrap::ERROR_FAILURE);
		EXPECT_TRUE(wentNoop);
		EXPECT_EQ_HEX(shouldBeUntouched, (u32)0x11111111);
		EXPECT_EQ_STR(alsoUntouched, std::string("keepme"));

		u8 *ptr = &buf[0];
		PointerWrap p(&ptr, PointerWrap::MODE_READ);
		p.SetError(PointerWrap::ERROR_WARNING);
		EXPECT_FALSE(p.Failed());
		EXPECT_EQ_INT((int)p.mode, (int)PointerWrap::MODE_READ);
	}

	// A marker that doesn't match means the writer and reader disagree about the layout, which has
	// to be a hard failure - carrying on would read every following field from the wrong offset.
	{
		std::vector<u8> buf;
		EXPECT_TRUE(SerializerWrite(&buf, [&](PointerWrap &p) { p.DoMarker("Thing", 0x1234); }));
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { p.DoMarker("Thing", 0x1234); }), (int)PointerWrap::ERROR_NONE);
		EXPECT_EQ_INT((int)SerializerRead(buf, [&](PointerWrap &p) { p.DoMarker("Thing", 0x4321); }), (int)PointerWrap::ERROR_FAILURE);
	}

	// The measure pass and the write pass have to visit the same sections at the same offsets;
	// CheckAfterWrite() exists to catch DoState() code whose behaviour depends on something that
	// changed in between. Fake exactly that and make sure it's noticed rather than silently
	// producing a savestate that can't be loaded.
	{
		int pass = 0;
		std::vector<u8> buf;
		EXPECT_FALSE(SerializerWrite(&buf, [&](PointerWrap &p) {
			u32 v = 0;
			PointerWrapSection s = p.Section(pass++ == 0 ? "SectionA" : "SectionB", 1);
			if (s)
				Do(p, v);
		}));
	}

	return true;
}

bool TestMemBlockInfoSaveState() {
	MemBlockInfoInit();
	MemBlockOverrideDetailed();

	// Split the single initial slab (which spans the whole address space) into several
	// pieces, so the savestate has more than just the first slab.
	NotifyMemInfo(MemBlockFlags::ALLOC, 0x08800000, 0x1000, "InitialTag", 10);
	NotifyMemInfo(MemBlockFlags::ALLOC, 0x08810000, 0x1000, "SecondTag", 9);
	// FindMemInfo flushes pending notifications into the actual slab maps.
	FindMemInfo(0x08800000, 0x20000);

	// Round-trip through the savestate serializer.  This used to leave every slab but
	// the first with an uninitialized tagLen, which MemSlabMap::Split() would later use
	// as an unbounded memcpy length into a fixed 128 byte buffer, corrupting the heap.
	uint8_t *measurePtr = nullptr;
	PointerWrap pm(&measurePtr, PointerWrap::MODE_MEASURE);
	MemBlockInfoDoState(pm);
	size_t stateSize = (size_t)measurePtr;
	EXPECT_TRUE(stateSize > 0);

	std::vector<uint8_t> buffer(stateSize);
	uint8_t *writePtr = &buffer[0];
	PointerWrap pw(&writePtr, PointerWrap::MODE_WRITE);
	MemBlockInfoDoState(pw);

	uint8_t *readPtr = &buffer[0];
	PointerWrap pr(&readPtr, PointerWrap::MODE_READ);
	MemBlockInfoDoState(pr);

	// Force a split on a slab that was just loaded from the savestate - this is what used
	// to corrupt the heap (or crash outright) before the fix.
	NotifyMemInfo(MemBlockFlags::ALLOC, 0x08800100, 0x10, "SplitTag", 8);
	auto results = FindMemInfo(0x08800000, 0x20000);
	EXPECT_TRUE(!results.empty());

	MemBlockReleaseDetailed();
	MemBlockInfoShutdown();
	return true;
}

// Covers BreakpointManager::ChangeBreakPointAddress(), which the ImDebugger uses to relocate a
// breakpoint the user is editing. Only the pure bookkeeping is exercised here - there's no JIT in
// this build, so the cache invalidation it also does is a no-op.
bool TestBreakpoints() {
	const u32 kAddrA = 0x08804000;
	const u32 kAddrB = 0x08804100;
	const u32 kAddrC = 0x08804200;

	g_breakpoints.AddBreakPoint(kAddrA);
	g_breakpoints.ChangeBreakPoint(kAddrA, BreakAction(BREAK_ACTION_PAUSE | BREAK_ACTION_LOG));
	// Pretend it tripped a few times, so the reset below is actually testing something.
	g_breakpoints.GetBreakpointRefs()[0].numHits = 7;

	// A plain move: gone from the old address, present at the new one, action carried over, and the
	// hit count (which belonged to the old address) reset.
	EXPECT_TRUE(g_breakpoints.ChangeBreakPointAddress(kAddrA, kAddrB));
	EXPECT_FALSE(g_breakpoints.IsAddressBreakPoint(kAddrA));
	EXPECT_TRUE(g_breakpoints.IsAddressBreakPoint(kAddrB));
	{
		std::vector<BreakPoint> bps = g_breakpoints.GetBreakpoints();
		EXPECT_EQ_INT((int)bps.size(), 1);
		EXPECT_EQ_INT((int)bps[0].action, (int)(BREAK_ACTION_PAUSE | BREAK_ACTION_LOG));
		EXPECT_EQ_INT((int)bps[0].numHits, 0);
	}

	// Moving onto an address that already has a breakpoint must be refused rather than creating a
	// duplicate - FindBreakpoint() only ever returns one entry per address, so the other would be
	// silently dead. Neither breakpoint should move.
	g_breakpoints.AddBreakPoint(kAddrC);
	EXPECT_FALSE(g_breakpoints.ChangeBreakPointAddress(kAddrB, kAddrC));
	EXPECT_TRUE(g_breakpoints.IsAddressBreakPoint(kAddrB));
	EXPECT_TRUE(g_breakpoints.IsAddressBreakPoint(kAddrC));
	EXPECT_EQ_INT((int)g_breakpoints.GetBreakpoints().size(), 2);

	// Nothing to move.
	EXPECT_FALSE(g_breakpoints.ChangeBreakPointAddress(kAddrA, 0x08804300));
	EXPECT_FALSE(g_breakpoints.IsAddressBreakPoint(0x08804300));

	// Moving somewhere it already is succeeds and does nothing.
	EXPECT_TRUE(g_breakpoints.ChangeBreakPointAddress(kAddrB, kAddrB));
	EXPECT_TRUE(g_breakpoints.IsAddressBreakPoint(kAddrB));
	EXPECT_EQ_INT((int)g_breakpoints.GetBreakpoints().size(), 2);

	g_breakpoints.RemoveBreakPoint(kAddrB);
	g_breakpoints.RemoveBreakPoint(kAddrC);
	EXPECT_EQ_INT((int)g_breakpoints.GetBreakpoints().size(), 0);
	return true;
}

// The one-shot breakpoint behind step-over/step-out/run-until. It deliberately lives outside the
// user's breakpoint list, so the two must not be able to see or clobber each other.
bool TestTempBreakpoints() {
	const u32 kAddrA = 0x08804000;
	const u32 kAddrB = 0x08804100;

	// ExecBreakPoint's log path asks the symbol map to describe the address, and the unit test
	// build leaves g_symbolMap null (the emulator always creates one at boot).
	SymbolMap symbolMap;
	g_symbolMap = &symbolMap;

	g_breakpoints.SetTempBreakPoint(kAddrA);
	EXPECT_TRUE(g_breakpoints.HasTempBreakPoint());
	// Invisible to the user's list, but the interpreter and JIT still have to check the address.
	EXPECT_EQ_INT((int)g_breakpoints.GetBreakpoints().size(), 0);
	EXPECT_FALSE(g_breakpoints.IsAddressBreakPoint(kAddrA));
	EXPECT_TRUE(g_breakpoints.NeedsBreakCheckAt(kAddrA));
	EXPECT_TRUE(g_breakpoints.RangeContainsBreakPoint(kAddrA - 4, 16));
	// This one is the trap: with no user breakpoints at all, the run loops and the JIT skip
	// breakpoint checking entirely unless HasBreakPoints() accounts for the temporary one.
	EXPECT_TRUE(g_breakpoints.HasBreakPoints());

	// Only one at a time - a second request replaces rather than accumulating.
	g_breakpoints.SetTempBreakPoint(kAddrB);
	EXPECT_FALSE(g_breakpoints.NeedsBreakCheckAt(kAddrA));
	EXPECT_TRUE(g_breakpoints.NeedsBreakCheckAt(kAddrB));

	// A log-only user breakpoint at the same address is the case that used to break stepping: the
	// user breakpoint must log without stopping, and the pending step must still complete.
	g_breakpoints.AddBreakPoint(kAddrB);
	g_breakpoints.ChangeBreakPoint(kAddrB, BREAK_ACTION_LOG);
	EXPECT_TRUE(g_breakpoints.HasTempBreakPoint());
	{
		std::vector<BreakPoint> bps = g_breakpoints.GetBreakpoints();
		EXPECT_EQ_INT((int)bps.size(), 1);
		EXPECT_EQ_INT((int)bps[0].action, (int)BREAK_ACTION_LOG);
	}
	{
		// Both fire: the log from the user breakpoint, the pause from the temporary one.
		BreakAction action = g_breakpoints.ExecBreakPoint(kAddrB);
		EXPECT_TRUE((action & BREAK_ACTION_LOG) != 0);
		EXPECT_TRUE((action & BREAK_ACTION_PAUSE) != 0);
		EXPECT_EQ_INT((int)g_breakpoints.GetBreakpoints()[0].numHits, 1);
	}

	// Removing the user breakpoint must not take the temporary one with it, and vice versa.
	EXPECT_TRUE(g_breakpoints.HasTempBreakPoint());
	g_breakpoints.RemoveBreakPoint(kAddrB);
	EXPECT_EQ_INT((int)g_breakpoints.GetBreakpoints().size(), 0);
	EXPECT_TRUE(g_breakpoints.HasTempBreakPoint());
	EXPECT_TRUE(g_breakpoints.NeedsBreakCheckAt(kAddrB));

	g_breakpoints.ClearTempBreakPoint();
	EXPECT_FALSE(g_breakpoints.HasTempBreakPoint());
	EXPECT_FALSE(g_breakpoints.NeedsBreakCheckAt(kAddrB));
	EXPECT_FALSE(g_breakpoints.HasBreakPoints());

	// A user breakpoint alone still behaves normally after all that.
	g_breakpoints.AddBreakPoint(kAddrA);
	EXPECT_TRUE(g_breakpoints.IsAddressBreakPoint(kAddrA));
	EXPECT_TRUE((g_breakpoints.ExecBreakPoint(kAddrA) & BREAK_ACTION_PAUSE) != 0);
	g_breakpoints.RemoveBreakPoint(kAddrA);
	EXPECT_FALSE(g_breakpoints.HasBreakPoints());

	g_symbolMap = nullptr;
	return true;
}

// BlockAllocator backs sceKernelAllocPartitionMemory and friends. It's pure address bookkeeping -
// no real memory involved - which makes it cheap to check hard: after any sequence of operations
// the blocks must still tile the range exactly, and the free-space accessors must match reality.

// Rebuilds the block list through the public accessors and checks it tiles [start, start+size)
// with no gaps, overlaps or strays, then cross-checks GetTotalFreeBytes/GetLargestFreeBlockSize
// against what is actually in the list.
static bool ValidateAllocator(BlockAllocator &a, u32 rangeStart, u32 rangeSize) {
	u32 addr = rangeStart;
	u32 totalFree = 0;
	u32 largestFree = 0;
	int guard = 0;
	while (addr < rangeStart + rangeSize) {
		const u32 blockStart = a.GetBlockStartFromAddress(addr);
		const u32 blockSize = a.GetBlockSizeFromAddress(addr);
		if (blockStart != addr)
			return false;  // a gap, or the block misreports where it starts
		if (blockSize == 0 || blockSize == (u32)-1)
			return false;
		if ((u64)blockStart + blockSize > (u64)rangeStart + rangeSize)
			return false;  // runs off the end of the range
		if (a.IsBlockFree(addr)) {
			totalFree += blockSize;
			if (blockSize > largestFree)
				largestFree = blockSize;
		}
		addr += blockSize;
		if (++guard > 200000)
			return false;  // cycle in the list
	}
	if (addr != rangeStart + rangeSize)
		return false;  // last block overshot the end
	if (a.GetTotalFreeBytes() != totalFree)
		return false;
	if (a.GetLargestFreeBlockSize() != largestFree)
		return false;
	return true;
}

bool TestBlockAllocator() {
	const u32 kStart = 0x08800000;
	const u32 kSize = 0x00100000;  // 1MB
	const u32 kGrain = 256;

	// A fresh allocator is one free block covering everything.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), (int)kSize);
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
		EXPECT_TRUE(a.IsBlockFree(kStart));
	}

	// Bottom-up allocation starts at the bottom; top-down ends at the top.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);

		u32 sizeA = 0x1000;
		const u32 addrA = a.Alloc(sizeA, false, "bottom");
		EXPECT_EQ_INT((int)addrA, (int)kStart);
		EXPECT_FALSE(a.IsBlockFree(addrA));

		u32 sizeB = 0x1000;
		const u32 addrB = a.Alloc(sizeB, true, "top");
		EXPECT_EQ_INT((int)(addrB + sizeB), (int)(kStart + kSize));
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), (int)(kSize - sizeA - sizeB));

		EXPECT_TRUE(a.Free(addrA));
		EXPECT_TRUE(a.Free(addrB));
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// Sizes are rounded up to the grain, and the caller is told about it.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		u32 size = 1;
		const u32 addr = a.Alloc(size, false, "tiny");
		EXPECT_FALSE(addr == (u32)-1);
		EXPECT_EQ_INT((int)size, (int)kGrain);
		EXPECT_EQ_INT((int)a.GetBlockSizeFromAddress(addr), (int)kGrain);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// Nonsense sizes are refused rather than wrapping into something huge.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		u32 zero = 0;
		EXPECT_EQ_INT((int)a.Alloc(zero, false, "zero"), -1);
		u32 huge = kSize + 1;
		EXPECT_EQ_INT((int)a.Alloc(huge, false, "huge"), -1);
		// A failed allocation must not have disturbed anything.
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), (int)kSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// Freeing the middle of three leaves a hole; freeing its neighbours merges it all back.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		u32 s1 = 0x10000, s2 = 0x10000, s3 = 0x10000;
		const u32 a1 = a.Alloc(s1, false, "1");
		const u32 a2 = a.Alloc(s2, false, "2");
		const u32 a3 = a.Alloc(s3, false, "3");
		EXPECT_TRUE(a1 < a2 && a2 < a3);

		EXPECT_TRUE(a.Free(a2));
		EXPECT_TRUE(a.IsBlockFree(a2));
		EXPECT_EQ_INT((int)a.GetBlockSizeFromAddress(a2), (int)s2);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));

		EXPECT_TRUE(a.Free(a1));
		// a1 and a2 are adjacent and both free now, so they must have become one block.
		EXPECT_EQ_INT((int)a.GetBlockStartFromAddress(a2), (int)a1);
		EXPECT_EQ_INT((int)a.GetBlockSizeFromAddress(a1), (int)(s1 + s2));
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));

		EXPECT_TRUE(a.Free(a3));
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// Double free, and freeing an address that was never allocated, must fail rather than corrupt.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		u32 size = 0x1000;
		const u32 addr = a.Alloc(size, false, "once");
		EXPECT_TRUE(a.Free(addr));
		EXPECT_FALSE(a.Free(addr));
		EXPECT_FALSE(a.Free(kStart + kSize + 0x1000));  // outside the range entirely
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// AllocAt places a block exactly, and refuses if it is already taken.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		const u32 target = kStart + 0x20000;
		const u32 got = a.AllocAt(target, 0x1000, "at");
		EXPECT_EQ_INT((int)got, (int)target);
		EXPECT_FALSE(a.IsBlockFree(target));
		EXPECT_TRUE(a.IsBlockFree(kStart));  // the space below it stays free
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));

		EXPECT_EQ_INT((int)a.AllocAt(target, 0x1000, "again"), -1);
		EXPECT_EQ_INT((int)a.AllocAt(target + 0x800, 0x100, "overlap"), -1);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));

		EXPECT_TRUE(a.Free(target));
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
	}

	// AllocAligned honours a coarser alignment than the allocator's own grain.
	{
		BlockAllocator a(16);
		a.Init(kStart + 16, kSize, false);  // deliberately not 4K-aligned to start with
		u32 skew = 0x30;
		EXPECT_FALSE(a.Alloc(skew, false, "skew") == (u32)-1);

		u32 size = 0x1000;
		const u32 addr = a.AllocAligned(size, 0x1000, 0x1000, false, "aligned");
		EXPECT_FALSE(addr == (u32)-1);
		EXPECT_EQ_INT((int)(addr & 0xFFF), 0);
		EXPECT_TRUE(ValidateAllocator(a, kStart + 16, kSize));

		u32 topSize = 0x1000;
		const u32 topAddr = a.AllocAligned(topSize, 0x1000, 0x1000, true, "aligned-top");
		EXPECT_FALSE(topAddr == (u32)-1);
		EXPECT_EQ_INT((int)(topAddr & 0xFFF), 0);
		EXPECT_TRUE(ValidateAllocator(a, kStart + 16, kSize));
	}

	// Fill the range completely, then drain it - nothing should leak or go missing.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		std::vector<u32> addrs;
		for (;;) {
			u32 size = 0x4000;
			const u32 addr = a.Alloc(size, false, "fill");
			if (addr == (u32)-1)
				break;
			addrs.push_back(addr);
		}
		EXPECT_EQ_INT((int)addrs.size(), (int)(kSize / 0x4000));
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), 0);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));

		for (u32 addr : addrs)
			EXPECT_TRUE(a.Free(addr));
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), (int)kSize);
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// AllocAt with a position that is not on the grain: it must still round down to a whole block
	// and keep the range tiled, and it reports back how much the caller actually got from their
	// requested position (which is less than a whole block, since the block starts lower).
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		const u32 unaligned = kStart + 0x2010;
		u32 size = 0x100;
		const u32 got = a.AllocAt(unaligned, size, "unaligned");
		EXPECT_EQ_INT((int)got, (int)unaligned);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
		// The block it landed in starts at the grain boundary below.
		EXPECT_EQ_INT((int)a.GetBlockStartFromAddress(unaligned), (int)(kStart + 0x2000));
		EXPECT_FALSE(a.IsBlockFree(unaligned));
		// Free() takes any address inside the block, so the address AllocAt handed back works.
		EXPECT_TRUE(a.Free(got));
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// FreeExact only accepts the true start of a block, so an unaligned AllocAt address is
	// rejected - worth pinning down, since Free() and FreeExact() differ here.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		const u32 unaligned = kStart + 0x2010;
		u32 size = 0x100;
		EXPECT_EQ_INT((int)a.AllocAt(unaligned, size, "unaligned"), (int)unaligned);
		EXPECT_FALSE(a.FreeExact(unaligned));
		EXPECT_TRUE(a.FreeExact(kStart + 0x2000));
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// A range whose size is not a multiple of the grain. The leftover tail can never be handed
	// out, but it must not break the tiling or the accounting.
	{
		BlockAllocator a(kGrain);
		const u32 oddSize = 0x10000 + 0x10;
		a.Init(kStart, oddSize, false);
		EXPECT_TRUE(ValidateAllocator(a, kStart, oddSize));
		std::vector<u32> addrs;
		for (;;) {
			u32 size = 0x1000;
			const u32 addr = a.Alloc(size, false, "odd");
			if (addr == (u32)-1)
				break;
			addrs.push_back(addr);
			EXPECT_TRUE(ValidateAllocator(a, kStart, oddSize));
		}
		for (size_t j = 0; j < addrs.size(); ++j)
			EXPECT_TRUE(a.Free(addrs[j]));
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), (int)oddSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, oddSize));
	}

	// Churn again, this time mixing in aligned allocations and AllocAt so the block list gets into
	// shapes the plain alloc/free loop never produces.
	{
		BlockAllocator a(16);
		a.Init(kStart, kSize, false);
		std::vector<u32> live;
		u32 rng = 987654321;
		auto next = [&rng]() { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7FFF; };

		for (int i = 0; i < 4000; ++i) {
			const int op = next() % 100;
			if (op < 30 && !live.empty()) {
				const size_t idx = next() % live.size();
				EXPECT_TRUE(a.Free(live[idx]));
				live.erase(live.begin() + idx);
			} else if (op < 60) {
				u32 size = ((next() % 32) + 1) * 16;
				const u32 addr = a.Alloc(size, (next() % 2) != 0, "churn2");
				if (addr != (u32)-1)
					live.push_back(addr);
			} else if (op < 90) {
				// Alignments of 16, 64, 256, 1024, 4096.
				const u32 align = 16u << ((next() % 5) * 2);
				u32 size = ((next() % 32) + 1) * 16;
				const u32 addr = a.AllocAligned(size, align, align, (next() % 2) != 0, "aligned2");
				if (addr != (u32)-1) {
					if ((addr & (align - 1)) != 0) {
						printf("AllocAligned returned %08x for alignment %08x at iteration %d\n", addr, align, i);
						return false;
					}
					live.push_back(addr);
				}
			} else {
				const u32 pos = kStart + ((next() % (kSize / 0x1000)) * 0x1000);
				const u32 addr = a.AllocAt(pos, 0x800, "at2");
				if (addr != (u32)-1)
					live.push_back(addr);
			}
			if (!ValidateAllocator(a, kStart, kSize)) {
				printf("BlockAllocator invariant broken at iteration %d (op %d)\n", i, op);
				return false;
			}
		}

		for (size_t j = 0; j < live.size(); ++j)
			EXPECT_TRUE(a.Free(live[j]));
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), (int)kSize);
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	// Randomised churn. Fixed seed so a failure is reproducible; the point is to reach block
	// layouts hand-written cases would not, while checking the invariants after every step.
	{
		BlockAllocator a(kGrain);
		a.Init(kStart, kSize, false);
		std::vector<std::pair<u32, u32> > live;  // address, size
		u32 rng = 12345;
		auto next = [&rng]() { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7FFF; };

		for (int i = 0; i < 3000; ++i) {
			const bool doAlloc = live.empty() || (next() % 100) < 55;
			if (doAlloc) {
				u32 size = ((next() % 64) + 1) * kGrain;
				const bool fromTop = (next() % 2) != 0;
				const u32 addr = a.Alloc(size, fromTop, "churn");
				if (addr != (u32)-1) {
					// It must not overlap anything already handed out.
					for (size_t j = 0; j < live.size(); ++j) {
						const bool overlaps = addr < live[j].first + live[j].second && live[j].first < addr + size;
						EXPECT_FALSE(overlaps);
					}
					live.push_back(std::make_pair(addr, size));
				}
			} else {
				const size_t idx = next() % live.size();
				EXPECT_TRUE(a.Free(live[idx].first));
				live.erase(live.begin() + idx);
			}
			if (!ValidateAllocator(a, kStart, kSize)) {
				printf("BlockAllocator invariant broken at iteration %d\n", i);
				return false;
			}
		}

		for (size_t j = 0; j < live.size(); ++j)
			EXPECT_TRUE(a.Free(live[j].first));
		// Everything given back means one free block again - if merging ever misses a case, this
		// is where it shows up.
		EXPECT_EQ_INT((int)a.GetTotalFreeBytes(), (int)kSize);
		EXPECT_EQ_INT((int)a.GetLargestFreeBlockSize(), (int)kSize);
		EXPECT_TRUE(ValidateAllocator(a, kStart, kSize));
	}

	return true;
}

// SymbolMap holds the function/data/label tables the debugger and disassembler read. Symbols are
// stored relative to a module so they survive that module being unloaded and reloaded elsewhere,
// and only symbols belonging to a currently-loaded module count as "active". That indirection is
// where the surprises live, so most of this is about module lifetime and the shared label table.
bool TestSymbolMap() {
	const u32 kModStart = 0x08804000;
	const u32 kModSize = 0x00010000;

	// Functions are found by containing address, not just by their start.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		map.AddFunction("func_a", kModStart + 0x100, 0x40);
		map.AddFunction("func_b", kModStart + 0x200, 0x80);
		map.SortSymbols();

		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x100), (int)(kModStart + 0x100));
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x120), (int)(kModStart + 0x100));
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x13C), (int)(kModStart + 0x100));
		// One past the end belongs to nobody.
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x140), (int)SymbolMap::INVALID_ADDRESS);
		EXPECT_EQ_INT((int)map.GetFunctionSize(kModStart + 0x100), 0x40);
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x27F), (int)(kModStart + 0x200));

		// AddFunction doubles as a label, so the name is reachable both ways.
		EXPECT_EQ_STR(map.GetLabelString(kModStart + 0x100), std::string("func_a"));
		u32 value = 0;
		EXPECT_TRUE(map.GetLabelValue("func_b", value));
		EXPECT_EQ_INT((int)value, (int)(kModStart + 0x200));
	}

	// SetFunctionSize and RemoveFunction.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		map.AddFunction("func", kModStart + 0x100, 0x40);
		map.SortSymbols();

		EXPECT_TRUE(map.SetFunctionSize(kModStart + 0x100, 0x80));
		EXPECT_EQ_INT((int)map.GetFunctionSize(kModStart + 0x100), 0x80);
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x170), (int)(kModStart + 0x100));

		EXPECT_TRUE(map.RemoveFunction(kModStart + 0x100, true));
		map.SortSymbols();
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x100), (int)SymbolMap::INVALID_ADDRESS);
		// Removing something that isn't there fails rather than doing damage.
		EXPECT_FALSE(map.RemoveFunction(kModStart + 0x100, true));
	}

	// Data symbols work the same way, and carry a type.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		map.AddData(kModStart + 0x400, 0x20, DATATYPE_WORD);
		map.SortSymbols();

		EXPECT_EQ_INT((int)map.GetDataStart(kModStart + 0x400), (int)(kModStart + 0x400));
		EXPECT_EQ_INT((int)map.GetDataStart(kModStart + 0x41F), (int)(kModStart + 0x400));
		EXPECT_EQ_INT((int)map.GetDataStart(kModStart + 0x420), (int)SymbolMap::INVALID_ADDRESS);
		EXPECT_EQ_INT((int)map.GetDataSize(kModStart + 0x400), 0x20);
		EXPECT_EQ_INT((int)map.GetDataType(kModStart + 0x400), (int)DATATYPE_WORD);
	}

	// Symbols only count as active while their module is loaded, and they come back - at the new
	// address - when it is loaded somewhere else. This is the whole point of storing them
	// module-relative.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		map.AddFunction("func", kModStart + 0x100, 0x40);
		map.SortSymbols();
		EXPECT_EQ_INT((int)map.GetAllActiveSymbols(ST_FUNCTION).size(), 1);

		map.UnloadModule(kModStart, kModSize);
		EXPECT_EQ_INT((int)map.GetAllActiveSymbols(ST_FUNCTION).size(), 0);
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x100), (int)SymbolMap::INVALID_ADDRESS);

		// Same module, different load address - the symbol should follow it.
		const u32 newStart = kModStart + 0x100000;
		map.AddModule("TEST", newStart, kModSize);
		map.SortSymbols();
		EXPECT_EQ_INT((int)map.GetAllActiveSymbols(ST_FUNCTION).size(), 1);
		EXPECT_EQ_INT((int)map.GetFunctionStart(newStart + 0x100), (int)(newStart + 0x100));
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x100), (int)SymbolMap::INVALID_ADDRESS);
	}

	// Symbols outside any module are stored against module index 0 ("absolute"), which is always
	// considered loaded - that's what makes labelling a heap or stack address work.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		const u32 outside = 0x0BFBF800;  // stack, well outside the module
		EXPECT_EQ_INT(map.GetModuleIndex(outside), -1);
		map.AddData(outside, 0x10, DATATYPE_BYTE, 0);
		map.AddLabel("stackthing", outside, 0);
		map.SortSymbols();

		EXPECT_EQ_INT((int)map.GetDataStart(outside), (int)outside);
		EXPECT_EQ_STR(map.GetLabelString(outside), std::string("stackthing"));
		// Unloading the module must not take an unrelated absolute symbol with it.
		map.UnloadModule(kModStart, kModSize);
		EXPECT_EQ_INT((int)map.GetDataStart(outside), (int)outside);
	}

	// Labels are one table shared by functions and data, and AddLabel deliberately leaves an
	// existing one alone. Pinning this down because it surprises people: hle.data.add reports the
	// name you asked for while the map keeps the old one, unless the caller forces it.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		const u32 addr = kModStart + 0x100;
		map.AddFunction("original", addr, 0x40);
		map.SortSymbols();
		EXPECT_EQ_STR(map.GetLabelString(addr), std::string("original"));

		map.AddLabel("replacement", addr);
		EXPECT_EQ_STR(map.GetLabelString(addr), std::string("original"));

		// SetLabelName is the way to actually change it...
		map.SetLabelName("replacement", addr);
		EXPECT_EQ_STR(map.GetLabelString(addr), std::string("replacement"));
		// ...and because the table is shared, that renamed the function too.
		std::vector<SymbolEntry> funcs = map.GetAllActiveSymbols(ST_FUNCTION);
		EXPECT_EQ_INT((int)funcs.size(), 1);
		EXPECT_EQ_STR(funcs[0].name, std::string("replacement"));
	}

	// Likewise, removing a data symbol with removeName drops the shared label, which is why
	// hle.data.remove has to check whether a function is using it first.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		const u32 addr = kModStart + 0x100;
		map.AddFunction("shared", addr, 0x40);
		map.AddData(addr, 0x10, DATATYPE_BYTE);
		map.SortSymbols();
		EXPECT_EQ_STR(map.GetLabelString(addr), std::string("shared"));

		EXPECT_TRUE(map.RemoveData(addr, true));
		map.SortSymbols();
		// The function is still there, but its name is gone with the label.
		EXPECT_EQ_INT((int)map.GetFunctionStart(addr), (int)addr);
		EXPECT_TRUE(map.GetLabelString(addr).empty());

		// Whereas removeName=false leaves the label for the function that still needs it.
		SymbolMap map2;
		map2.AddModule("TEST", kModStart, kModSize);
		map2.AddFunction("kept", addr, 0x40);
		map2.AddData(addr, 0x10, DATATYPE_BYTE);
		map2.SortSymbols();
		EXPECT_TRUE(map2.RemoveData(addr, false));
		map2.SortSymbols();
		EXPECT_EQ_STR(map2.GetLabelString(addr), std::string("kept"));
	}

	// GetSymbolInfo / GetDescription round out what the disassembler asks for.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		map.AddFunction("described", kModStart + 0x100, 0x40);
		map.SortSymbols();

		SymbolInfo info{};
		EXPECT_TRUE(map.GetSymbolInfo(&info, kModStart + 0x110, ST_FUNCTION));
		EXPECT_EQ_INT((int)info.address, (int)(kModStart + 0x100));
		EXPECT_EQ_INT((int)info.size, 0x40);
		EXPECT_FALSE(map.GetSymbolInfo(&info, kModStart + 0x900, ST_FUNCTION));
		EXPECT_EQ_STR(map.GetDescription(kModStart + 0x100), std::string("described"));
	}

	// Clear really clears, including the module table.
	{
		SymbolMap map;
		map.AddModule("TEST", kModStart, kModSize);
		map.AddFunction("func", kModStart + 0x100, 0x40);
		map.AddData(kModStart + 0x400, 0x20, DATATYPE_WORD);
		map.SortSymbols();
		EXPECT_EQ_INT((int)map.GetAllActiveSymbols(ST_FUNCTION).size(), 1);

		map.Clear();
		EXPECT_EQ_INT((int)map.GetAllActiveSymbols(ST_FUNCTION).size(), 0);
		EXPECT_EQ_INT((int)map.GetAllActiveSymbols(ST_DATA).size(), 0);
		EXPECT_EQ_INT((int)map.getAllModules().size(), 0);
		EXPECT_EQ_INT((int)map.GetFunctionStart(kModStart + 0x100), (int)SymbolMap::INVALID_ADDRESS);
	}

	// Version() is what the ImDebugger's symbol list uses to notice its cached copy went stale.
	{
		SymbolMap map;
		const uint32_t v0 = map.Version();
		map.AddModule("TEST", kModStart, kModSize);
		const uint32_t v1 = map.Version();
		EXPECT_TRUE(v0 != v1);
		map.AddFunction("func", kModStart + 0x100, 0x40);
		const uint32_t v2 = map.Version();
		EXPECT_TRUE(v1 != v2);
		map.SetLabelName("renamed", kModStart + 0x100);
		const uint32_t v3 = map.Version();
		EXPECT_TRUE(v2 != v3);
		// Reads don't count as changes.
		map.SortSymbols();
		map.GetAllActiveSymbols(ST_FUNCTION);
		EXPECT_EQ_INT((int)map.Version(), (int)v3);
		map.UnloadModule(kModStart, kModSize);
		EXPECT_TRUE(v3 != map.Version());
		map.Clear();
		EXPECT_TRUE(v3 != map.Version());

		// The emulator throws the whole map away and builds a new one on every boot, so a fresh
		// map must never hand out a version a previous one already used - otherwise a cache built
		// from the last game's symbols looks current for the next game.
		SymbolMap map2;
		EXPECT_TRUE(map.Version() != map2.Version());
		map2.AddModule("TEST", kModStart, kModSize);
		map2.AddFunction("func", kModStart + 0x100, 0x40);
		EXPECT_TRUE(map.Version() != map2.Version());
	}

	return true;
}

// DenseHashMap/PrehashMap are open-addressed, linear-probing maps used in hot GPU paths - the
// texture cache, the shader managers, the software renderer's sampler/drawpixel caches. They use
// tombstones for removal, which is where the interesting failure modes live.
static void *HashValue(int i) {
	return (void *)(uintptr_t)(i + 1);  // never null, so GetOrNull can tell "missing" apart
}

bool TestHashmaps() {
	// The basics: insert, find, miss, remove, size.
	{
		DenseHashMap<uint32_t, void *> m(16);
		EXPECT_EQ_INT((int)m.size(), 0);
		EXPECT_TRUE(m.Insert(100, HashValue(1)));
		EXPECT_TRUE(m.Insert(200, HashValue(2)));
		EXPECT_EQ_INT((int)m.size(), 2);

		void *v = nullptr;
		EXPECT_TRUE(m.Get(100, &v));
		EXPECT_TRUE(v == HashValue(1));
		EXPECT_TRUE(m.Get(200, &v));
		EXPECT_TRUE(v == HashValue(2));
		EXPECT_FALSE(m.Get(300, &v));
		EXPECT_TRUE(m.ContainsKey(100));
		EXPECT_FALSE(m.ContainsKey(300));
		EXPECT_TRUE(m.GetOrNull(300) == nullptr);

		EXPECT_TRUE(m.Remove(100));
		EXPECT_EQ_INT((int)m.size(), 1);
		EXPECT_FALSE(m.Get(100, &v));
		// Removing what isn't there says so rather than corrupting the map.
		EXPECT_FALSE(m.Remove(100));
		EXPECT_FALSE(m.Remove(999));
		// The other entry must still be reachable - a tombstone can't cut the probe chain.
		EXPECT_TRUE(m.Get(200, &v));
	}

	// Iterate visits exactly the live entries, and Clear empties it.
	{
		DenseHashMap<uint32_t, void *> m(16);
		for (int i = 0; i < 6; i++)
			EXPECT_TRUE(m.Insert(i, HashValue(i)));
		EXPECT_TRUE(m.Remove(2));
		EXPECT_TRUE(m.Remove(4));

		int seen = 0;
		uint32_t keyMask = 0;
		bool valuesOk = true;
		m.Iterate([&](const uint32_t &key, void *value) {
			seen++;
			keyMask |= 1u << key;
			// The value must still be the one that went in with this key.
			if (value != HashValue((int)key))
				valuesOk = false;
		});
		EXPECT_TRUE(valuesOk);
		EXPECT_EQ_INT(seen, 4);
		EXPECT_EQ_INT((int)keyMask, (int)((1u << 0) | (1u << 1) | (1u << 3) | (1u << 5)));

		m.Clear();
		EXPECT_EQ_INT((int)m.size(), 0);
		void *v = nullptr;
		EXPECT_FALSE(m.Get(0, &v));
		// Still usable after Clear.
		EXPECT_TRUE(m.Insert(0, HashValue(42)));
		EXPECT_TRUE(m.Get(0, &v));
	}

	// Growing past the initial capacity must not lose or corrupt anything.
	{
		DenseHashMap<uint32_t, void *> m(8);
		const int kCount = 500;
		for (int i = 0; i < kCount; i++)
			EXPECT_TRUE(m.Insert(i * 7 + 1, HashValue(i)));
		EXPECT_EQ_INT((int)m.size(), kCount);
		for (int i = 0; i < kCount; i++) {
			void *v = nullptr;
			EXPECT_TRUE(m.Get(i * 7 + 1, &v));
			EXPECT_TRUE(v == HashValue(i));
		}
		// And nothing that was never inserted has appeared.
		for (int i = 0; i < kCount; i++) {
			void *v = nullptr;
			EXPECT_FALSE(m.Get(i * 7 + 2, &v));
		}
	}

	// Rebuild() compacts away tombstones without changing what's in the map.
	{
		DenseHashMap<uint32_t, void *> m(64);
		for (int i = 0; i < 20; i++)
			EXPECT_TRUE(m.Insert(i, HashValue(i)));
		for (int i = 0; i < 20; i += 2)
			EXPECT_TRUE(m.Remove(i));
		m.Rebuild();
		EXPECT_EQ_INT((int)m.size(), 10);
		for (int i = 1; i < 20; i += 2) {
			void *v = nullptr;
			EXPECT_TRUE(m.Get(i, &v));
			EXPECT_TRUE(v == HashValue(i));
		}
		for (int i = 0; i < 20; i += 2) {
			void *v = nullptr;
			EXPECT_FALSE(m.Get(i, &v));
		}
	}

	// Differential test against std::unordered_map. Fixed seed so a failure reproduces.
	{
		DenseHashMap<uint32_t, void *> m(16);
		std::unordered_map<uint32_t, void *> ref;
		uint32_t rng = 24680;
		auto next = [&rng]() { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7FFF; };

		for (int i = 0; i < 20000; i++) {
			const uint32_t key = next() % 500;
			if ((next() % 100) < 55) {
				if (ref.find(key) == ref.end()) {
					void *value = HashValue((int)key);
					EXPECT_TRUE(m.Insert(key, value));
					ref[key] = value;
				}
			} else {
				const bool had = ref.find(key) != ref.end();
				EXPECT_EQ_INT((int)m.Remove(key), (int)had);
				ref.erase(key);
			}
			if ((int)m.size() != (int)ref.size()) {
				printf("Hashmap size diverged at iteration %d: %d vs %d\n", i, (int)m.size(), (int)ref.size());
				return false;
			}
		}

		// Every key the reference has, the map must have - with the same value - and nothing else.
		for (const auto &pair : ref) {
			void *v = nullptr;
			if (!m.Get(pair.first, &v) || v != pair.second) {
				printf("Hashmap lost key %u\n", pair.first);
				return false;
			}
		}
		int seen = 0;
		m.Iterate([&](const uint32_t &key, void *value) {
			seen++;
			if (ref.find(key) == ref.end())
				printf("Hashmap has phantom key %u\n", key);
		});
		EXPECT_EQ_INT(seen, (int)ref.size());
	}

	// Insert/remove churn with fresh keys every round leaves tombstones behind. They take up
	// probe slots exactly like real entries do, so if they aren't counted towards the load factor
	// the table fills up with them - and then a lookup for a missing key never finds a FREE bucket
	// to stop at. The map stays small the whole time, so nothing here should be slow or fail.
	{
		DenseHashMap<uint32_t, void *> m(16);
		for (int round = 0; round < 200; round++) {
			for (int i = 0; i < 4; i++)
				EXPECT_TRUE(m.Insert(round * 4 + i, HashValue(i)));
			for (int i = 0; i < 4; i++)
				EXPECT_TRUE(m.Remove(round * 4 + i));
			EXPECT_EQ_INT((int)m.size(), 0);
			// A miss has to terminate. If tombstones have eaten every bucket, this is where a
			// linear-probing map spins forever.
			void *v = nullptr;
			EXPECT_FALSE(m.Get(0xD1A6, &v));
		}
	}

	// PrehashMap is the same structure keyed directly on a precomputed hash.
	{
		PrehashMap<void *> m(16);
		EXPECT_TRUE(m.Insert(0x1000, HashValue(1)));
		EXPECT_TRUE(m.Insert(0x2000, HashValue(2)));
		// It reports a duplicate rather than asserting, unlike DenseHashMap.
		EXPECT_FALSE(m.Insert(0x1000, HashValue(3)));

		void *v = nullptr;
		EXPECT_TRUE(m.Get(0x1000, &v));
		EXPECT_TRUE(v == HashValue(1));
		EXPECT_FALSE(m.Get(0x3000, &v));
		EXPECT_TRUE(m.Remove(0x1000));
		EXPECT_FALSE(m.Get(0x1000, &v));
		EXPECT_TRUE(m.Get(0x2000, &v));

		// Same tombstone churn as above.
		for (int round = 0; round < 200; round++) {
			for (int i = 0; i < 4; i++)
				EXPECT_TRUE(m.Insert(0x10000 + round * 4 + i, HashValue(i)));
			for (int i = 0; i < 4; i++)
				EXPECT_TRUE(m.Remove(0x10000 + round * 4 + i));
			EXPECT_FALSE(m.Get(0xD1A6, &v));
		}
	}

	return true;
}

bool TestTinySet() {
	TinySet<int, 4> a;
	EXPECT_EQ_INT((int)a.size(), 0);
	a.push_back(1);
	EXPECT_EQ_INT((int)a.size(), 1);
	a.push_back(2);
	EXPECT_EQ_INT((int)a.size(), 2);
	TinySet<int, 4> b;
	b.push_back(8);
	b.push_back(9);
	b.push_back(10);
	EXPECT_EQ_INT((int)b.size(), 3);

	a.append(b);
	EXPECT_EQ_INT((int)a.size(), 5);
	EXPECT_EQ_INT((int)b.size(), 3);

	b.append(b);
	EXPECT_EQ_INT((int)b.size(), 6);

	EXPECT_EQ_INT(a[0], 1);
	EXPECT_EQ_INT(a[1], 2);
	EXPECT_EQ_INT(a[2], 8);
	EXPECT_EQ_INT(a[3], 9);
	EXPECT_EQ_INT(a[4], 10);
	a.append(a);
	EXPECT_EQ_INT(a.size(), 10);
	EXPECT_EQ_INT(a[9], 10);

	b.push_back(11);
	EXPECT_EQ_INT((int)b.size(), 7);
	b.push_back(12);
	EXPECT_EQ_INT((int)b.size(), 8);
	b.push_back(13);
	EXPECT_EQ_INT(b.size(), 9);
	return true;
}

bool TestFastVec() {
	FastVec<int> a;
	EXPECT_EQ_INT((int)a.size(), 0);
	a.push_back(1);
	EXPECT_EQ_INT((int)a.size(), 1);
	a.push_back(2);
	EXPECT_EQ_INT((int)a.size(), 2);
	FastVec<int> b;
	b.push_back(8);
	b.push_back(9);
	b.push_back(10);
	EXPECT_EQ_INT((int)b.size(), 3);
	for (int i = 0; i < 100; i++) {
		b.push_back(33);
	}
	EXPECT_EQ_INT((int)b.size(), 103);

	int items[4] = { 50, 60, 70, 80 };
	b.insert(b.begin() + 1, items, items + 4);
	EXPECT_EQ_INT(b[0], 8);
	EXPECT_EQ_INT(b[1], 50);
	EXPECT_EQ_INT(b[2], 60);
	EXPECT_EQ_INT(b[3], 70);
	EXPECT_EQ_INT(b[4], 80);
	EXPECT_EQ_INT(b[5], 9);

	b.resize(2);
	b.insert(b.end(), items, items + 4);
	EXPECT_EQ_INT(b[0], 8);
	EXPECT_EQ_INT(b[1], 50);
	EXPECT_EQ_INT(b[2], 50);
	EXPECT_EQ_INT(b[3], 60);
	EXPECT_EQ_INT(b[4], 70);
	EXPECT_EQ_INT(b[5], 80);


	return true;
}

bool TestVFPUSinCos() {
	float sine, cosine;
	// Needed for VFPU tables.
	// There might be a better place to invoke it, but whatever.
	g_VFS.Register("", new DirectoryReader(Path("assets")));
	InitVFPU();
	vfpu_sincos(0.0f, sine, cosine);
	EXPECT_EQ_FLOAT(sine, 0.0f);
	EXPECT_EQ_FLOAT(cosine, 1.0f);
	vfpu_sincos(1.0f, sine, cosine);
	EXPECT_APPROX_EQ_FLOAT(sine, 1.0f);
	EXPECT_APPROX_EQ_FLOAT(cosine, 0.0f);
	vfpu_sincos(2.0f, sine, cosine);
	EXPECT_APPROX_EQ_FLOAT(sine, 0.0f);
	EXPECT_APPROX_EQ_FLOAT(cosine, -1.0f);
	vfpu_sincos(3.0f, sine, cosine);
	EXPECT_APPROX_EQ_FLOAT(sine, -1.0f);
	EXPECT_APPROX_EQ_FLOAT(cosine, 0.0f);
	vfpu_sincos(4.0f, sine, cosine);
	EXPECT_EQ_FLOAT(sine, 0.0f);
	EXPECT_EQ_FLOAT(cosine, 1.0f);
	vfpu_sincos(5.0f, sine, cosine);
	EXPECT_APPROX_EQ_FLOAT(sine, 1.0f);
	EXPECT_APPROX_EQ_FLOAT(cosine, 0.0f);

	vfpu_sincos(-1.0f, sine, cosine);
	EXPECT_EQ_FLOAT(sine, -1.0f);
	EXPECT_EQ_FLOAT(cosine, 0.0f);
	vfpu_sincos(-2.0f, sine, cosine);
	EXPECT_EQ_FLOAT(sine, 0.0f);
	EXPECT_EQ_FLOAT(cosine, -1.0f);

	for (float angle = -10.0f; angle < 10.0f; angle += 0.1f) {
		vfpu_sincos(angle, sine, cosine);
		EXPECT_APPROX_EQ_FLOAT(sine, sinf(angle * M_PI_2));
		EXPECT_APPROX_EQ_FLOAT(cosine, cosf(angle * M_PI_2));

		if (g_testLog) {
			printf("sine: %f==%f cosine: %f==%f\n", sine, sinf(angle * M_PI_2), cosine, cosf(angle * M_PI_2));
		}
	}
	return true;
}

bool TestVFPUMatrixTranspose() {
	MatrixSize sz = M_4x4;
	int matrix = 0;  // M000
	u8 cols[4];
	u8 rows[4];

	GetMatrixColumns(matrix, sz, cols);
	GetMatrixRows(matrix, sz, rows);

	int transposed = Xpose(matrix);
	u8 x_cols[4];
	u8 x_rows[4];

	GetMatrixColumns(transposed, sz, x_cols);
	GetMatrixRows(transposed, sz, x_rows);

	for (int i = 0; i < GetMatrixSide(sz); i++) {
		EXPECT_EQ_INT(cols[i], x_rows[i]);
		EXPECT_EQ_INT(x_cols[i], rows[i]);
	}
	return true;
}

// TODO: Hook this up again!
void TestGetMatrix(int matrix, MatrixSize sz) {
	INFO_LOG(Log::System, "Testing matrix %s", GetMatrixNotation(matrix, sz).c_str());
	u8 fullMatrix[16];

	u8 cols[4];
	u8 rows[4];

	GetMatrixColumns(matrix, sz, cols);
	GetMatrixRows(matrix, sz, rows);

	GetMatrixRegs(fullMatrix, sz, matrix);

	int n = GetMatrixSide(sz);
	VectorSize vsz = GetVectorSize(sz);
	for (int i = 0; i < n; i++) {
		// int colName = GetColumnName(matrix, sz, i, 0);
		// int rowName = GetRowName(matrix, sz, i, 0);
		int colName = cols[i];
		int rowName = rows[i];
		INFO_LOG(Log::System, "Column %i: %s", i, GetVectorNotation(colName, vsz).c_str());
		INFO_LOG(Log::System, "Row %i: %s", i, GetVectorNotation(rowName, vsz).c_str());

		u8 colRegs[4];
		u8 rowRegs[4];
		GetVectorRegs(colRegs, vsz, colName);
		GetVectorRegs(rowRegs, vsz, rowName);

		// Check that the individual regs are the expected ones.
		std::stringstream a, b, c, d;
		for (int j = 0; j < n; j++) {
			a.clear();
			b.clear();
			a << (int)fullMatrix[i * 4 + j] << " ";
			b << (int)colRegs[j] << " ";

			c.clear();
			d.clear();

			c << (int)fullMatrix[j * 4 + i] << " ";
			d << (int)rowRegs[j] << " ";
		}
		INFO_LOG(Log::System, "Col: %s vs %s", a.str().c_str(), b.str().c_str());
		if (a.str() != b.str())
			INFO_LOG(Log::System, "WRONG!");
		INFO_LOG(Log::System, "Row: %s vs %s", c.str().c_str(), d.str().c_str());
		if (c.str() != d.str())
			INFO_LOG(Log::System, "WRONG!");
	}
}

bool TestParseLBN() {
	const char *validStrings[] = {
		"/sce_lbn0x5fa0_size0x1428",
		"/sce_lbn7050_sizeee850",
		"/sce_lbn0x5eeeh_size0x234x",  // Check for trailing chars. See #7960.
		"/sce_lbneee__size434.",  // Check for trailing chars. See #7960.
	};
	int expectedResults[][2] = {
		{0x5fa0, 0x1428},
		{0x7050, 0xee850},
		{0x5eee, 0x234},
		{0xeee,  0x434},
	};
	const char *invalidStrings[] = {
		"/sce_lbn0x5fa0_sze0x1428",
		"",
		"//",
	};
	for (int i = 0; i < ARRAY_SIZE(validStrings); i++) {
		u32 startSector = 0, readSize = 0;
		// printf("testing %s\n", validStrings[i]);
		EXPECT_TRUE(parseLBN(validStrings[i], &startSector, &readSize));
		EXPECT_EQ_INT(startSector, expectedResults[i][0]);
		EXPECT_EQ_INT(readSize, expectedResults[i][1]);
	}
	for (int i = 0; i < ARRAY_SIZE(invalidStrings); i++) {
		u32 startSector, readSize;
		EXPECT_FALSE(parseLBN(invalidStrings[i], &startSector, &readSize));
	}
	return true;
}

// So we can use EXPECT_TRUE, etc.
struct AlignedMem {
	AlignedMem(size_t sz, size_t alignment = 16) {
		p_ = AllocateAlignedMemory(sz, alignment);
	}
	~AlignedMem() {
		FreeAlignedMemory(p_);
	}

	operator void *() {
		return p_;
	}

	operator char *() {
		return (char *)p_;
	}

private:
	void *p_;
};

bool TestQuickTexHash() {
	static const int BUF_SIZE = 1024;
	AlignedMem buf(BUF_SIZE, 16);

	memset(buf, 0, BUF_SIZE);
	EXPECT_EQ_HEX(StableQuickTexHash(buf, BUF_SIZE), 0xaa756edc);

	memset(buf, 1, BUF_SIZE);
	EXPECT_EQ_HEX(StableQuickTexHash(buf, BUF_SIZE), 0x66f81b1c);

	strncpy(buf, "hello", BUF_SIZE);
	EXPECT_EQ_HEX(StableQuickTexHash(buf, BUF_SIZE), 0xf6028131);

	strncpy(buf, "goodbye", BUF_SIZE);
	EXPECT_EQ_HEX(StableQuickTexHash(buf, BUF_SIZE), 0xef81b54f);

	// Simple patterns.
	for (int i = 0; i < BUF_SIZE; ++i) {
		char *p = buf;
		p[i] = i & 0xFF;
	}
	EXPECT_EQ_HEX(StableQuickTexHash(buf, BUF_SIZE), 0x0d64531c);

	int j = 573;
	for (int i = 0; i < BUF_SIZE; ++i) {
		char *p = buf;
		j += ((i * 7) + (i & 3)) * 11;
		p[i] = j & 0xFF;
	}
	EXPECT_EQ_HEX(StableQuickTexHash(buf, BUF_SIZE), 0x58de8dbc);

	return true;
}

bool TestCLZ() {
	static const uint32_t input[] = {
		0xFFFFFFFF,
		0x00FFFFF0,
		0x00101000,
		0x00003000,
		0x00000001,
		0x00000000,
	};
	static const uint32_t expected[] = {
		0,
		8,
		11,
		18,
		31,
		32,
	};
	for (int i = 0; i < ARRAY_SIZE(input); i++) {
		EXPECT_EQ_INT(clz32(input[i]), expected[i]);
	}
	return true;
}

static bool TestMemMap() {
	Memory::g_MemorySize = Memory::RAM_DOUBLE_SIZE;

	enum class Flags {
		NO_KERNEL = 0,
		ALLOW_KERNEL = 1,
	};
	struct Range {
		uint32_t base;
		uint32_t size;
		Flags flags;
	};
	static const Range ranges[] = {
		{ 0x08000000, Memory::RAM_DOUBLE_SIZE, Flags::ALLOW_KERNEL },
		{ 0x00010000, Memory::SCRATCHPAD_SIZE, Flags::NO_KERNEL },
		{ 0x04000000, 0x00800000, Flags::NO_KERNEL },  // VRAM (although we don't take wrapping into account here...)
	};
	static const uint32_t extraBits[] = {
		0x00000000,
		0x40000000,
		0x80000000,
	};

	for (const auto &range : ranges) {
		size_t testBits = range.flags == Flags::ALLOW_KERNEL ? 3 : 2;
		for (size_t i = 0; i < testBits; ++i) {
			uint32_t base = range.base | extraBits[i];

			EXPECT_TRUE(Memory::IsValidAddress(base));
			EXPECT_TRUE(Memory::IsValidAddress(base + range.size - 1));
			EXPECT_FALSE(Memory::IsValidAddress(base + range.size));
			EXPECT_FALSE(Memory::IsValidAddress(base - 1));

			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, range.size), range.size);
			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, range.size + 1), range.size);
			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, range.size - 1), range.size - 1);
			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, 0), 0);
			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, 0x80000001), range.size);
			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, 0x40000001), range.size);
			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, 0x20000001), range.size);
			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base, 0x10000001), range.size);

			EXPECT_EQ_HEX(Memory::ClampValidSizeAt(base + range.size - 0x10, 0x20000001), 0x10);
		}
	}

	EXPECT_FALSE(Memory::IsValidAddress(0x00015000));
	EXPECT_FALSE(Memory::IsValidAddress(0x04900000));
	EXPECT_EQ_HEX(Memory::ClampValidSizeAt(0x00015000, 4), 0);
	EXPECT_EQ_HEX(Memory::ClampValidSizeAt(0x04900000, 4), 0);

	// Test the regular kernel check
	EXPECT_TRUE(Memory::IsKernelAddress(0x08002000));
	EXPECT_TRUE(Memory::IsKernelAddress(0x08000000));  // to avoid our patches at the start of kernel ram
	EXPECT_TRUE(Memory::IsKernelAddress(0x08300000));  // to avoid our patches at the start of kernel ram
	EXPECT_FALSE(Memory::IsKernelAddress(0x08800000));  // to avoid our patches at the start of kernel ram

	// Test the code kernel space hack.
	EXPECT_TRUE(Memory::IsKernelCodeAddress(0x08002000));
	EXPECT_FALSE(Memory::IsKernelCodeAddress(0x08000000));  // to avoid our patches at the start of kernel ram
	EXPECT_TRUE(Memory::IsKernelCodeAddress(0x08300000));  // to avoid our patches at the start of kernel ram
	EXPECT_FALSE(Memory::IsKernelCodeAddress(0x08800000));  // to avoid our patches at the start of kernel ram

	return true;
}

static bool TestPath() {
	// Also test the Path class while we're at it.
	Path path("/asdf/jkl/");
	EXPECT_EQ_STR(path.ToString(), std::string("/asdf/jkl"));

	Path path2("/asdf/jkl");
	EXPECT_EQ_STR(path2.NavigateUp().ToString(), std::string("/asdf"));

	Path path3 = path2 / "foo/bar";
	EXPECT_EQ_STR(path3.WithExtraExtension(".txt").ToString(), std::string("/asdf/jkl/foo/bar.txt"));

	EXPECT_EQ_STR(Path("foo.bar/hello").GetFileExtension(), std::string());
	EXPECT_EQ_STR(Path("foo.bar/hello.txt").WithReplacedExtension(".txt", ".html").ToString(), std::string("foo.bar/hello.html"));

	EXPECT_EQ_STR(Path("C:\\Yo").NavigateUp().ToString(), std::string("C:"));
#if PPSSPP_PLATFORM(WINDOWS)
	EXPECT_EQ_STR(Path("C:").NavigateUp().ToString(), std::string("/"));

	EXPECT_EQ_STR(Path("C:\\Yo").GetDirectory(), std::string("C:"));
	EXPECT_EQ_STR(Path("C:\\Yo").GetFilename(), std::string("Yo"));
	EXPECT_EQ_STR(Path("C:\\Yo\\Lo").GetDirectory(), std::string("C:/Yo"));
	EXPECT_EQ_STR(Path("C:\\Yo\\Lo").GetFilename(), std::string("Lo"));

	EXPECT_EQ_STR(Path(R"(\\host\share\filename)").GetRootVolume().ToString(), std::string("//host"));
	EXPECT_EQ_STR(Path(R"(\\?\UNC\share\filename)").GetRootVolume().ToString(), std::string("//?/UNC"));
	EXPECT_EQ_STR(Path(R"(\\?\C:\share\filename)").GetRootVolume().ToString(), std::string("//?/C:"));
#endif

	std::string computedPath;

	EXPECT_TRUE(Path("/a/b").ComputePathTo(Path("/a/b/c/d/e"), computedPath));

	EXPECT_EQ_STR(computedPath, std::string("c/d/e"));

	EXPECT_TRUE(Path("/").ComputePathTo(Path("/home/foo/bar"), computedPath));
	EXPECT_EQ_STR(computedPath, std::string("home/foo/bar"));

	EXPECT_TRUE(Path("/a/b").ComputePathTo(Path("/a/b"), computedPath));
	EXPECT_EQ_STR(computedPath, std::string());

	return true;
}

static bool TestAndroidContentURI() {
	static const char *treeURIString = "content://com.android.externalstorage.documents/tree/primary%3APSP%20ISO";
	static const char *directoryURIString = "content://com.android.externalstorage.documents/tree/primary%3APSP%20ISO/document/primary%3APSP%20ISO";
	static const char *fileTreeURIString = "content://com.android.externalstorage.documents/tree/primary%3APSP%20ISO/document/primary%3APSP%20ISO%2FTekken%206.iso";
	static const char *fileNonTreeString = "content://com.android.externalstorage.documents/document/primary%3APSP%2Fcrash_bad_execaddr.prx";
	static const char *downloadURIString = "content://com.android.providers.downloads.documents/document/msf%3A10000000006";

	AndroidContentURI treeURI;
	EXPECT_TRUE(treeURI.Parse(treeURIString));
	AndroidContentURI dirURI;
	EXPECT_TRUE(dirURI.Parse(directoryURIString));
	AndroidContentURI fileTreeURI;
	EXPECT_TRUE(fileTreeURI.Parse(fileTreeURIString));
	AndroidContentURI fileTreeURICopy;
	EXPECT_TRUE(fileTreeURICopy.Parse(fileTreeURIString));
	AndroidContentURI fileURI;
	EXPECT_TRUE(fileURI.Parse(fileNonTreeString));

	EXPECT_EQ_STR(fileTreeURI.GetLastPart(), std::string("Tekken 6.iso"));

	EXPECT_TRUE(treeURI.TreeContains(fileTreeURI));

	EXPECT_TRUE(fileTreeURI.CanNavigateUp());
	fileTreeURI.NavigateUp();
	EXPECT_FALSE(fileTreeURI.CanNavigateUp());

	EXPECT_EQ_STR(fileTreeURI.FilePath(), fileTreeURI.RootPath());

	EXPECT_EQ_STR(fileTreeURI.ToString(), std::string(directoryURIString));

	std::string diff;
	EXPECT_TRUE(dirURI.ComputePathTo(fileTreeURICopy, diff));
	EXPECT_EQ_STR(diff, std::string("Tekken 6.iso"));

	EXPECT_EQ_STR(fileURI.GetFileExtension(), std::string(".prx"));
	EXPECT_TRUE(fileURI.CanNavigateUp());  // Can now virtually navigate up one step from these.

	// These are annoying because they hide the actual filename, and we can't get at a parent folder.
	// Decided to handle the ':' as a directory separator for navigation purposes, which fixes the problem (though not the extension thing).
	AndroidContentURI downloadURI;
	EXPECT_TRUE(downloadURI.Parse(std::string(downloadURIString)));
	EXPECT_EQ_STR(downloadURI.GetLastPart(), std::string("10000000006"));
	EXPECT_TRUE(downloadURI.CanNavigateUp());
	EXPECT_TRUE(downloadURI.NavigateUp());
	// While this is not an openable valid content URI, we can still get something that we can concatenate a filename on top of.
	EXPECT_EQ_STR(downloadURI.ToString(), std::string("content://com.android.providers.downloads.documents/document/msf%3A"));
	EXPECT_EQ_STR(downloadURI.GetLastPart(), std::string("msf:"));
	downloadURI = downloadURI.WithComponent("myfile");
	EXPECT_EQ_STR(downloadURI.ToString(), std::string("content://com.android.providers.downloads.documents/document/msf%3Amyfile"));
	return true;
}

class UnitTestWordWrapper : public WordWrapper {
public:
	UnitTestWordWrapper(std::string_view str, float maxW, int flags)
		: WordWrapper(str, maxW, flags) {
	}

protected:
	float MeasureWidth(std::string_view str) override {
		// Simple case for unit testing.
		int w = 0;
		for (UTF8 utf(str); !utf.end(); ) {
			uint32_t c = utf.next();
			switch (c) {
			case ' ':
			case '.':
				w += 1;
				break;
			case 0x00AD:
				// No width for soft hyphens.
				break;
			default:
				w += 2;
				break;
			}
		}

		return w;
	}
};

#define EXPECT_WORDWRAP_EQ_STR(a, l, f, b) if (UnitTestWordWrapper(a, l, f).Wrapped() != b) { printf("%s: Test Fail (%d, %s)\n%s\nvs\n%s\n", __FUNCTION__, l, #f, UnitTestWordWrapper(a, l, f).Wrapped().c_str(), std::string(b).c_str()); return false; }

static bool TestWrapText() {
	// If there's enough space, it shouldn't wrap.  This is exactly enough.
	EXPECT_WORDWRAP_EQ_STR("Hello", 10, 0, "Hello");
	EXPECT_WORDWRAP_EQ_STR("Hello", 10, FLAG_WRAP_TEXT, "Hello");
	EXPECT_WORDWRAP_EQ_STR("Hello", 10, FLAG_ELLIPSIZE_TEXT, "Hello");
	EXPECT_WORDWRAP_EQ_STR("Hello", 10, FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT, "Hello");

	// Try a single word that doesn't fit in the space.
	EXPECT_WORDWRAP_EQ_STR("Hello", 6, 0, "Hello");
	EXPECT_WORDWRAP_EQ_STR("Hello", 6, FLAG_WRAP_TEXT, "Hel\nlo");
	EXPECT_WORDWRAP_EQ_STR("Hello", 6, FLAG_ELLIPSIZE_TEXT, "H...");
	EXPECT_WORDWRAP_EQ_STR("Hello", 6, FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT, "H...");

	// Now, multiple words.
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 14, 0, "Hello goodbye");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 14, FLAG_WRAP_TEXT, "Hello \ngoodbye");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 14, FLAG_ELLIPSIZE_TEXT, "Hello...");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 14, FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT, "Hello \ngoodbye");

	// Multiple words with something short after...
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye yes", 14, 0, "Hello goodbye ");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye yes", 14, FLAG_WRAP_TEXT, "Hello \ngoodbye \nyes");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye yes", 14, FLAG_ELLIPSIZE_TEXT, "Hello...");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye yes", 14, FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT, "Hello \ngoodbye \nyes");

	// Now, multiple words, but only the first fits.
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 10, 0, "Hello ");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 10, FLAG_WRAP_TEXT, "Hello \ngoodb\nye");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 10, FLAG_ELLIPSIZE_TEXT, "Hel...");
	EXPECT_WORDWRAP_EQ_STR("Hello goodbye", 10, FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT, "Hello \ngoo...");

	// How about the shy character?
	const std::string shyTestString = StringFromFormat("Very%c%clong", 0xC2, 0xAD);
	EXPECT_WORDWRAP_EQ_STR(shyTestString.c_str(), 10, 0, shyTestString);
	EXPECT_WORDWRAP_EQ_STR(shyTestString.c_str(), 10, FLAG_WRAP_TEXT, "Very-\nlong");
	EXPECT_WORDWRAP_EQ_STR(shyTestString.c_str(), 10, FLAG_ELLIPSIZE_TEXT, "Very...");
	EXPECT_WORDWRAP_EQ_STR(shyTestString.c_str(), 10, FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT, "Very-\nlong");

	// Newlines should not be removed and should influence wrapping.
	EXPECT_WORDWRAP_EQ_STR("Hello\ngoodbye yes\nno", 14, 0, "Hello\ngoodbye ");
	EXPECT_WORDWRAP_EQ_STR("Hello\ngoodbye yes\nno", 14, FLAG_WRAP_TEXT, "Hello\ngoodbye \nyes\nno");
	EXPECT_WORDWRAP_EQ_STR("Hello\ngoodbye yes\nno", 14, FLAG_ELLIPSIZE_TEXT, "Hello\ngoodb...\nno");
	EXPECT_WORDWRAP_EQ_STR("Hello\ngoodbye yes\nno", 14, FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT, "Hello\ngoodbye \nyes\nno");

	return true;
}

static bool TestSmallDataConvert() {
	float f[4] = { 1.0f / 255.0f, 2.0f / 255.0f, 3.0f / 255.0f, 4.0f / 255.f };
	uint32_t result = Float4ToUint8x4_NoClamp(f);
	EXPECT_EQ_HEX(result, 0x04030201);
	result = Float4ToUint8x4(f);
	EXPECT_EQ_HEX(result, 0x04030201);
	return true;
}

bool TestInputMapping() {
	InputMapping mapping;
	mapping.deviceId = DEVICE_ID_PAD_0;
	mapping.keyCode = 20;
	InputMapping mapping2;
	mapping2.deviceId = DEVICE_ID_PAD_8;
	mapping2.keyCode = 38;
	std::string cfg = mapping.ToConfigString();

	InputMapping parsedMapping = InputMapping::FromConfigString(cfg);
	EXPECT_EQ_INT(parsedMapping.deviceId, mapping.deviceId);
	EXPECT_EQ_INT(parsedMapping.keyCode, mapping.keyCode);

	using KeyMap::MultiInputMapping;
	MultiInputMapping multi(mapping);

	EXPECT_EQ_STR(multi.ToConfigString(), mapping.ToConfigString());

	multi.mappings.push_back(mapping2);
	EXPECT_FALSE(multi.EqualsSingleMapping(mapping));
	EXPECT_TRUE(multi.mappings.contains(mapping2));
	EXPECT_TRUE(multi.mappings.contains(mapping));

	std::string cfgMulti = multi.ToConfigString();

	EXPECT_EQ_STR(cfgMulti, std::string("10-20:18-38"));

	MultiInputMapping parsedMulti = MultiInputMapping::FromConfigString(cfgMulti);

	EXPECT_EQ_INT((int)parsedMulti.mappings.size(), 2);

	// OK, both single and multiple mappings parse. Let's now see if the old parsing can handle a multimapping.
	// This is a requirement for the new format.

	InputMapping parsedMultiSingle = InputMapping::FromConfigString(cfgMulti);  // yes this is an intentional mismatch
	// We should get the first mapping.
	EXPECT_TRUE(parsedMultiSingle == mapping);
	return true;
}

bool TestEscapeMenuString() {
	char c;
	std::string temp = UnescapeMenuString("&File", &c);
	EXPECT_EQ_INT((int)c, (int)'F');
	EXPECT_EQ_STR(temp, std::string("File"));
	temp = UnescapeMenuString("U&til", &c);
	EXPECT_EQ_INT((int)c, (int)'t');
	EXPECT_EQ_STR(temp, std::string("Util"));
	temp = UnescapeMenuString("Ed&it", nullptr);
	EXPECT_EQ_STR(temp, std::string("Edit"));
	temp = UnescapeMenuString("Cut && Paste", nullptr);
	EXPECT_EQ_STR(temp, std::string("Cut & Paste"));
	temp = UnescapeMenuString("&A&B", &c);
	EXPECT_EQ_STR(temp, std::string("AB"));
	EXPECT_EQ_INT((int)c, (int)'A');
	return true;
}

bool TestSubstitutions() {
	std::string output = ApplySafeSubstitutions("%3 %2 %1", "a", "b", "c");
	EXPECT_EQ_STR(output, std::string("c b a"));
	return true;
}

bool TestIniFile() {
	const std::string testLine = "adsf\\#asdf = jkl\\# # comment";
	const std::string testLine2 = "# Just a comment";

	std::string temp;
	ParsedIniLine line(testLine);
	line.Reconstruct(&temp);
	EXPECT_EQ_STR(testLine, temp);

	temp.clear();
	ParsedIniLine line2(testLine2);
	line2.Reconstruct(&temp);

	EXPECT_EQ_STR(testLine2, temp);
	return true;
}

inline u32 ReferenceRGBA5551ToRGBA8888(u16 src) {
	u8 r = Convert5To8((src >> 0) & 0x1F);
	u8 g = Convert5To8((src >> 5) & 0x1F);
	u8 b = Convert5To8((src >> 10) & 0x1F);
	u8 a = (src >> 15) & 0x1;
	a = (a) ? 0xff : 0;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

inline u32 ReferenceRGB565ToRGBA8888(u16 src) {
	u8 r = Convert5To8((src >> 0) & 0x1F);
	u8 g = Convert6To8((src >> 5) & 0x3F);
	u8 b = Convert5To8((src >> 11) & 0x1F);
	u8 a = 0xFF;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

bool TestColorConv() {
	// Can exhaustively test the 16->32 conversions.
	for (int i = 0; i < 65536; i++) {
		u16 col16 = i;

		u32 reference = ReferenceRGBA5551ToRGBA8888(col16);
		u32 value = RGBA5551ToRGBA8888(col16);
		EXPECT_EQ_INT(reference, value);

		reference = ReferenceRGB565ToRGBA8888(col16);
		value = RGB565ToRGBA8888(col16);
		EXPECT_EQ_INT(reference, value);
	}

	return true;
}

CharQueue GetQueue() {
	CharQueue queue(5);
	return queue;
}

bool TestCharQueue() {
	// We use a tiny block size for testing.
	CharQueue queue = GetQueue();

	// Add 16 chars.
	queue.push_back("abcdefghijkl");
	queue.push_back("mnop");

	std::string testStr;
	queue.iterate_blocks([&](const char *buf, size_t sz) {
		testStr.append(buf, sz);
		return true;
	});
	EXPECT_EQ_STR(testStr, std::string("abcdefghijklmnop"));

	EXPECT_EQ_CHAR(queue.peek(11), 'l');
	EXPECT_EQ_CHAR(queue.peek(12), 'm');
	EXPECT_EQ_CHAR(queue.peek(15), 'p');
	EXPECT_EQ_INT(queue.block_count(), 3);  // Didn't fit in the first block, so the two pushes above should have each created one additional block.
	EXPECT_EQ_INT(queue.size(), 16);
	char dest[15];
	EXPECT_EQ_INT(queue.pop_front_bulk(dest, 4), 4);
	EXPECT_EQ_INT(queue.size(), 12);
	EXPECT_EQ_MEM(dest, "abcd", 4);
	EXPECT_EQ_INT(queue.pop_front_bulk(dest, 6), 6);
	EXPECT_EQ_INT(queue.size(), 6);
	EXPECT_EQ_MEM(dest, "efghij", 6);
	queue.push_back("qr");
	EXPECT_EQ_INT(queue.pop_front_bulk(dest, 4), 4);  // should pop off klmn
	EXPECT_EQ_MEM(dest, "klmn", 4);
	EXPECT_EQ_INT(queue.size(), 4);
	EXPECT_EQ_CHAR(queue.peek(3), 'r');
	queue.pop_front_bulk(dest, 4);
	EXPECT_EQ_MEM(dest, "opqr", 4);
	EXPECT_TRUE(queue.empty());
	queue.push_back("asdf");
	EXPECT_EQ_INT(queue.next_crlf_offset(), -1);
	queue.push_back("\r\r\n");
	EXPECT_EQ_INT(queue.next_crlf_offset(), 5);
	return true;
}

bool TestBuffer() {
	Buffer b = Buffer::Void();
	b.Append("hello");
	b.Append("world");
	std::string temp;
	b.Take(10, &temp);
	EXPECT_EQ_STR(temp, std::string("helloworld"));
	return true;
}

#if PPSSPP_ARCH(SSE2) && (defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER))
[[gnu::target("sse4.1")]]
#endif
bool TestSIMD() {
#if PPSSPP_ARCH(SSE2)
	__m128i x = _mm_set_epi16(0, 0x4444, 0, 0x3333, 0, 0x2222, 0, 0x1111);
	__m128i y = _mm_packu_epi32_SSE2(x);

	uint64_t testdata[2];
	_mm_store_si128((__m128i *)testdata, y);
	EXPECT_EQ_INT(testdata[0], 0x4444333322221111);
	EXPECT_EQ_INT(testdata[1], 0);

	__m128i a = _mm_set_epi16(0, 0x4444, 0, 0x3333, 0, 0x2222, 0, 0x1111);
	__m128i b = _mm_set_epi16(0, (int16_t)0x8888, 0, 0x7777, 0, 0x6666, 0, 0x5555);
	__m128i c = _mm_packu2_epi32_SSE2(a, b);
	__m128i d = _mm_packu1_epi32_SSE2(b);

	uint64_t testdata2[4];
	_mm_store_si128((__m128i *)testdata2, c);
	_mm_store_si128((__m128i *)testdata2 + 1, d);
	EXPECT_EQ_INT(testdata2[0], 0x4444333322221111);
	EXPECT_EQ_INT(testdata2[1], 0x8888777766665555);
	EXPECT_EQ_INT(testdata2[2], 0x8888777766665555);
	EXPECT_EQ_INT(testdata2[2], 0x8888777766665555);
#endif

	const int testval[2][4] = {
		{ 0x1000, 0x2000, 0x3000, 0x7000 },
		{ -0x1000, -0x2000, -0x3000, -0x7000 }
	};

	for (int i = 0; i < 2; i++) {
		Vec4S32 s = Vec4S32::Load(testval[i]);
		Vec4S32 square = s * s;
		Vec4S32 square16 = s.Mul16(s);
		EXPECT_EQ_INT(square[0], square16[0]);
		EXPECT_EQ_INT(square[1], square16[1]);
		EXPECT_EQ_INT(square[2], square16[2]);
		EXPECT_EQ_INT(square[3], square16[3]);
	}
	return true;
}

static void PrintFloats(const float *f, int count) {
	for (int i = 0; i < count; i++) {
		printf("%.1ff, ", f[i]);
	}
	printf("\n");
}

static bool CompareFloats(const float *values, const float *known_good, int count, int line) {
	int wrongCount = 0;

	for (int i = 0; i < count; i++) {
		if (values[i] != known_good[i]) {
			wrongCount++;
		}
	}

	if (wrongCount > 0) {
		for (int i = 0; i < count; i++) {
			bool wrong = values[i] != known_good[i];
			printf("%d: %0.3f vs %0.3f %s\n", i + 1, values[i], known_good[i], wrong ? "!! MISMATCH" : "");
		}
		printf("At UnitTest.cpp:%d: %d / %d were wrong\n", line, wrongCount, count);
		return false;
	} else {
		return true;
	}
}

bool TestCrossSIMD() {
	static const float a_values[16] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };
	static const float b_values[16] = { -12.0f, 3.0f, -2.5f, 5.0f, 31.0f, 0.5f, 4.0f, 6.0f, 7.0f, 13.0f, 12.0f, 51.0f, 81.0f, 32.0f };
	static const float known_result[16] = { 395.0f, 171.0f, 41.5f, 170.0f, 942.0f, 410.5f, 111.5f, 475.0f, 1358.0f, 607.5f, 163.0f, 728.0f, 297.0f, 49.5f, 25.0f, 160.0f, };
	float result[16];
	Mat4F32 a(a_values);
	Mat4F32 b(b_values);

	Mul4x4By4x4(a, b).Store(result);
	if (!CompareFloats(result, known_result, 16, __LINE__)) {
		return false;
	}

	Mat4x3F32 d = Mat4x3F32(b_values + 2);
	Mul4x3By4x4(d, a).Store(result);

	static const float known_4x3_result[16] = { 332.5f, 371.0f, 404.5f, 438.0f, 80.5f, 95.0f, 105.5f, 116.0f, 192.0f, 237.0f, 269.0f, 301.0f, 790.0f, 1036.0f, 1185.0f, 1349.0f, };
	if (!CompareFloats(result, known_4x3_result, 16, __LINE__)) {
		return false;
	}

	static const float vec_values[4] = { 3.0f, 5.0f, 7.0f, 10000000.0f };
	Vec4F32 v = Vec4F32::Load(vec_values);

	v.AsVec3ByMatrix44(b).Store3(result);

	static const float known_vec_result[3] = { 249.0f, 134.5f, 96.5f, };
	if (!CompareFloats(result, known_vec_result, ARRAY_SIZE(known_vec_result), __LINE__)) {
		return false;
	}
	Vec4F32 scale = Vec4F32::Load(a_values);
	Vec4F32 translate = Vec4F32::Load(b_values);

	TranslateAndScaleInplace(a, scale, translate);
	a.Store(result);

	static const float known_scale_result[16] = { -47.0f, 16.0f, -1.0f, 36.0f, -103.0f, 41.0f, 1.5f, 81.0f, -146.0f, 61.0f, 3.5f, 117.0f, 14.0f, 30.0f, 0.0f, 0.0f,};
	if (!CompareFloats(result, known_scale_result, ARRAY_SIZE(known_scale_result), __LINE__)) {
		return false;
	}

	s8 values[4] = {-1, -128, 127, 45};
	float fvalues[4];
	Vec4F32::LoadS8Norm(values).Store(fvalues);
	static const float known_s8norm_result[4] = {(float)values[0]/128.0f, (float)values[1]/128.0f, (float)values[2]/128.0f, (float)values[3]/128.0f,};
	if (!CompareFloats(fvalues, known_s8norm_result, ARRAY_SIZE(known_s8norm_result), __LINE__)) {
		return false;
	}

	// PrintFloats(result, 16);

	return true;
}

bool TestVolumeFunc() {
	for (int i = 0; i <= 20; i++) {
		float mul = Volume10ToMultiplier(i);

		int vol100 = MultiplierToVolume100(mul);
		float mul2 = Volume100ToMultiplier(vol100);

		bool smaller = (fabsf(mul2 - mul) < 0.02f);
		EXPECT_TRUE(smaller);
		// printf("%d -> %f -> %d -> %f\n", i, mul, vol100, mul2);
	}
	return true;
}

bool TestLinAlg() {
	static const float m1[16] = {
		1, 2, 3, 4,
		5, 6, 7, 8,
		9, 10, 11, 12,
		13, 14, 15, 16
	};
	static const float m2[16] = {
		56, 0, 24, 2,
		0.5f, 35, 2, 4,
		1, 6, 1, 2,
		4, 0, -1, -4
	};
	static const float correct[16] = {
		298.f, 380.f, 462.f, 544.f,
		245.5f, 287.f, 328.5f, 370.f,
		66.f, 76.f, 86.f, 96.f,
		-57.f, -58.f, -59.f, -60.f,
	};

	float d[16]{};

	fast_matrix_mul_4x4(d, m1, m2);

	for (int i = 0; i < 16; i += 4) {
		// printf("%0.2f, %0.2f, %0.2f, %0.2f,\n", d[i], d[i + 1], d[i + 2], d[i + 3]);
	}

	for (int i = 0; i < 16; i++) {
		EXPECT_EQ_FLOAT(d[i], correct[i]);
	}


	// OK, now test 4x3 multiplication.
	float a4x4[16];
	float b4x4[16];

	ConvertMatrix4x3To4x4(a4x4, m1);
	ConvertMatrix4x3To4x4(b4x4, m1);
	Matrix4ByMatrix4(d, a4x4, b4x4);

	for (int i = 0; i < 16; i += 4) {
		// printf("%0.2f, %0.2f, %0.2f, %0.2f,\n", d[i], d[i + 1], d[i + 2], d[i + 3]);
	}

	static const float correct4x4[16] = {
		30.00, 36.00, 42.00, 0.00,
		66.00, 81.00, 96.00, 0.00,
		102.00, 126.00, 150.00, 0.00,
		148.00, 182.00, 216.00, 1.00,
	};

	for (int i = 0; i < 16; i++) {
		EXPECT_EQ_FLOAT(d[i], correct4x4[i]);
	}

	ConvertMatrix4x3To4x4Transposed(b4x4, m1);
	Matrix4ByMatrix4(d, a4x4, b4x4);

	static const float correct4x4transposed[16] = {
		14.00, 32.00, 50.00, 68.00,
		32.00, 77.00, 122.00, 167.00,
		50.00, 122.00, 194.00, 266.00,
		68.00, 167.00, 266.00, 366.00,
	};

	for (int i = 0; i < 16; i++) {
		EXPECT_EQ_FLOAT(d[i], correct4x4transposed[i]);
	}
	// TODO: Add direct 4x3 x 4x3 multiplication
	return true;
}

bool TestSplitSearch() {
	std::string part1 = "The quick brown fox jumps";
	std::string part2 = " over the lazy dog.";

	size_t offset = SplitSearch("jumps over", part1, part2);
	EXPECT_EQ_INT(offset, 20);
	offset = SplitSearch("quick", part1, part2);
	EXPECT_EQ_INT(offset, 4);
	offset = SplitSearch(" over", part1, part2);
	EXPECT_EQ_INT(offset, 25);
	offset = SplitSearch("fox jumps", part1, part2);
	EXPECT_EQ_INT(offset, 16);
	offset = SplitSearch("dog.", part1, part2);
	EXPECT_EQ_INT(offset, 40);
	return true;
}

bool TestFriendlyPath() {
	Path path("/home/user/PPSSPP/games/My Game (USA)/EBOOT.PBP");
	Path baseDir("/home/user/PPSSPP/games/");
	std::string friendlyPath = GetFriendlyPath(path, baseDir, "ms:/");
	EXPECT_EQ_STR(friendlyPath, std::string("ms:/My Game (USA)/EBOOT.PBP"));
	return true;
}

bool TestCmdLine() {
	{
		const char *argv[] = {
			"ppsspp",
			"--fullscreen",
			"--graphics=d3d11",
			"--pause-menu-exit",
			"My_Game.iso"
		};
		int argc = ARRAY_SIZE(argv);
		CommandLineOptions options;
		options.Parse(argc, argv, CmdLineMode::Application);
		EXPECT_TRUE(options.fullscreen.value_or(false));
		if (options.bootFilenames.empty()) {
			EXPECT_TRUE(false);
			return false;
		}
		EXPECT_EQ_STR(options.bootFilenames[0], std::string("My_Game.iso"));
		EXPECT_TRUE(options.gpuBackend.has_value());
		EXPECT_EQ_INT((int)options.gpuBackend.value_or((GPUBackend)-1), (int)GPUBackend::DIRECT3D11);
		EXPECT_TRUE(options.pauseMenuExit.value_or(false));
	}
	// --timeout is headless-only (only headless/Headless.cpp reads it), so it must be parsed in Headless mode.
	{
		const char *argv[] = {
			"ppsspp",
			"--timeout=3",
			"My_Game.iso"
		};
		int argc = ARRAY_SIZE(argv);
		CommandLineOptions options;
		options.Parse(argc, argv, CmdLineMode::Headless);
		EXPECT_EQ_INT(options.timeout.value_or(0), 3);
	}
	// Test GL version override
	{
		const char *argv[] = {
			"ppsspp",
			"--graphics=gles3.3",
		};
		int argc = ARRAY_SIZE(argv);
		CommandLineOptions options;
		options.Parse(argc, argv);
		EXPECT_EQ_INT(options.force_gl_version, 33);
	}
	return true;
}

// Check that RTTI is working.
bool TestLang() {
	struct Base { virtual ~Base() = default; };
	struct Derived : Base {};

	Base* b = new Derived;
	bool equals = typeid(*b) == typeid(Derived);
	EXPECT_TRUE(equals);
	return true;
}

typedef bool (*TestFunc)();
struct TestItem {
	const char *name;
	TestFunc func;
};

#define TEST_ITEM(name) { #name, &Test ##name, }

bool TestArmEmitter();
bool TestArm64Emitter();
bool TestX64Emitter();
bool TestRiscVEmitter();
bool TestLoongArch64Emitter();
bool TestShaderGenerators();
bool TestSoftwareGPUJit();
bool TestIRPassSimplify();
bool TestThreadManager();
bool TestVFS();
bool TestZipSlip();
bool TestLzrc();
bool TestDemangle();

// Tab/Shift+Tab focus navigation walks the view hierarchy in declaration order rather than by
// geometry, so what it does is entirely determined by CollectTabOrder - which is worth pinning
// down, since the interesting cases (nesting, hidden tabs, disabled items) are all structural.
bool TestUITabOrder() {
	using namespace UI;

	LinearLayout root(ORIENT_VERTICAL);

	// A label is not a tab stop, but the item after it is.
	root.Add(new TextView("label"));
	Choice *a = root.Add(new Choice("a"));

	// Nested groups are flattened in place, in order.
	LinearLayout *inner = root.Add(new LinearLayout(ORIENT_HORIZONTAL));
	Choice *b = inner->Add(new Choice("b"));
	Choice *disabled = inner->Add(new Choice("disabled"));
	disabled->SetEnabled(false);

	// A hidden subtree is skipped whole - this is how the inactive tabs of a TabHolder,
	// which are V_GONE rather than removed, stay out of the way.
	LinearLayout *hidden = root.Add(new LinearLayout(ORIENT_VERTICAL));
	hidden->SetVisibility(V_GONE);
	hidden->Add(new Choice("hidden"));

	root.Add(new Spacer());
	Choice *c = root.Add(new Choice("c"));
	Choice *invisible = root.Add(new Choice("invisible"));
	invisible->SetVisibility(V_INVISIBLE);

	std::vector<View *> order;
	root.CollectTabOrder(&order);
	EXPECT_EQ_INT((int)order.size(), 3);
	EXPECT_TRUE(order[0] == a);
	EXPECT_TRUE(order[1] == b);
	EXPECT_TRUE(order[2] == c);

	// Tab walks forwards and wraps at the end, Shift+Tab does the reverse.
	EXPECT_TRUE(FindTabOrderNeighbor(&root, a, FocusMove::NEXT) == b);
	EXPECT_TRUE(FindTabOrderNeighbor(&root, b, FocusMove::NEXT) == c);
	EXPECT_TRUE(FindTabOrderNeighbor(&root, c, FocusMove::NEXT) == a);
	EXPECT_TRUE(FindTabOrderNeighbor(&root, c, FocusMove::PREV) == b);
	EXPECT_TRUE(FindTabOrderNeighbor(&root, b, FocusMove::PREV) == a);
	EXPECT_TRUE(FindTabOrderNeighbor(&root, a, FocusMove::PREV) == c);

	// A view that has gone away (or was never a stop) doesn't stall navigation - it starts
	// from whichever end we're heading towards.
	EXPECT_TRUE(FindTabOrderNeighbor(&root, disabled, FocusMove::NEXT) == a);
	EXPECT_TRUE(FindTabOrderNeighbor(&root, disabled, FocusMove::PREV) == c);
	EXPECT_TRUE(FindTabOrderNeighbor(&root, nullptr, FocusMove::NEXT) == a);

	// With a single stop, both directions land back on it, and with none there's nothing to do.
	LinearLayout one(ORIENT_VERTICAL);
	Choice *only = one.Add(new Choice("only"));
	EXPECT_TRUE(FindTabOrderNeighbor(&one, only, FocusMove::NEXT) == only);
	EXPECT_TRUE(FindTabOrderNeighbor(&one, only, FocusMove::PREV) == only);

	LinearLayout empty(ORIENT_VERTICAL);
	empty.Add(new TextView("just a label"));
	EXPECT_TRUE(FindTabOrderNeighbor(&empty, nullptr, FocusMove::NEXT) == nullptr);

	return true;
}

bool TestTextureReplacer();

TestItem availableTests[] = {
#if PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)
	TEST_ITEM(Arm64Emitter),
#endif
#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)
	TEST_ITEM(ArmEmitter),
#endif
#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)
	TEST_ITEM(X64Emitter),
#endif
#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86) || PPSSPP_ARCH(RISCV64)
	TEST_ITEM(RiscVEmitter),
#endif
#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86) || PPSSPP_ARCH(LOONGARCH64)
	TEST_ITEM(LoongArch64Emitter),
#endif
	TEST_ITEM(VertexJit),
	TEST_ITEM(Asin),
	TEST_ITEM(SinCos),
	TEST_ITEM(VFPUSinCos),
	TEST_ITEM(MathUtil),
	TEST_ITEM(Parsers),
	TEST_ITEM(TruncateCpy),
	TEST_ITEM(MemBlockInfoSaveState),
	TEST_ITEM(Serializer),
	TEST_ITEM(BlockAllocator),
	TEST_ITEM(SymbolMap),
	TEST_ITEM(Hashmaps),
	TEST_ITEM(Breakpoints),
	TEST_ITEM(TempBreakpoints),
	TEST_ITEM(Utf8),
	TEST_ITEM(IRPassSimplify),
	TEST_ITEM(Jit),
	TEST_ITEM(VFPUMatrixTranspose),
	TEST_ITEM(ParseLBN),
	TEST_ITEM(QuickTexHash),
	TEST_ITEM(CLZ),
	TEST_ITEM(MemMap),
	TEST_ITEM(ShaderGenerators),
	TEST_ITEM(SoftwareGPUJit),
	TEST_ITEM(Path),
	TEST_ITEM(AndroidContentURI),
	TEST_ITEM(ThreadManager),
	TEST_ITEM(WrapText),
	TEST_ITEM(TinySet),
	TEST_ITEM(FastVec),
	TEST_ITEM(SmallDataConvert),
	TEST_ITEM(InputMapping),
	TEST_ITEM(EscapeMenuString),
	TEST_ITEM(VFS),
	TEST_ITEM(Substitutions),
	TEST_ITEM(IniFile),
	TEST_ITEM(ColorConv),
	TEST_ITEM(CharQueue),
	TEST_ITEM(Buffer),
	TEST_ITEM(SIMD),
	TEST_ITEM(CrossSIMD),
	TEST_ITEM(VolumeFunc),
	TEST_ITEM(SplitSearch),
	TEST_ITEM(FriendlyPath),
	TEST_ITEM(LinAlg),
	TEST_ITEM(Lang),
	TEST_ITEM(CmdLine),
	TEST_ITEM(ZipSlip),
	TEST_ITEM(Lzrc),
	TEST_ITEM(Demangle),
	TEST_ITEM(TextureReplacer),
	TEST_ITEM(UITabOrder),
};

int main(int argc, const char *argv[]) {
	// Never block on a modal dialog - these get run from CI and from tooling.
	SetupCRT(true);
	SetCurrentThreadName("UnitTest");
	TimeInit();

	printf("CPU name: %s\n", cpu_info.cpu_string);
	printf("ABI: %s\n", GetCompilerABI());

	// In case we're on ARM, assume these are available.
	cpu_info.bNEON = true;
	cpu_info.bVFP = true;
	cpu_info.bVFPv3 = true;
	cpu_info.bVFPv4 = true;
	g_Config.bEnableLogging = true;
	g_logManager.DisableOutput(LogOutput::DebugString);  // not really needed

	// Collect the set of tests to run: "all", or one or more test names by
	// (case-insensitive) name. Every non-"all" argument must match a known test name, or we
	// bail out with the usage text - a silent partial run (e.g. from a typo) would be worse
	// than an error.
	std::vector<TestItem> testsToRun;
	bool badArg = false;
	if (argc == 2 && !strcasecmp(argv[1], "all")) {
		for (const auto &f : availableTests) {
			testsToRun.push_back(f);
		}
	} else {
		for (int i = 1; i < argc; ++i) {
			const TestItem *found = nullptr;
			for (const auto &f : availableTests) {
				if (!strcasecmp(argv[i], f.name)) {
					found = &f;
					break;
				}
			}
			if (found) {
				testsToRun.push_back(*found);
			} else {
				fprintf(stderr, "Unknown test: %s\n", argv[i]);
				badArg = true;
			}
		}
	}

	if (testsToRun.empty() || badArg) {
		fprintf(stderr, "You may select tests to run by passing one or more arguments, either \"all\" or one or more of the below.\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "Available tests:\n");
		for (auto f : availableTests) {
			fprintf(stderr, "  * %s\n", f.name);
		}
		return 1;
	}

	int passes = 0;
	int fails = 0;
	std::vector<const char *> failedTests;
	for (const auto &f : testsToRun) {
		printf("\n**** Running test %s ****\n", f.name);
		if (f.func()) {
			++passes;
		} else {
			printf("%s: FAILED\n", f.name);
			failedTests.push_back(f.name);
			++fails;
		}
	}
	if (passes > 0) {
		printf("%d tests passed.\n", passes);
	}
	if (fails > 0) {
		printf("%d tests failed!\n", fails);
		for (auto testName : failedTests) {
			printf("  * %s\n", testName);
		}
		return 2;
	}

	return 0;
}
