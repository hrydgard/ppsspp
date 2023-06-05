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

#pragma once

#include <cmath>
#include <string>
#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

#define _VD (op & 0x7F)
#define _VS ((op>>8) & 0x7F)
#define _VT ((op>>16) & 0x7F)

inline int Xpose(int v) {
	return v^0x20;
}

// Half of PI, or 90 degrees.
#ifndef M_PI_2
#define M_PI_2     1.57079632679489661923
#endif

// The VFPU uses weird angles where 4.0 represents a full circle. This makes it possible to return
// exact 1.0/-1.0 values at certain angles.
//
// The current code attempts to match VFPU sin/cos exactly.
// Possibly affected games:
//     Final Fantasy III               (#2921 )
//     Hitman Reborn 2                 (#12900)
//     Cho Aniki Zero                  (#13705)
//     Hajime no Ippo                  (#13671) 
//     Dissidia Duodecim Final Fantasy (#6710 )
//
// Messing around with the modulo functions? try https://www.desmos.com/calculator.

extern float vfpu_sin(float);
extern float vfpu_cos(float);
extern void vfpu_sincos(float, float&, float&);

extern float vfpu_asin(float);

inline float vfpu_clamp(float v, float min, float max) {
	// Note: NAN is preserved, and -0.0 becomes +0.0 if min=+0.0.
	return v >= max ? max : (v <= min ? min : v);
}

float vfpu_dot(const float a[4], const float b[4]);
float vfpu_sqrt(float a);
float vfpu_rsqrt(float a);

extern float vfpu_exp2(float);
extern float vfpu_rexp2(float);
extern float vfpu_log2(float);
extern float vfpu_rcp(float);

extern void vrnd_init_default(uint32_t *rcx);
extern void vrnd_init(uint32_t seed, uint32_t *rcx);
extern uint32_t vrnd_generate(uint32_t *rcx);

inline uint32_t get_uexp(uint32_t x) {
	return (x >> 23) & 0xFF;
}

inline int32_t get_exp(uint32_t x) {
	return get_uexp(x) - 127;
}

inline int32_t get_mant(uint32_t x) {
	// Note: this returns the hidden 1.
	return (x & 0x007FFFFF) | 0x00800000;
}

inline int32_t get_sign(uint32_t x) {
	return x & 0x80000000;
}

#define VFPU_FLOAT16_EXP_MAX    0x1f
#define VFPU_SH_FLOAT16_SIGN    15
#define VFPU_MASK_FLOAT16_SIGN  0x1
#define VFPU_SH_FLOAT16_EXP     10
#define VFPU_MASK_FLOAT16_EXP   0x1f
#define VFPU_SH_FLOAT16_FRAC    0
#define VFPU_MASK_FLOAT16_FRAC  0x3ff

enum VectorSize {
	V_Single = 1,
	V_Pair = 2,
	V_Triple = 3,
	V_Quad = 4,
	V_Invalid = -1,
};

enum MatrixSize {
	M_1x1 = 1,
	M_2x2 = 2,
	M_3x3 = 3,
	M_4x4 = 4,
	M_Invalid = -1
};

inline u32 VFPU_SWIZZLE(int x, int y, int z, int w) {
	return (x << 0) | (y << 2) | (z << 4) | (w << 6);
}

inline u32 VFPU_MASK(int x, int y, int z, int w) {
	return (x << 0) | (y << 1) | (z << 2) | (w << 3);
}

inline u32 VFPU_ANY_SWIZZLE() {
	return 0x000000FF;
}

inline u32 VFPU_ABS(int x, int y, int z, int w) {
	return VFPU_MASK(x, y, z, w) << 8;
}

inline u32 VFPU_CONST(int x, int y, int z, int w) {
	return VFPU_MASK(x, y, z, w) << 12;
}

inline u32 VFPU_NEGATE(int x, int y, int z, int w) {
	return VFPU_MASK(x, y, z, w) << 16;
}

enum class VFPUConst {
	NONE = -1,
	ZERO,
	ONE,
	TWO,
	HALF,
	THREE,
	THIRD,
	FOURTH,
	SIXTH,
};

inline u32 VFPU_MAKE_CONSTANTS(VFPUConst x, VFPUConst y, VFPUConst z, VFPUConst w) {
	u32 result = 0;
	if (x != VFPUConst::NONE) {
		// This sets the constant flag and the swizzle/abs flags for the right constant.
		result |= (((int)x & 3) << 0) | (((int)x & 4) << 6) | (1 << 12);
	}
	if (y != VFPUConst::NONE) {
		result |= (((int)y & 3) << 2) | (((int)y & 4) << 7) | (1 << 13);
	}
	if (z != VFPUConst::NONE) {
		result |= (((int)z & 3) << 4) | (((int)z & 4) << 8) | (1 << 14);
	}
	if (w != VFPUConst::NONE) {
		result |= (((int)w & 3) << 6) | (((int)w & 4) << 9) | (1 << 15);
	}
	return result;
}

u32 VFPURewritePrefix(int ctrl, u32 remove, u32 add);

void ReadMatrix(float *rd, MatrixSize size, int reg);
void WriteMatrix(const float *rs, MatrixSize size, int reg);

void WriteVector(const float *rs, VectorSize N, int reg);
void ReadVector(float *rd, VectorSize N, int reg);

void GetVectorRegs(u8 regs[4], VectorSize N, int vectorReg);
void GetMatrixRegs(u8 regs[16], MatrixSize N, int matrixReg);
 
// Translate between vector and matrix size. Possibly we should simply
// join the two enums, but the type safety is kind of nice.
VectorSize GetVectorSize(MatrixSize sz);
MatrixSize GetMatrixSize(VectorSize sz);

// Note that if matrix is a transposed matrix (E format), GetColumn will actually return rows,
// and vice versa.
int GetColumnName(int matrix, MatrixSize msize, int column, int offset);
int GetRowName(int matrix, MatrixSize msize, int row, int offset);

int GetMatrixName(int matrix, MatrixSize msize, int column, int row, bool transposed);

void GetMatrixColumns(int matrixReg, MatrixSize msize, u8 vecs[4]);
void GetMatrixRows(int matrixReg, MatrixSize msize, u8 vecs[4]);

enum MatrixOverlapType {
	OVERLAP_NONE = 0,
	OVERLAP_PARTIAL = 1,
	OVERLAP_EQUAL = 2,
	// Transposed too?  (same space but transposed)
};

MatrixOverlapType GetMatrixOverlap(int m1, int m2, MatrixSize msize);

// Returns a number from 0-7, good for checking overlap for 4x4 matrices.
static inline int GetMtx(int matrixReg) {
	return (matrixReg >> 2) & 7;
}

static inline VectorSize GetVecSize(MIPSOpcode op) {
	int a = (op >> 7) & 1;
	int b = (op >> 14) & 2;
	return (VectorSize)(a + b + 1);  // Safe, there are no other possibilities
}

static inline MatrixSize GetMtxSize(MIPSOpcode op) {
	int a = (op >> 7) & 1;
	int b = (op >> 14) & 2;
	return (MatrixSize)(a + b + 1);  // Safe, there are no other possibilities
}

VectorSize GetHalfVectorSizeSafe(VectorSize sz);
VectorSize GetHalfVectorSize(VectorSize sz);
VectorSize GetDoubleVectorSizeSafe(VectorSize sz);
VectorSize GetDoubleVectorSize(VectorSize sz);
VectorSize MatrixVectorSizeSafe(MatrixSize sz);
VectorSize MatrixVectorSize(MatrixSize sz);

static inline int GetNumVectorElements(VectorSize sz) {
	switch (sz) {
	case V_Single: return 1;
	case V_Pair:   return 2;
	case V_Triple: return 3;
	case V_Quad:   return 4;
	default:       return 0;
	}
}

int GetMatrixSideSafe(MatrixSize sz);
int GetMatrixSide(MatrixSize sz);
std::string GetVectorNotation(int reg, VectorSize size);
std::string GetMatrixNotation(int reg, MatrixSize size);
static inline bool IsMatrixTransposed(int matrixReg) {
	return (matrixReg >> 5) & 1;
}
static inline bool IsVectorColumn(int vectorReg) {
	return !((vectorReg >> 5) & 1);
}
static inline int TransposeMatrixReg(int matrixReg) {
	return matrixReg ^ 0x20;
}
int GetVectorOverlap(int reg1, VectorSize size1, int reg2, VectorSize size2);

bool GetVFPUCtrlMask(int reg, u32 *mask);

float Float16ToFloat32(unsigned short l);
void InitVFPU();
