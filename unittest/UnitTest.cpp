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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>

#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
#endif

#include "Common/Data/Collections/TinySet.h"
#include "Common/Data/Collections/FastVec.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/Path.h"
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
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/MemMap.h"
#include "Core/KeyMap.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/GPUStateUtils.h"

#include "Common/File/AndroidContentURI.h"

#include "unittest/JitHarness.h"
#include "unittest/TestVertexJit.h"
#include "unittest/UnitTest.h"


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
void System_PostUIMessage(UIMessage message, const std::string &param) {}
void System_AudioGetDebugStats(char *buf, size_t bufSize) { if (buf) buf[0] = '\0'; }
void System_AudioClear() {}
void System_AudioPushSamples(const s32 *audio, int numSamples) {}

// TODO: To avoid having to define these here, these should probably be turned into system "requests".
// To clear the secret entirely, just save an empty string.
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) { return false; }
std::string NativeLoadSecret(std::string_view nameOfSecret) {
	return "";
}

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
		printf("%f: slow: %0.8f, %0.8f fast: %0.8f, %0.8f\n", f, slowsin, slowcos, fastsin, fastcos);
	}
	return true;
}


bool TestAsin() {
	for (int i = -100; i <= 100; i++) {
		float f = i / 100.0f;
		float slowval = asinf(f) / M_PI_2;
		float fastval = fastasin5(f) / M_PI_2;
		printf("slow: %0.16f fast: %0.16f\n", slowval, fastval);
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

		printf("sine: %f==%f cosine: %f==%f\n", sine, sinf(angle * M_PI_2), cosine, cosf(angle * M_PI_2));
	}
	return true;
}

bool TestMatrixTranspose() {
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
		{ 0x04000000, 0x00800000, Flags::NO_KERNEL },
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

			EXPECT_EQ_HEX(Memory::ValidSize(base, range.size), range.size);
			EXPECT_EQ_HEX(Memory::ValidSize(base, range.size + 1), range.size);
			EXPECT_EQ_HEX(Memory::ValidSize(base, range.size - 1), range.size - 1);
			EXPECT_EQ_HEX(Memory::ValidSize(base, 0), 0);
			EXPECT_EQ_HEX(Memory::ValidSize(base, 0x80000001), range.size);
			EXPECT_EQ_HEX(Memory::ValidSize(base, 0x40000001), range.size);
			EXPECT_EQ_HEX(Memory::ValidSize(base, 0x20000001), range.size);
			EXPECT_EQ_HEX(Memory::ValidSize(base, 0x10000001), range.size);

			EXPECT_EQ_HEX(Memory::ValidSize(base + range.size - 0x10, 0x20000001), 0x10);
		}
	}

	EXPECT_FALSE(Memory::IsValidAddress(0x00015000));
	EXPECT_FALSE(Memory::IsValidAddress(0x04900000));
	EXPECT_EQ_HEX(Memory::ValidSize(0x00015000, 4), 0);
	EXPECT_EQ_HEX(Memory::ValidSize(0x04900000, 4), 0);

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
	EXPECT_TRUE(treeURI.Parse(std::string(treeURIString)));
	AndroidContentURI dirURI;
	EXPECT_TRUE(dirURI.Parse(std::string(directoryURIString)));
	AndroidContentURI fileTreeURI;
	EXPECT_TRUE(fileTreeURI.Parse(std::string(fileTreeURIString)));
	AndroidContentURI fileTreeURICopy;
	EXPECT_TRUE(fileTreeURICopy.Parse(std::string(fileTreeURIString)));
	AndroidContentURI fileURI;
	EXPECT_TRUE(fileURI.Parse(std::string(fileNonTreeString)));

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

float DepthSliceFactor(u32 useFlags);

static bool TestDepthMath() {
	// These are in normalized space.
	static const volatile float testValues[] = { 0.0f, 0.1f, 0.5f, M_PI / 4.0f, 0.9f, 1.0f };

	// Flag combinations that can happen (any combination not included here is invalid, see comment
	// over in GPUStateUtils.cpp):
	static const u32 useFlagsArray[] = {
		0,
		GPU_USE_ACCURATE_DEPTH,
		GPU_USE_ACCURATE_DEPTH | GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT,
		GPU_USE_DEPTH_CLAMP | GPU_USE_ACCURATE_DEPTH,
		GPU_USE_DEPTH_CLAMP | GPU_USE_ACCURATE_DEPTH | GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT,  // Here, GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT should take precedence over USE_DEPTH_CLAMP.
	};
	static const float expectedScale[] = { 65535.0f, 262140.0f, 16777215.0f, 65535.0f, 16777215.0f, };
	static const float expectedOffset[] = { 0.0f, 0.375f, 0.498047f, 0.0f, 0.498047f, };

	EXPECT_REL_EQ_FLOAT(100000.0f, 100001.0f, 0.00001f);

	for (int j = 0; j < ARRAY_SIZE(useFlagsArray); j++) {
		u32 useFlags = useFlagsArray[j];
		printf("j: %d useflags: %d\n", j, useFlags);
		DepthScaleFactors factors = GetDepthScaleFactors(useFlags);

		EXPECT_EQ_FLOAT(factors.ScaleU16(), expectedScale[j]);
		EXPECT_REL_EQ_FLOAT(factors.Offset(), expectedOffset[j], 0.00001f);
		EXPECT_REL_EQ_FLOAT(factors.Scale(), DepthSliceFactor(useFlags), 0.0001f);

		for (int i = 0; i < ARRAY_SIZE(testValues); i++) {
			float testValue = testValues[i] * 65535.0f;

			float encoded = factors.EncodeFromU16(testValue);
			float decodedU16 = factors.DecodeToU16(encoded);
			EXPECT_REL_EQ_FLOAT(decodedU16, testValue, 0.0001f);
		}
	}

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
	ParsedIniLine line;
	line.ParseFrom(testLine);
	line.Reconstruct(&temp);
	EXPECT_EQ_STR(testLine, temp);

	temp.clear();
	line.ParseFrom(testLine2);
	line.Reconstruct(&temp);

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
bool TestShaderGenerators();
bool TestSoftwareGPUJit();
bool TestIRPassSimplify();
bool TestThreadManager();
bool TestVFS();

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
	TEST_ITEM(VertexJit),
	TEST_ITEM(Asin),
	TEST_ITEM(SinCos),
	TEST_ITEM(VFPUSinCos),
	TEST_ITEM(MathUtil),
	TEST_ITEM(Parsers),
	TEST_ITEM(IRPassSimplify),
	TEST_ITEM(Jit),
	TEST_ITEM(MatrixTranspose),
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
	TEST_ITEM(DepthMath),
	TEST_ITEM(InputMapping),
	TEST_ITEM(EscapeMenuString),
	TEST_ITEM(VFS),
	TEST_ITEM(Substitutions),
	TEST_ITEM(IniFile),
	TEST_ITEM(ColorConv),
};

int main(int argc, const char *argv[]) {
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

	bool allTests = false;
	TestFunc testFunc = nullptr;
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "all")) {
			allTests = true;
		}
		for (auto f : availableTests) {
			if (!strcasecmp(argv[1], f.name)) {
				testFunc = f.func;
				break;
			}
		}
	}

	if (allTests) {
		int passes = 0;
		int fails = 0;
		for (auto f : availableTests) {
			if (f.func()) {
				++passes;
			} else {
				printf("%s: FAILED\n", f.name);
				++fails;
			}
		}
		if (passes > 0) {
			printf("%d tests passed.\n", passes);
		}
		if (fails > 0) {
			printf("%d tests failed!\n", fails);
			return 2;
		}
	} else if (testFunc == nullptr) {
		fprintf(stderr, "You may select a test to run by passing an argument, either \"all\" or one or more of the below.\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "Available tests:\n");
		for (auto f : availableTests) {
			fprintf(stderr, "  * %s\n", f.name);
		}
		return 1;
	} else {
		if (!testFunc()) {
			return 2;
		}
	}

	return 0;
}
