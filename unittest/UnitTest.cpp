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


#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <sstream>

#include "base/NativeApp.h"
#include "base/logging.h"
#include "Common/CPUDetect.h"
#include "Common/ArmEmitter.h"
#include "ext/disarm.h"
#include "math/math_util.h"
#include "util/text/parsers.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#include "unittest/JitHarness.h"
#include "unittest/UnitTest.h"

std::string System_GetProperty(SystemProperty prop) { return ""; }
int System_GetPropertyInt(SystemProperty prop) { return -1; }
void NativeMessageReceived(const char *message, const char *value) {}

#define M_PI_2     1.57079632679489661923

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

	x2=x * x; 
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

	const float B = 2;

	float x = 2 * gamma - gamma * abs(gamma);
	float y = 2 * theta - theta * abs(theta);
	const float P = 0.225;
	outsine = P * (y * abs(y) - y) + y;   // Q * y + P * y * abs(y)
	outcosine = P * (x * abs(x) - x) + x;   // Q * y + P * y * abs(y)
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

bool TestVFPUSinCos() {
	float sine, cosine;
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

	for (float angle = -10.0f; angle < 10.0f; angle++) {
		vfpu_sincos(angle, sine, cosine);
		EXPECT_APPROX_EQ_FLOAT(sine, sinf(angle * M_PI_2));
		EXPECT_APPROX_EQ_FLOAT(cosine, cosf(angle * M_PI_2));
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
	ILOG("Testing matrix %s", GetMatrixNotation(matrix, sz));
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
		ILOG("Column %i: %s", i, GetVectorNotation(colName, vsz));
		ILOG("Row %i: %s", i, GetVectorNotation(rowName, vsz));

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
		ILOG("Col: %s vs %s", a.str().c_str(), b.str().c_str());
		if (a.str() != b.str())
			ILOG("WRONG!");
		ILOG("Row: %s vs %s", c.str().c_str(), d.str().c_str());
		if (c.str() != d.str())
			ILOG("WRONG!");
	}
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

TestItem availableTests[] = {
#if defined(ARM64) || defined(_M_X64) || defined(_M_IX86)
	TEST_ITEM(Arm64Emitter),
#endif
#if defined(ARM) || defined(_M_X64) || defined(_M_IX86)
	TEST_ITEM(ArmEmitter),
#endif
#if defined(_M_X64) || defined(_M_IX86)
	TEST_ITEM(X64Emitter),
#endif
	TEST_ITEM(Asin),
	TEST_ITEM(SinCos),
	TEST_ITEM(VFPUSinCos),
	TEST_ITEM(MathUtil),
	TEST_ITEM(Parsers),
	TEST_ITEM(Jit),
	TEST_ITEM(MatrixTranspose)
};

int main(int argc, const char *argv[]) {
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
			return 2;
		}
	} else if (testFunc == nullptr) {
		fprintf(stderr, "You may select a test to run by passing an argument.\n");
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
