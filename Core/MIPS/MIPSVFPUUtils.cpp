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

#include <cstdint>
#include <cstring>

#include "Common/BitScan.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/StringUtils.h"
#include "Core/Reporting.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#ifdef _MSC_VER
#pragma warning(disable: 4146)
#endif

union float2int {
	uint32_t i;
	float f;
};

void GetVectorRegs(u8 regs[4], VectorSize N, int vectorReg) {
	int mtx = (vectorReg >> 2) & 7;
	int col = vectorReg & 3;
	int row = 0;
	int length = 0;
	int transpose = (vectorReg>>5) & 1;

	switch (N) {
	case V_Single: transpose = 0; row=(vectorReg>>5)&3; length = 1; break;
	case V_Pair:   row=(vectorReg>>5)&2; length = 2; break;
	case V_Triple: row=(vectorReg>>6)&1; length = 3; break;
	case V_Quad:   row=(vectorReg>>5)&2; length = 4; break;
	default: _assert_msg_(false, "%s: Bad vector size", __FUNCTION__);
	}

	for (int i = 0; i < length; i++) {
		int index = mtx * 4;
		if (transpose)
			index += ((row+i)&3) + col*32;
		else
			index += col + ((row+i)&3)*32;
		regs[i] = index;
	}
}

void GetMatrixRegs(u8 regs[16], MatrixSize N, int matrixReg) {
	int mtx = (matrixReg >> 2) & 7;
	int col = matrixReg & 3;

	int row = 0;
	int side = 0;
	int transpose = (matrixReg >> 5) & 1;

	switch (N) {
	case M_1x1: transpose = 0; row = (matrixReg >> 5) & 3; side = 1; break;
	case M_2x2: row = (matrixReg >> 5) & 2; side = 2; break;
	case M_3x3: row = (matrixReg >> 6) & 1; side = 3; break;
	case M_4x4: row = (matrixReg >> 5) & 2; side = 4; break;
	default: _assert_msg_(false, "%s: Bad matrix size", __FUNCTION__);
	}

	for (int i = 0; i < side; i++) {
		for (int j = 0; j < side; j++) {
			int index = mtx * 4;
			if (transpose)
				index += ((row+i)&3) + ((col+j)&3)*32;
			else
				index += ((col+j)&3) + ((row+i)&3)*32;
			regs[j*4 + i] = index;
		}
	}
}

int GetMatrixName(int matrix, MatrixSize msize, int column, int row, bool transposed) {
	// TODO: Fix (?)
	int name = (matrix * 4) | (transposed << 5);
	switch (msize) {
	case M_4x4:
		if (row || column) {
			ERROR_LOG(Log::JIT, "GetMatrixName: Invalid row %i or column %i for size %i", row, column, msize);
		}
		break;

	case M_3x3:
		if (row & ~2) {
			ERROR_LOG(Log::JIT, "GetMatrixName: Invalid row %i for size %i", row, msize);
		}
		if (column & ~2) {
			ERROR_LOG(Log::JIT, "GetMatrixName: Invalid col %i for size %i", column, msize);
		}
		name |= (row << 6) | column;
		break;

	case M_2x2:
		if (row & ~2) {
			ERROR_LOG(Log::JIT, "GetMatrixName: Invalid row %i for size %i", row, msize);
		}
		if (column & ~2) {
			ERROR_LOG(Log::JIT, "GetMatrixName: Invalid col %i for size %i", column, msize);
		}
		name |= (row << 5) | column;
		break;

	default: _assert_msg_(false, "%s: Bad matrix size", __FUNCTION__);
	}

	return name;
}

int GetColumnName(int matrix, MatrixSize msize, int column, int offset) {
	return matrix * 4 + column + offset * 32;
}

int GetRowName(int matrix, MatrixSize msize, int column, int offset) {
	return 0x20 | (matrix * 4 + column + offset * 32);
}

void GetMatrixColumns(int matrixReg, MatrixSize msize, u8 vecs[4]) {
	int n = GetMatrixSide(msize);

	int col = matrixReg & 3;
	int row = (matrixReg >> 5) & 2;
	int transpose = (matrixReg >> 5) & 1;

	for (int i = 0; i < n; i++) {
		vecs[i] = (transpose << 5) | (row << 5) | (matrixReg & 0x1C) | (i + col);
	}
}

void GetMatrixRows(int matrixReg, MatrixSize msize, u8 vecs[4]) {
	int n = GetMatrixSide(msize);
	int col = matrixReg & 3;
	int row = (matrixReg >> 5) & 2;

	int swappedCol = row ? (msize == M_3x3 ? 1 : 2) : 0;
	int swappedRow = col ? 2 : 0;
	int transpose = ((matrixReg >> 5) & 1) ^ 1;

	for (int i = 0; i < n; i++) {
		vecs[i] = (transpose << 5) | (swappedRow << 5) | (matrixReg & 0x1C) | (i + swappedCol);
	}
}

void ReadVector(const MIPSState *mips, float *rd, VectorSize size, int reg) {
	int row;
	int length;
	switch (size) {
	case V_Single: rd[0] = mips->v[voffset[reg]]; return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	default: length = 0; break;
	}
	int transpose = (reg >> 5) & 1;
	const int mtx = ((reg << 2) & 0x70);
	const int col = reg & 3;
	// NOTE: We now skip the voffset lookups.
	if (transpose) {
		const int base = mtx + col;
		for (int i = 0; i < length; i++)
			rd[i] = mips->v[base + ((row + i) & 3) * 4];
	} else {
		const int base = mtx + col * 4;
		for (int i = 0; i < length; i++)
			rd[i] = mips->v[base + ((row + i) & 3)];
	}
}

void WriteVector(MIPSState *mips, const float *rd, VectorSize size, int reg) {
	int row;
	int length;

	switch (size) {
	case V_Single: if (!mips->VfpuWriteMask(0)) mips->v[voffset[reg]] = rd[0]; return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	default: length = 0; break;
	}

	const int mtx = ((reg << 2) & 0x70);
	const int col = reg & 3;
	bool transpose = (reg >> 5) & 1;
	// NOTE: We now skip the voffset lookups.
	if (transpose) {
		const int base = mtx + col;
		if (mips->VfpuWriteMask() == 0) {
			for (int i = 0; i < length; i++)
				mips->v[base + ((row+i) & 3) * 4] = rd[i];
		} else {
			for (int i = 0; i < length; i++) {
				if (!mips->VfpuWriteMask(i)) {
					mips->v[base + ((row+i) & 3) * 4] = rd[i];
				}
			}
		}
	} else {
		const int base = mtx + col * 4;
		if (mips->VfpuWriteMask() == 0) {
			for (int i = 0; i < length; i++)
				mips->v[base + ((row + i) & 3)] = rd[i];
		} else {
			for (int i = 0; i < length; i++) {
				if (!mips->VfpuWriteMask(i)) {
					mips->v[base + ((row + i) & 3)] = rd[i];
				}
			}
		}
	}
}

u32 VFPURewritePrefix(MIPSState *mips, int ctrl, u32 remove, u32 add) {
	u32 prefix = mips->vfpuCtrl[ctrl];
	return (prefix & ~remove) | add;
}

void ReadMatrix(const MIPSState *mips, float *rd, MatrixSize size, int reg) {
	int row = 0;
	int side = 0;
	int transpose = (reg >> 5) & 1;

	switch (size) {
	case M_1x1: transpose = 0; row = (reg >> 5) & 3; side = 1; break;
	case M_2x2: row = (reg >> 5) & 2; side = 2; break;
	case M_3x3: row = (reg >> 6) & 1; side = 3; break;
	case M_4x4: row = (reg >> 5) & 2; side = 4; break;
	default: side = 0; break;
	}

	int mtx = (reg >> 2) & 7;
	int col = reg & 3;

	// The voffset ordering is now integrated in these formulas,
	// eliminating a table lookup.
	const float *v = mips->v + (size_t)mtx * 16;
	if (transpose) {
		if (side == 4 && col == 0 && row == 0) {
			// Fast path: Simple 4x4 transpose. TODO: Optimize.
			for (int j = 0; j < 4; j++) {
				for (int i = 0; i < 4; i++) {
					rd[j * 4 + i] = v[i * 4 + j];
				}
			}
		} else {
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					int index = ((row + i) & 3) * 4 + ((col + j) & 3);
					rd[j * 4 + i] = v[index];
				}
			}
		}
	} else {
		if (side == 4 && col == 0 && row == 0) {
			// Fast path
			memcpy(rd, v, sizeof(float) * 16);  // rd[j * 4 + i] = v[j * 4 + i];
		} else {
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					int index = ((col + j) & 3) * 4 + ((row + i) & 3);
					rd[j * 4 + i] = v[index];
				}
			}
		}
	}
}

void WriteMatrix(MIPSState *mips, const float *rd, MatrixSize size, int reg) {
	int mtx = (reg>>2)&7;
	int col = reg&3;

	int row;
	int side;
	int transpose = (reg >> 5) & 1;

	switch (size) {
	case M_1x1: transpose = 0; row = (reg >> 5) & 3; side = 1; break;
	case M_2x2: row = (reg >> 5) & 2; side = 2; break;
	case M_3x3: row = (reg >> 6) & 1; side = 3; break;
	case M_4x4: row = (reg >> 5) & 2; side = 4; break;
	default: side = 0;
	}

	if (currentMIPS->VfpuWriteMask() != 0) {
		ERROR_LOG_REPORT(Log::CPU, "Write mask used with vfpu matrix instruction.");
	}

	// The voffset ordering is now integrated in these formulas,
	// eliminating a table lookup.
	float *v = mips->v + (size_t)mtx * 16;
	if (transpose) {
		if (side == 4 && row == 0 && col == 0 && mips->VfpuWriteMask() == 0x0) {
			// Fast path: Simple 4x4 transpose. TODO: Optimize.
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					v[i * 4 + j] = rd[j * 4 + i];
				}
			}
		} else {
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					if (j != side - 1 || !mips->VfpuWriteMask(i)) {
						int index = ((row + i) & 3) * 4 + ((col + j) & 3);
						v[index] = rd[j * 4 + i];
					}
				}
			}
		}
	} else {
		if (side == 4 && row == 0 && col == 0 && mips->VfpuWriteMask() == 0x0) {
			memcpy(v, rd, sizeof(float) * 16);  // v[j * 4 + i] = rd[j * 4 + i];
		} else {
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					if (j != side - 1 || !mips->VfpuWriteMask(i)) {
						int index = ((col + j) & 3) * 4 + ((row + i) & 3);
						v[index] = rd[j * 4 + i];
					}
				}
			}
		}
	}
}

int GetVectorOverlap(int vec1, VectorSize size1, int vec2, VectorSize size2) {
	// Different matrices?  Can't overlap, return early.
	if (((vec1 >> 2) & 7) != ((vec2 >> 2) & 7))
		return 0;

	int n1 = GetNumVectorElements(size1);
	int n2 = GetNumVectorElements(size2);
	u8 regs1[4];
	u8 regs2[4];
	GetVectorRegs(regs1, size1, vec1);
	GetVectorRegs(regs2, size2, vec2);
	int count = 0;
	for (int i = 0; i < n1; i++) {
		for (int j = 0; j < n2; j++) {
			if (regs1[i] == regs2[j])
				count++;
		}
	}
	return count;
}

VectorSize GetQuarterVectorSizeSafe(VectorSize sz) {
	switch (sz) {
	case V_Quad: return V_Single;
	default: return V_Invalid;
	}
}

VectorSize GetQuarterVectorSize(VectorSize sz) {
	VectorSize res = GetQuarterVectorSizeSafe(sz);
	_assert_msg_(res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

VectorSize GetHalfVectorSizeSafe(VectorSize sz) {
	switch (sz) {
	case V_Pair: return V_Single;
	case V_Quad: return V_Pair;
	default: return V_Invalid;
	}
}

VectorSize GetHalfVectorSize(VectorSize sz) {
	VectorSize res = GetHalfVectorSizeSafe(sz);
	_assert_msg_(res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

VectorSize GetQuadrupleVectorSizeSafe(VectorSize sz) {
	switch (sz) {
	case V_Single: return V_Quad;
	default: return V_Invalid;
	}
}

VectorSize GetQuadrupleVectorSize(VectorSize sz) {
	VectorSize res = GetQuadrupleVectorSizeSafe(sz);
	_assert_msg_(res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

VectorSize GetDoubleVectorSizeSafe(VectorSize sz) {
	switch (sz) {
	case V_Single: return V_Pair;
	case V_Pair: return V_Quad;
	default: return V_Invalid;
	}
}

VectorSize GetDoubleVectorSize(VectorSize sz) {
	VectorSize res = GetDoubleVectorSizeSafe(sz);
	_assert_msg_(res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

VectorSize GetVectorSizeSafe(MatrixSize sz) {
	switch (sz) {
	case M_1x1: return V_Single;
	case M_2x2: return V_Pair;
	case M_3x3: return V_Triple;
	case M_4x4: return V_Quad;
	default: return V_Invalid;
	}
}

VectorSize GetVectorSize(MatrixSize sz) {
	VectorSize res = GetVectorSizeSafe(sz);
	_assert_msg_(res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

MatrixSize GetMatrixSizeSafe(VectorSize sz) {
	switch (sz) {
	case V_Single: return M_1x1;
	case V_Pair: return M_2x2;
	case V_Triple: return M_3x3;
	case V_Quad: return M_4x4;
	default: return M_Invalid;
	}
}

MatrixSize GetMatrixSize(VectorSize sz) {
	MatrixSize res = GetMatrixSizeSafe(sz);
	_assert_msg_(res != M_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

VectorSize MatrixVectorSizeSafe(MatrixSize sz) {
	switch (sz) {
	case M_1x1: return V_Single;
	case M_2x2: return V_Pair;
	case M_3x3: return V_Triple;
	case M_4x4: return V_Quad;
	default: return V_Invalid;
	}
}

VectorSize MatrixVectorSize(MatrixSize sz) {
	VectorSize res = MatrixVectorSizeSafe(sz);
	_assert_msg_(res != V_Invalid, "%s: Bad matrix size", __FUNCTION__);
	return res;
}

int GetMatrixSideSafe(MatrixSize sz) {
	switch (sz) {
	case M_1x1: return 1;
	case M_2x2: return 2;
	case M_3x3: return 3;
	case M_4x4: return 4;
	default: return 0;
	}
}

int GetMatrixSide(MatrixSize sz) {
	int res = GetMatrixSideSafe(sz);
	_assert_msg_(res != 0, "%s: Bad matrix size", __FUNCTION__);
	return res;
}

// TODO: Optimize
MatrixOverlapType GetMatrixOverlap(int mtx1, int mtx2, MatrixSize msize) {
	int n = GetMatrixSide(msize);

	if (mtx1 == mtx2)
		return OVERLAP_EQUAL;

	u8 m1[16];
	u8 m2[16];
	GetMatrixRegs(m1, msize, mtx1);
	GetMatrixRegs(m2, msize, mtx2);

	// Simply do an exhaustive search.
	for (int x = 0; x < n; x++) {
		for (int y = 0; y < n; y++) {
			int val = m1[y * 4 + x];
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					if (m2[a * 4 + b] == val) {
						return OVERLAP_PARTIAL;
					}
				}
			}
		}
	}

	return OVERLAP_NONE;
}

std::string GetVectorNotation(int reg, VectorSize size) {
	int mtx = (reg>>2)&7;
	int col = reg&3;
	int row = 0;
	int transpose = (reg>>5)&1;
	char c;
	switch (size) {
	case V_Single:  transpose=0; c='S'; row=(reg>>5)&3; break;
	case V_Pair:    c='C'; row=(reg>>5)&2; break;
	case V_Triple:	c='C'; row=(reg>>6)&1; break;
	case V_Quad:    c='C'; row=(reg>>5)&2; break;
	default:        c='?'; break;
	}
	if (transpose && c == 'C') c='R';
	if (transpose)
		return StringFromFormat("%c%i%i%i", c, mtx, row, col);
	return StringFromFormat("%c%i%i%i", c, mtx, col, row);
}

std::string GetMatrixNotation(int reg, MatrixSize size) {
	int mtx = (reg>>2)&7;
	int col = reg&3;
	int row = 0;
	int transpose = (reg>>5)&1;
	char c;
	switch (size)
	{
	case M_2x2:     c='M'; row=(reg>>5)&2; break;
	case M_3x3:     c='M'; row=(reg>>6)&1; break;
	case M_4x4:     c='M'; row=(reg>>5)&2; break;
	default:        c='?'; break;
	}
	if (transpose && c=='M') c='E';
	if (transpose)
		return StringFromFormat("%c%i%i%i", c, mtx, row, col);
	return StringFromFormat("%c%i%i%i", c, mtx, col, row);
}

bool GetVFPUCtrlMask(int reg, u32 *mask) {
	switch (reg) {
	case VFPU_CTRL_SPREFIX:
	case VFPU_CTRL_TPREFIX:
		*mask = 0x000FFFFF;
		return true;
	case VFPU_CTRL_DPREFIX:
		*mask = 0x00000FFF;
		return true;
	case VFPU_CTRL_CC:
		*mask = 0x0000003F;
		return true;
	case VFPU_CTRL_INF4:
		*mask = 0xFFFFFFFF;
		return true;
	case VFPU_CTRL_RSV5:
	case VFPU_CTRL_RSV6:
	case VFPU_CTRL_REV:
		// Don't change anything, these regs are read only.
		return false;
	case VFPU_CTRL_RCX0:
	case VFPU_CTRL_RCX1:
	case VFPU_CTRL_RCX2:
	case VFPU_CTRL_RCX3:
	case VFPU_CTRL_RCX4:
	case VFPU_CTRL_RCX5:
	case VFPU_CTRL_RCX6:
	case VFPU_CTRL_RCX7:
		*mask = 0x000FFFFF;
		return true;
	default:
		return false;
	}
}

u32 GetVFPUCtrlSetBits(int reg) {
	switch (reg) {
	case VFPU_CTRL_RCX0:
	case VFPU_CTRL_RCX1:
	case VFPU_CTRL_RCX2:
	case VFPU_CTRL_RCX3:
	case VFPU_CTRL_RCX4:
	case VFPU_CTRL_RCX5:
	case VFPU_CTRL_RCX6:
	case VFPU_CTRL_RCX7:
		return 0x3F800000;
	default:
		return 0;
	}
}

float Float16ToFloat32(unsigned short l)
{
	float2int f2i;

	unsigned short float16 = l;
	unsigned int sign = (float16 >> VFPU_SH_FLOAT16_SIGN) & VFPU_MASK_FLOAT16_SIGN;
	int exponent = (float16 >> VFPU_SH_FLOAT16_EXP) & VFPU_MASK_FLOAT16_EXP;
	unsigned int fraction = float16 & VFPU_MASK_FLOAT16_FRAC;

	float f;
	if (exponent == VFPU_FLOAT16_EXP_MAX)
	{
		f2i.i = sign << 31;
		f2i.i |= 255 << 23;
		f2i.i |= fraction;
		f = f2i.f;
	}
	else if (exponent == 0 && fraction == 0)
	{
		f = sign == 1 ? -0.0f : 0.0f;
	}
	else
	{
		if (exponent == 0)
		{
			do
			{
				fraction <<= 1;
				exponent--;
			}
			while (!(fraction & (VFPU_MASK_FLOAT16_FRAC + 1)));

			fraction &= VFPU_MASK_FLOAT16_FRAC;
		}

		/* Convert to 32-bit single-precision IEEE754. */
		f2i.i = sign << 31;
		f2i.i |= (exponent + 112) << 23;
		f2i.i |= fraction << 13;
		f=f2i.f;
	}
	return f;
}

u32 vfpu_h2f(u16 h) {
	u32 sign = (u32)(h & 0x8000) << 16;
	u32 exp = (h >> 10) & 0x1F;
	u32 mant = h & 0x3FF;
	if (exp == 0) {
		// Zero and subnormal halves both give a signed zero.
		return sign;
	}
	if (exp == 31) {
		// Inf/NaN: the mantissa bits stay where they are, not shifted into place.
		return sign | 0x7F800000 | mant;
	}
	return sign | ((exp + 112) << 23) | (mant << 13);
}

u16 vfpu_f2h(u32 f) {
	u16 sign = (u16)((f >> 16) & 0x8000);
	u32 exp = (f >> 23) & 0xFF;
	u32 mant = f & 0x7FFFFF;
	if (exp == 255) {
		// Inf/NaN: the low ten mantissa bits carry over, so a NaN with those clear becomes inf.
		return sign | 0x7C00 | (u16)(mant & 0x3FF);
	}
	if (exp < 113) {
		// Below 2^-14: no subnormal halves, signed zero.
		return sign;
	}
	if (exp >= 143) {
		// 65536 and up: inf. Nothing rounds up to it, since the mantissa is truncated.
		return sign | 0x7C00;
	}
	return sign | (u16)((exp - 112) << 10) | (u16)(mant >> 13);
}

// Implementations of vmul and vdiv, assumed to
// be bitwise-exact to PSP, see
// https://github.com/hrydgard/ppsspp/issues/21070#issuecomment-4618120525
// for details. These functions behave the same as
// IEEE-755 multiplication/division with both
// denoramls-are-zero (DAZ) and flush-to-zero (FTZ) enabled,
// and specific bitpatterns used for NaN (which
// would be sNaN on x86).
// The threshold for FTZ is FLT_MIN-FLT_TRUE_MIN/4
// (which seems to be the same as x86 FTZ threshold
// on the machine tested; but may not be same elsewhere,
// e.g. ARM).
// Not currently used anywhere, just for reference purposes.

static inline float vfpu_mul(float a, float b) {
	uint32_t x, y, z;
	memcpy(&x, &a, sizeof(x));
	memcpy(&y, &b, sizeof(y));
	// Subnormal inputs -> zero.
	if (((x >> 23) & 255) == 0) x &= 0x80000000u;
	if (((y >> 23) & 255) == 0) y &= 0x80000000u;
	memcpy(&a, &x, sizeof(a));
	memcpy(&b, &y, sizeof(b));
	// IEEE-754 float32 round-to-nearest-ties-to-even multiplication.
	float c = a * b;
	memcpy(&z, &c, sizeof(z));
	// Subnormal outputs -> zero.
	if ((z & 0x7FFFFFFFu) <= 0x00800000u) {
		double r = double(a) * double(b);
		if (fabs(r) < 1.1754943157898259e-38) // double(FLT_MIN-0.25*FLT_TRUE_MIN)
			z &= 0x80000000u;
	}
	// NaN bitpattern.
	if ((z & 0x7FFFFFFFu) > 0x7F800000u)
		z = ((x^y) & 0x80000000u) | 0x7F800001u;
	memcpy(&c, &z, sizeof(c));
	return c;
}

static inline float vfpu_div(float a, float b) {
	uint32_t x, y, z;
	memcpy(&x, &a, sizeof(x));
	memcpy(&y, &b, sizeof(y));
	// Subnormal inputs -> zero.
	if (((x >> 23) & 255) == 0) x &= 0x80000000u;
	if (((y >> 23) & 255) == 0) y &= 0x80000000u;
	memcpy(&a, &x, sizeof(a));
	memcpy(&b, &y, sizeof(b));
	// IEEE-754 float32 round-to-nearest-ties-to-even division.
	float c = a / b;
	memcpy(&z, &c, sizeof(z));
	// Subnormal outputs -> zero.
	if ((z & 0x7FFFFFFFu) <= 0x00800000u) {
		double r = double(a) / double(b);
		if (fabs(r) < 1.1754943157898259e-38) // double(FLT_MIN-0.25*FLT_TRUE_MIN)
			z &= 0x80000000u;
	}
	// NaN bitpattern.
	if ((z & 0x7FFFFFFFu) > 0x7F800000u)
		z = ((x^y) & 0x80000000u) | 0x7F800001u;
	memcpy(&c, &z, sizeof(c));
	return c;
}

// Implementation of vdot instruction. Assumed bitwise-exact
// to PSP output on all inputs. For details see
// https://github.com/hrydgard/ppsspp/issues/21070#issuecomment-4640382516
// Reference C++ version.
float vfpu_dot_reference(const float a[4], const float b[4]) {
	int EXTRA_BITS = 2;
	uint32_t I = uint32_t(1) << 23, J = uint32_t(1) << (23 - EXTRA_BITS);
	int32_t s[4], e[4], ehi = -2*127;
	uint32_t p[4];
	int32_t val = 0;
	int has_inf = 0;
	for (int i = 0; i < 4; i++) {
		uint32_t x, y;
		memcpy(&x, a + i, sizeof(x));
		memcpy(&y, b + i, sizeof(y));
		int32_t ex = int32_t((x >> 23) & 255), ey = int32_t((y >> 23) & 255);
		uint32_t mx = x & (I - 1), my = y & (I - 1);
		if(ex == 255 || ey == 255) {
			// Handle inf/nan.
			float ret;
			uint32_t bits = 0x7F800001;
			memcpy(&ret, &bits, sizeof(ret));
			int sgn=((x ^ y) >> 31 ? -1 : +1);
			if(ex == 255 && mx != 0) return ret;      // x=nan
			if(ey == 255 && my != 0) return ret;      // y=nan
			if(ex == 255 && ey == 0) return ret;      // inf*0=nan
			if(ey == 255 && ex == 0) return ret;      // 0*inf=nan
			if(has_inf && has_inf != sgn) return ret; // inf-inf=nan
			has_inf=sgn;
		}
		// Compute sign/exponent and intermediate product.
		// Note that "exponent" here is basically ex+ey,
		// even though IEEE-754 exponent of the product
		// may be 1 higher (product of 2 numbers in [1;2)
		// range is in [1;4) range).
		// Note that product is computed in extra precision,
		// and using round-to-odd mode (for details see
		// https://github.com/hrydgard/ppsspp/issues/21070#issuecomment-4640372343).
		s[i] = int32_t((x ^ y) >> 31);
		e[i] = int32_t(ex + ey) - 2 * 127;
		uint64_t v = uint64_t(I + mx) * uint64_t(I + my);
		p[i] = uint32_t(v >> (23 - EXTRA_BITS));
		if(v & (J - 1)) p[i] |= 1; // round-to-odd
		if(!(ex && ey)) {e[i] = -2*127; p[i] = 0;} // subnormals -> zero
		if(e[i] > ehi) ehi = e[i];
	}
	if (has_inf) {
		uint32_t bits = (has_inf < 0 ? 0xFF800000 : 0x7F800000);
		float ret;
		memcpy(&ret, &bits, sizeof(bits));
		return ret;
	}
	// Align the terms according to max. exponent and compute
	// the intermediate sum.
	// Uses round-to-zero (i.e. truncation) to align; the
	// sum afterwards is integer (and therefore exact).
	for (int i = 0; i < 4; i++) {
		int32_t d = ehi - e[i];
		if(d > 28) d = 28;
		uint32_t v = (p[i] >> d);
		val += (s[i] ? -1 : +1) * int32_t(v);
	}
	uint32_t m = uint32_t(val < 0 ? -val : +val);
	// Remove the extra precision (using round-to-zero).
	m >>= EXTRA_BITS;
	// Adjust significand to 1.xxxxxxxxxxxxxxxxxxxxxxx
	// (i.e. 2^23 <= m < 2^24), unless 0. Rounding, if any,
	// is done via round-to-nearest-ties-to-even.
	if (m != 0) {
		int shift = 8 - int(clz32_nonzero(m));
		ehi += shift;
		if(shift > 0) {
			uint32_t r = uint32_t(1) << (shift - 1); // next-after-lsb bit
			m = (m >> shift) + ((m & (2 * r - 1)) + ((m >> shift) & 1) > r);
			// After rounding, the value may have been
			// bumped up into the next exponent range.
			if (m >= 2 * I) {m >>= 1; ehi += 1;}
		}
		if(shift < 0) m = m << -shift;
		_dbg_assert_msg_(m >= I && m < 2 * I, "Significand wrong: %08X", m);
	}
	else ehi = -128;
	if (ehi <= -127) {ehi = -127; m = 0;} // subnormals -> zero
	if (ehi >= +128) {ehi = +128; m = 0;} // inf
	uint32_t bits = (uint32_t(val < 0) << 31) |
		(uint32_t(ehi + 127) << 23) |
		uint32_t(m & 0x007FFFFF);
	float ret;
	memcpy(&ret, &bits, sizeof(ret));
	return ret;
}

#if PPSSPP_ARCH(ARM64_NEON) || PPSSPP_ARCH(SSE2)

// Inf and NaN inputs are rare, and the reference handles them.
static NO_INLINE float vfpu_dot_special(const float a[4], const float b[4]) {
	return vfpu_dot_reference(a, b);
}

// The end of the SIMD versions: drops the extra bits off the exact sum by truncation, then rounds to
// 24 bits, nearest even. With the top bit moved to bit 62, adding 2^38 - 1 plus the lowest kept bit
// and shifting out 39 bits does that for every magnitude, and adding the significand, implicit bit
// and all, onto the exponent field lets a carry from the rounding bump the exponent by itself. It's
// done in integers because the host rounding mode may be the game's.
static inline float vfpu_dot_finish(int32_t val, uint32_t ehi) {
	const uint32_t signBit = (uint32_t)val & 0x80000000u;
	const uint32_t m = (val < 0 ? 0u - (uint32_t)val : (uint32_t)val) >> 2;
	const int lz = 32 + (int)clz32_nonzero(m | 1);  // of m as 64 bits
	const uint64_t top = (uint64_t)m << (lz - 1);
	const uint64_t q = (top + ((1ULL << 38) - 1) + ((top >> 39) & 1)) >> 39;
	const int64_t t = ((int64_t)((int)ehi - 88 - lz) << 23) + (int64_t)q;
	uint32_t bits = t >= 0x7F800000 ? 0x7F800000u : (uint32_t)t;
	bits = (t < 0x00800000 || m == 0) ? 0u : bits;
	bits |= signBit;
	float ret;
	memcpy(&ret, &bits, sizeof(ret));
	return ret;
}

#endif

#if PPSSPP_ARCH(ARM64_NEON)

// The reference, four lanes at a time. Bit-exact with it.
static float vfpu_dot_neon(const float a[4], const float b[4]) {
	const uint32x4_t x = vld1q_u32((const uint32_t *)a);
	const uint32x4_t y = vld1q_u32((const uint32_t *)b);
	const uint32x4_t expBits = vdupq_n_u32(0x7F800000);
	const uint32x4_t xe = vandq_u32(x, expBits), ye = vandq_u32(y, expBits);

	// Zero and subnormal inputs make the product zero, with the lowest exponent. Inf and NaN get
	// 0x7FF added, above any real exponent sum, so one maximum finds the alignment and the specials.
	const uint32x4_t normal = vandq_u32(vtstq_u32(x, expBits), vtstq_u32(y, expBits));
	const uint32x4_t special = vceqq_u32(vmaxq_u32(xe, ye), expBits);
	const uint32x4_t e = vsraq_n_u32(vandq_u32(vshrq_n_u32(vaddq_u32(xe, ye), 23), normal), special, 21);
	uint32x4_t emax = vpmaxq_u32(e, e);
	emax = vpmaxq_u32(emax, emax);
	const uint32_t ehi = vgetq_lane_u32(emax, 0);
	if (ehi >= 0x7FF)
		return vfpu_dot_special(a, b);

	// 24x24-bit products, kept to 2 extra bits with round-to-odd.
	const uint32x4_t mantMask = vdupq_n_u32(0x007FFFFF), implicitBit = vdupq_n_u32(0x00800000);
	const uint32x4_t mx = vbslq_u32(mantMask, x, implicitBit);
	const uint32x4_t my = vbslq_u32(mantMask, y, implicitBit);
	const uint64x2_t plo = vmull_u32(vget_low_u32(mx), vget_low_u32(my));
	const uint64x2_t phi = vmull_high_u32(mx, my);
	const uint32x4_t kept = vcombine_u32(vshrn_n_u64(plo, 21), vshrn_n_u64(phi, 21));
	const uint32x4_t dropped = vandq_u32(vuzp1q_u32(vreinterpretq_u32_u64(plo), vreinterpretq_u32_u64(phi)), vdupq_n_u32((1 << 21) - 1));
	const uint32x4_t p = vandq_u32(vorrq_u32(kept, vminq_u32(dropped, vdupq_n_u32(1))), normal);

	// Align to the largest exponent by truncation, apply the signs, and sum exactly.
	const int32x4_t shift = vmaxq_s32(vreinterpretq_s32_u32(vsubq_u32(e, emax)), vdupq_n_s32(-28));
	const int32x4_t v = vreinterpretq_s32_u32(vshlq_u32(p, shift));
	const int32x4_t sign = vshrq_n_s32(vreinterpretq_s32_u32(veorq_u32(x, y)), 31);
	return vfpu_dot_finish(vaddvq_s32(vsubq_s32(veorq_s32(v, sign), sign)), ehi);
}

#elif PPSSPP_ARCH(SSE2)

// The reference, four lanes at a time. Bit-exact with it.
static float vfpu_dot_sse2(const float a[4], const float b[4]) {
	const __m128i x = _mm_loadu_si128((const __m128i *)a);
	const __m128i y = _mm_loadu_si128((const __m128i *)b);
	const __m128i expBits = _mm_set1_epi32(0x7F800000);
	const __m128i xe = _mm_and_si128(x, expBits), ye = _mm_and_si128(y, expBits);
	const __m128i zero = _mm_setzero_si128();

	// Zero and subnormal inputs make the product zero, with the lowest exponent. Inf and NaN get
	// 0x7FF, above any real exponent sum, so one maximum finds the alignment and the specials. All of
	// these fit in 15 bits, so the 16-bit maximum works on them.
	const __m128i flush = _mm_or_si128(_mm_cmpeq_epi32(xe, zero), _mm_cmpeq_epi32(ye, zero));
	const __m128i special = _mm_or_si128(_mm_cmpeq_epi32(xe, expBits), _mm_cmpeq_epi32(ye, expBits));
	const __m128i e = _mm_or_si128(_mm_andnot_si128(flush, _mm_srli_epi32(_mm_add_epi32(xe, ye), 23)), _mm_srli_epi32(special, 21));
	__m128i emax = _mm_max_epi16(e, _mm_shuffle_epi32(e, _MM_SHUFFLE(1, 0, 3, 2)));
	emax = _mm_max_epi16(emax, _mm_shuffle_epi32(emax, _MM_SHUFFLE(2, 3, 0, 1)));
	const uint32_t ehi = (uint32_t)_mm_cvtsi128_si32(emax);
	if (ehi >= 0x7FF)
		return vfpu_dot_special(a, b);

	// 24x24-bit products, even and odd lanes separately, kept to 2 extra bits with round-to-odd.
	const __m128i mantMask = _mm_set1_epi32(0x007FFFFF), implicitBit = _mm_set1_epi32(0x00800000);
	const __m128i mx = _mm_or_si128(_mm_and_si128(x, mantMask), implicitBit);
	const __m128i my = _mm_or_si128(_mm_and_si128(y, mantMask), implicitBit);
	const __m128i pEven = _mm_mul_epu32(mx, my);
	const __m128i pOdd = _mm_mul_epu32(_mm_srli_epi64(mx, 32), _mm_srli_epi64(my, 32));
	const __m128i kept = _mm_or_si128(_mm_srli_epi64(pEven, 21), _mm_slli_epi64(_mm_srli_epi64(pOdd, 21), 32));
	const __m128i lowMask = _mm_set_epi32(0, (1 << 21) - 1, 0, (1 << 21) - 1);
	const __m128i dropped = _mm_or_si128(_mm_and_si128(pEven, lowMask), _mm_slli_epi64(_mm_and_si128(pOdd, lowMask), 32));
	const __m128i sticky = _mm_andnot_si128(_mm_cmpeq_epi32(dropped, zero), _mm_set1_epi32(1));
	const __m128i p = _mm_andnot_si128(flush, _mm_or_si128(kept, sticky));

	// Align to the largest exponent by truncation. SSE2 has no per-lane shift, so p >> d is done as
	// (p * 2^(28 - d)) >> 28, with the power of two built as float bits and converted by truncation.
	const __m128i d = _mm_min_epi16(_mm_sub_epi32(emax, e), _mm_set1_epi32(28));
	const __m128i scale = _mm_cvttps_epi32(_mm_castsi128_ps(_mm_slli_epi32(_mm_sub_epi32(_mm_set1_epi32(127 + 28), d), 23)));
	const __m128i sEven = _mm_srli_epi64(_mm_mul_epu32(p, scale), 28);
	const __m128i sOdd = _mm_srli_epi64(_mm_mul_epu32(_mm_srli_epi64(p, 32), _mm_srli_epi64(scale, 32)), 28);
	__m128i v = _mm_or_si128(sEven, _mm_slli_epi64(sOdd, 32));

	// Apply the signs and sum exactly.
	const __m128i sign = _mm_srai_epi32(_mm_xor_si128(x, y), 31);
	v = _mm_sub_epi32(_mm_xor_si128(v, sign), sign);
	v = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2)));
	v = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1)));
	return vfpu_dot_finish(_mm_cvtsi128_si32(v), ehi);
}

#endif

float vfpu_dot(const float a[4], const float b[4]) {
#if PPSSPP_ARCH(ARM64_NEON)
	return vfpu_dot_neon(a, b);
#elif PPSSPP_ARCH(SSE2)
	return vfpu_dot_sse2(a, b);
#else
	return vfpu_dot_reference(a, b);
#endif
}

//==============================================================================
// The code below attempts to exactly match behaviour of
// PSP's vrnd instructions. See investigation starting around
// https://github.com/hrydgard/ppsspp/issues/16946#issuecomment-1467261209
// for details.

// Redundant currently, since MIPSState::Init() already
// does this on its own, but left as-is to be self-contained.
void vrnd_init_default(uint32_t *rcx) {
	rcx[0] = 0x00000001;
	rcx[1] = 0x00000002;
	rcx[2] = 0x00000004;
	rcx[3] = 0x00000008;
	rcx[4] = 0x00000000;
	rcx[5] = 0x00000000;
	rcx[6] = 0x00000000;
	rcx[7] = 0x00000000;
}

void vrnd_init(uint32_t seed, uint32_t *rcx) {
	for(int i = 0; i < 8; ++i) rcx[i] =
		0x3F800000u |                          // 1.0f mask.
		((seed >> ((i / 4) * 16)) & 0xFFFFu) | // lower or upper half of the seed.
		(((seed >> (4 * i)) & 0xF) << 16);     // remaining nibble.

}

uint32_t vrnd_generate(uint32_t *rcx) {
	// The actual RNG state appears to be 5 parts
	// (32-bit each) stored into the registers as follows:
	uint32_t A = (rcx[0] & 0xFFFFu) | (rcx[4] << 16);
	uint32_t B = (rcx[1] & 0xFFFFu) | (rcx[5] << 16);
	uint32_t C = (rcx[2] & 0xFFFFu) | (rcx[6] << 16);
	uint32_t D = (rcx[3] & 0xFFFFu) | (rcx[7] << 16);
	uint32_t E = (((rcx[0] >> 16) & 0xF) <<  0) |
	             (((rcx[1] >> 16) & 0xF) <<  4) |
	             (((rcx[2] >> 16) & 0xF) <<  8) |
	             (((rcx[3] >> 16) & 0xF) << 12) |
	             (((rcx[4] >> 16) & 0xF) << 16) |
	             (((rcx[5] >> 16) & 0xF) << 20) |
	             (((rcx[6] >> 16) & 0xF) << 24) |
	             (((rcx[7] >> 16) & 0xF) << 28);
	// Update.
	// LCG with classic parameters.
	A = 69069u * A + 1u; // NOTE: decimal constants.
	// Xorshift, with classic parameters. Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs".
	B ^= B << 13;
	B ^= B >> 17;
	B ^= B <<  5;
	// Sequence similar to Pell numbers ( https://en.wikipedia.org/wiki/Pell_number ),
	// except with different starting values, and an occasional increment (E).
	uint32_t t= 2u * D + C + E;
	// NOTE: the details of how E-part is set are somewhat of a guess
	// at the moment. The expression below looks weird, but does match
	// the available test data.
	E = uint32_t((uint64_t(C) + uint64_t(D >> 1) + uint64_t(E)) >> 32);
	C = D;
	D = t;
	// Store.
	rcx[0] = 0x3F800000u | (((E >>  0) & 0xF) << 16) | (A & 0xFFFFu);
	rcx[1] = 0x3F800000u | (((E >>  4) & 0xF) << 16) | (B & 0xFFFFu);
	rcx[2] = 0x3F800000u | (((E >>  8) & 0xF) << 16) | (C & 0xFFFFu);
	rcx[3] = 0x3F800000u | (((E >> 12) & 0xF) << 16) | (D & 0xFFFFu);
	rcx[4] = 0x3F800000u | (((E >> 16) & 0xF) << 16) | (A >> 16);
	rcx[5] = 0x3F800000u | (((E >> 20) & 0xF) << 16) | (B >> 16);
	rcx[6] = 0x3F800000u | (((E >> 24) & 0xF) << 16) | (C >> 16);
	rcx[7] = 0x3F800000u | (((E >> 28) & 0xF) << 16) | (D >> 16);
	// Return value.
	return A + B + D;
}

//==============================================================================
// The PSP's VFPU computes rcp, rsq, sqrt, exp2, log2, sin/cos and asin with one quadratic
// interpolator. The top 7 bits of a 23-bit input index pick one of 128 segments with their own
// coefficients; the other 16 bits, x2, enter the linear term in full, while the squared term only
// sees the top 10 of them, as a distance t from the middle of the segment. The squarer rounds t^2 up
// to a multiple of 256, and the squared term is floored separately from the rest. The sum is a
// significand in ulps of a per-segment exponent e, truncated to 22 bits like every VFPU result.
// The coefficients were fitted to the output of fp64's table-based versions (see
// https://github.com/hrydgard/ppsspp/issues/16946), and reproduce them bit for bit over every input.
struct VFPUSegment {
	int32_t c0;  // value at x2 = 0, in ulps of 2^(e - 150), implicit bit included
	int32_t m;   // slope in 2^-17 ulps per step of x2
	int16_t n;   // squared term coefficient
	int16_t e;   // binade whose ulps the segment works in
};

// 1/x for x in [1, 2).
static constexpr VFPUSegment vfpu_rcp_segments[128] = {
	{ 0x0FFFF02, -0x003F80C,  0x07F, 0x7E }, { 0x0FE0300, -0x003E86C,  0x07C, 0x7E },
	{ 0x0FC0ECF, -0x003D924,  0x079, 0x7E }, { 0x0FA2241, -0x003CA38,  0x076, 0x7E },
	{ 0x0F83D28, -0x003BBA0,  0x074, 0x7E }, { 0x0F65F5C, -0x003AD60,  0x071, 0x7E },
	{ 0x0F488B1, -0x0039F6C,  0x06E, 0x7E }, { 0x0F2B8FE, -0x00391C8,  0x06C, 0x7E },
	{ 0x0F0F01D, -0x0038470,  0x06A, 0x7E }, { 0x0EF2DE9, -0x0037764,  0x067, 0x7E },
	{ 0x0ED723A, -0x0036AA0,  0x065, 0x7E }, { 0x0EBBCED, -0x0035E20,  0x063, 0x7E },
	{ 0x0EA0DDF, -0x00351E8,  0x061, 0x7E }, { 0x0E864EE, -0x00345EC,  0x05F, 0x7E },
	{ 0x0E6C1FA, -0x0033A34,  0x05D, 0x7E }, { 0x0E524E2, -0x0032EBC,  0x05B, 0x7E },
	{ 0x0E38D87, -0x0032380,  0x059, 0x7E }, { 0x0E1FBCA, -0x003187C,  0x057, 0x7E },
	{ 0x0E06F8C, -0x0030DB4,  0x056, 0x7E }, { 0x0DEE8B4, -0x0030324,  0x054, 0x7E },
	{ 0x0DD6725, -0x002F8CC,  0x052, 0x7E }, { 0x0DBEAC0, -0x002EEA4,  0x051, 0x7E },
	{ 0x0DA7370, -0x002E4B4,  0x04F, 0x7E }, { 0x0D90118, -0x002DAF4,  0x04D, 0x7E },
	{ 0x0D7939E, -0x002D168,  0x04C, 0x7E }, { 0x0D62AEB, -0x002C808,  0x04B, 0x7E },
	{ 0x0D4C6E9, -0x002BED8,  0x049, 0x7E }, { 0x0D3677D, -0x002B5D4,  0x048, 0x7E },
	{ 0x0D20C95, -0x002AD00,  0x046, 0x7E }, { 0x0D0B616, -0x002A454,  0x045, 0x7E },
	{ 0x0CF63EF, -0x0029BD4,  0x043, 0x7E }, { 0x0CE1606, -0x0029378,  0x042, 0x7E },
	{ 0x0CCCC4B, -0x0028B48,  0x041, 0x7E }, { 0x0CB86A8, -0x002833C,  0x040, 0x7E },
	{ 0x0CA450A, -0x0027B58,  0x03F, 0x7E }, { 0x0C90760, -0x0027398,  0x03D, 0x7E },
	{ 0x0C7CD94, -0x0026BFC,  0x03C, 0x7E }, { 0x0C69796, -0x0026484,  0x03B, 0x7E },
	{ 0x0C56554, -0x0025D2C,  0x03A, 0x7E }, { 0x0C436BE, -0x00255FC,  0x039, 0x7E },
	{ 0x0C30BC1, -0x0024EE8,  0x038, 0x7E }, { 0x0C1E44E, -0x00247F4,  0x037, 0x7E },
	{ 0x0C0C055, -0x0024120,  0x036, 0x7E }, { 0x0BF9FC6, -0x0023A68,  0x035, 0x7E },
	{ 0x0BE8292, -0x00233D0,  0x034, 0x7E }, { 0x0BD68A8, -0x0022D54,  0x034, 0x7E },
	{ 0x0BC5200, -0x00226F8,  0x032, 0x7E }, { 0x0BB3E83, -0x00220B4,  0x032, 0x7E },
	{ 0x0BA2E2A, -0x0021A8C,  0x031, 0x7E }, { 0x0B920E4, -0x0021480,  0x030, 0x7E },
	{ 0x0B816A5, -0x0020E8C,  0x02F, 0x7E }, { 0x0B70F5D, -0x00208B0,  0x02F, 0x7E },
	{ 0x0B60B05, -0x00202F0,  0x02E, 0x7E }, { 0x0B5098D, -0x001FD48,  0x02D, 0x7E },
	{ 0x0B40AE9, -0x001F7B8,  0x02C, 0x7E }, { 0x0B30F0B, -0x001F23C,  0x02C, 0x7E },
	{ 0x0B215ED, -0x001ECDC,  0x02B, 0x7E }, { 0x0B11F80, -0x001E790,  0x02A, 0x7E },
	{ 0x0B02BB9, -0x001E258,  0x029, 0x7E }, { 0x0AF3A8C, -0x001DD38,  0x029, 0x7E },
	{ 0x0AE4BF1, -0x001D828,  0x028, 0x7E }, { 0x0AD5FDD, -0x001D330,  0x027, 0x7E },
	{ 0x0AC7644, -0x001CE4C,  0x027, 0x7E }, { 0x0AB8F1E, -0x001C97C,  0x026, 0x7E },
	{ 0x0AAAA5F, -0x001C4C0,  0x026, 0x7E }, { 0x0A9C800, -0x001C014,  0x025, 0x7E },
	{ 0x0A8E7F5, -0x001BB78,  0x025, 0x7E }, { 0x0A80A39, -0x001B6F4,  0x024, 0x7E },
	{ 0x0A72EBF, -0x001B280,  0x023, 0x7E }, { 0x0A6557E, -0x001AE1C,  0x023, 0x7E },
	{ 0x0A57E6F, -0x001A9C8,  0x023, 0x7E }, { 0x0A4A98B, -0x001A588,  0x022, 0x7E },
	{ 0x0A3D6C8, -0x001A154,  0x021, 0x7E }, { 0x0A3061C, -0x0019D34,  0x021, 0x7E },
	{ 0x0A23783, -0x0019920,  0x020, 0x7E }, { 0x0A16AF1, -0x001951C,  0x020, 0x7E },
	{ 0x0A0A063, -0x001912C,  0x01F, 0x7E }, { 0x09FD7CC, -0x0018D44,  0x01F, 0x7E },
	{ 0x09F112A, -0x0018970,  0x01E, 0x7E }, { 0x09E4C71, -0x00185A4,  0x01E, 0x7E },
	{ 0x09D899D, -0x00181EC,  0x01E, 0x7E }, { 0x09CC8A7, -0x0017E3C,  0x01D, 0x7E },
	{ 0x09C0987, -0x0017AA0,  0x01D, 0x7E }, { 0x09B4C36, -0x001770C,  0x01D, 0x7E },
	{ 0x09A90B0, -0x0017388,  0x01C, 0x7E }, { 0x099D6EB, -0x0017010,  0x01C, 0x7E },
	{ 0x0991EE4, -0x0016CA0,  0x01B, 0x7E }, { 0x0986892, -0x0016940,  0x01B, 0x7E },
	{ 0x097B3F0, -0x00165EC,  0x01B, 0x7E }, { 0x09700FA, -0x00162A4,  0x01A, 0x7E },
	{ 0x0964FA6, -0x0015F68,  0x01A, 0x7E }, { 0x0959FF3, -0x0015C34,  0x019, 0x7E },
	{ 0x094F1D7, -0x001590C,  0x019, 0x7E }, { 0x094454F, -0x00155F4,  0x019, 0x7E },
	{ 0x0939A54, -0x00152E0,  0x019, 0x7E }, { 0x092F0E4, -0x0014FDC,  0x018, 0x7E },
	{ 0x09248F5, -0x0014CE0,  0x018, 0x7E }, { 0x091A284, -0x00149EC,  0x018, 0x7E },
	{ 0x090FD8E, -0x0014704,  0x017, 0x7E }, { 0x0905A0A, -0x0014424,  0x017, 0x7E },
	{ 0x08FB7F6, -0x0014150,  0x017, 0x7E }, { 0x08F174E, -0x0013E88,  0x016, 0x7E },
	{ 0x08E7809, -0x0013BC4,  0x016, 0x7E }, { 0x08DDA26, -0x001390C,  0x016, 0x7E },
	{ 0x08D3DA1, -0x001365C,  0x015, 0x7E }, { 0x08CA272, -0x00133B4,  0x015, 0x7E },
	{ 0x08C0897, -0x0013118,  0x015, 0x7E }, { 0x08B700C, -0x0012E80,  0x014, 0x7E },
	{ 0x08AD8CB, -0x0012BF4,  0x014, 0x7E }, { 0x08A42D0, -0x001296C,  0x014, 0x7E },
	{ 0x089AE19, -0x00126F0,  0x014, 0x7E }, { 0x0891A9F, -0x0012478,  0x014, 0x7E },
	{ 0x0888861, -0x001220C,  0x014, 0x7E }, { 0x087F75B, -0x0011FA8,  0x013, 0x7E },
	{ 0x0876785, -0x0011D48,  0x013, 0x7E }, { 0x086D8E1, -0x0011AF4,  0x012, 0x7E },
	{ 0x0864B66, -0x00118A4,  0x012, 0x7E }, { 0x085BF13, -0x001165C,  0x012, 0x7E },
	{ 0x08533E4, -0x0011418,  0x012, 0x7E }, { 0x084A9D6, -0x00111E0,  0x012, 0x7E },
	{ 0x08420E4, -0x0010FAC,  0x012, 0x7E }, { 0x083990E, -0x0010D80,  0x011, 0x7E },
	{ 0x083124D, -0x0010B5C,  0x011, 0x7E }, { 0x0828C9E, -0x001093C,  0x011, 0x7E },
	{ 0x08207FF, -0x0010724,  0x011, 0x7E }, { 0x081846C, -0x0010510,  0x011, 0x7E },
	{ 0x08101E4, -0x0010304,  0x010, 0x7E }, { 0x0808061, -0x0010100,  0x010, 0x7E },
};

// 1/sqrt(x) for x in [1, 4), indexed by the bottom exponent bit and the top 22 mantissa bits.
static constexpr VFPUSegment vfpu_rsqrt_segments[128] = {
	{ 0x0FFFE88, -0x003F428,  0x0BC, 0x7E }, { 0x0FE0482, -0x003DD10,  0x0B5, 0x7E },
	{ 0x0FC1608, -0x003C6D4,  0x0AE, 0x7E }, { 0x0FA32AB, -0x003B16C,  0x0A7, 0x7E },
	{ 0x0F859FE, -0x0039CCC,  0x0A2, 0x7E }, { 0x0F68BA4, -0x00388E8,  0x09C, 0x7E },
	{ 0x0F4C73B, -0x00375B8,  0x096, 0x7E }, { 0x0F30C68, -0x0036334,  0x091, 0x7E },
	{ 0x0F15AD7, -0x0035154,  0x08C, 0x7E }, { 0x0EFB237, -0x003400C,  0x087, 0x7E },
	{ 0x0EE1238, -0x0032F5C,  0x083, 0x7E }, { 0x0EC7A94, -0x0031F34,  0x07E, 0x7E },
	{ 0x0EAEB01, -0x0030F94,  0x07A, 0x7E }, { 0x0E9633C, -0x0030078,  0x077, 0x7E },
	{ 0x0E7E307, -0x002F1D4,  0x073, 0x7E }, { 0x0E66A24, -0x002E3A8,  0x06F, 0x7E },
	{ 0x0E4F856, -0x002D5E8,  0x06C, 0x7E }, { 0x0E38D67, -0x002C898,  0x069, 0x7E },
	{ 0x0E22923, -0x002BBAC,  0x065, 0x7E }, { 0x0E0CB52, -0x002AF24,  0x062, 0x7E },
	{ 0x0DF73C3, -0x002A2FC,  0x060, 0x7E }, { 0x0DE224B, -0x002972C,  0x05D, 0x7E },
	{ 0x0DCD6BA, -0x0028BB4,  0x05A, 0x7E }, { 0x0DB90E5, -0x0028090,  0x057, 0x7E },
	{ 0x0DA50A0, -0x00275BC,  0x055, 0x7E }, { 0x0D915C5, -0x0026B38,  0x053, 0x7E },
	{ 0x0D7E02D, -0x00260FC,  0x051, 0x7E }, { 0x0D6AFB5, -0x0025708,  0x04E, 0x7E },
	{ 0x0D58435, -0x0024D54,  0x04C, 0x7E }, { 0x0D45D8E, -0x00243E8,  0x04A, 0x7E },
	{ 0x0D33B9E, -0x0023ABC,  0x048, 0x7E }, { 0x0D21E44, -0x00231C8,  0x046, 0x7E },
	{ 0x0D10562, -0x0022914,  0x045, 0x7E }, { 0x0CFF0DC, -0x0022098,  0x043, 0x7E },
	{ 0x0CEE094, -0x0021850,  0x041, 0x7E }, { 0x0CDD46F, -0x0021040,  0x03F, 0x7E },
	{ 0x0CCCC51, -0x0020864,  0x03E, 0x7E }, { 0x0CBC823, -0x00200B8,  0x03C, 0x7E },
	{ 0x0CAC7C9, -0x001F938,  0x03B, 0x7E }, { 0x0C9CB30, -0x001F1EC,  0x039, 0x7E },
	{ 0x0C8D23B, -0x001EACC,  0x038, 0x7E }, { 0x0C7DCD9, -0x001E3D4,  0x036, 0x7E },
	{ 0x0C6EAEE, -0x001DD08,  0x036, 0x7E }, { 0x0C5FC6E, -0x001D664,  0x034, 0x7E },
	{ 0x0C5113D, -0x001CFE8,  0x033, 0x7E }, { 0x0C4294B, -0x001C990,  0x032, 0x7E },
	{ 0x0C34486, -0x001C35C,  0x030, 0x7E }, { 0x0C262D7, -0x001BD50,  0x030, 0x7E },
	{ 0x0C18433, -0x001B760,  0x02E, 0x7E }, { 0x0C0A882, -0x001B198,  0x02E, 0x7E },
	{ 0x0BFCFB8, -0x001ABEC,  0x02D, 0x7E }, { 0x0BEF9C6, -0x001A660,  0x02B, 0x7E },
	{ 0x0BE2695, -0x001A0F4,  0x02B, 0x7E }, { 0x0BD561D, -0x0019BA4,  0x02A, 0x7E },
	{ 0x0BC884D, -0x0019670,  0x029, 0x7E }, { 0x0BBBD17, -0x0019158,  0x028, 0x7E },
	{ 0x0BAF46C, -0x0018C5C,  0x027, 0x7E }, { 0x0BA2E40, -0x001877C,  0x026, 0x7E },
	{ 0x0B96A84, -0x00182B0,  0x025, 0x7E }, { 0x0B8A92B, -0x0017E00,  0x025, 0x7E },
	{ 0x0B7EA2C, -0x0017968,  0x024, 0x7E }, { 0x0B72D79, -0x00174E8,  0x023, 0x7E },
	{ 0x0B67306, -0x0017080,  0x022, 0x7E }, { 0x0B5BAC5, -0x0016C2C,  0x022, 0x7E },
	{ 0x0B503E9, -0x002CBB4,  0x085, 0x7E }, { 0x0B39E19, -0x002BB5C,  0x080, 0x7E },
	{ 0x0B24072, -0x002ABA8,  0x07C, 0x7E }, { 0x0B0EAA8, -0x0029C84,  0x077, 0x7E },
	{ 0x0AF9C6D, -0x0028DEC,  0x073, 0x7E }, { 0x0AE557E, -0x0027FE0,  0x06F, 0x7E },
	{ 0x0AD1596, -0x002724C,  0x06B, 0x7E }, { 0x0ABDC78, -0x0026534,  0x067, 0x7E },
	{ 0x0AAA9E5, -0x0025890,  0x063, 0x7E }, { 0x0A97DA2, -0x0024C58,  0x060, 0x7E },
	{ 0x0A8577B, -0x0024088,  0x05D, 0x7E }, { 0x0A7373C, -0x0023520,  0x05A, 0x7E },
	{ 0x0A61CB1, -0x0022A14,  0x057, 0x7E }, { 0x0A507AD, -0x0021F64,  0x054, 0x7E },
	{ 0x0A3F800, -0x0021508,  0x051, 0x7E }, { 0x0A2ED7F, -0x0020B04,  0x04F, 0x7E },
	{ 0x0A1E801, -0x002014C,  0x04D, 0x7E }, { 0x0A0E761, -0x001F7E0,  0x04A, 0x7E },
	{ 0x09FEB74, -0x001EEC0,  0x048, 0x7E }, { 0x09EF418, -0x001E5E0,  0x046, 0x7E },
	{ 0x09E012B, -0x001DD48,  0x044, 0x7E }, { 0x09D128A, -0x001D4F0,  0x042, 0x7E },
	{ 0x09C2816, -0x001CCD4,  0x040, 0x7E }, { 0x09B41B0, -0x001C4F4,  0x03E, 0x7E },
	{ 0x09A5F38, -0x001BD4C,  0x03D, 0x7E }, { 0x0998096, -0x001B5D8,  0x03B, 0x7E },
	{ 0x098A5AD, -0x001AE9C,  0x039, 0x7E }, { 0x097CE60, -0x001A794,  0x038, 0x7E },
	{ 0x096FA9A, -0x001A0B8,  0x036, 0x7E }, { 0x0962A3F, -0x0019A0C,  0x035, 0x7E },
	{ 0x0955D3C, -0x0019390,  0x033, 0x7E }, { 0x0949375, -0x0018D3C,  0x032, 0x7E },
	{ 0x093CCD8, -0x0018714,  0x031, 0x7E }, { 0x093094F, -0x0018114,  0x030, 0x7E },
	{ 0x09248C7, -0x0017B3C,  0x02F, 0x7E }, { 0x0918B2B, -0x0017588,  0x02E, 0x7E },
	{ 0x090D06B, -0x0016FF8,  0x02C, 0x7E }, { 0x0901870, -0x0016A88,  0x02B, 0x7E },
	{ 0x08F632D, -0x0016540,  0x02A, 0x7E }, { 0x08EB08E, -0x0016014,  0x029, 0x7E },
	{ 0x08E0085, -0x0015B0C,  0x028, 0x7E }, { 0x08D5301, -0x0015620,  0x027, 0x7E },
	{ 0x08CA7F3, -0x0015150,  0x026, 0x7E }, { 0x08BFF4D, -0x0014C9C,  0x025, 0x7E },
	{ 0x08B58FE, -0x0014808,  0x025, 0x7E }, { 0x08AB4FE, -0x001438C,  0x023, 0x7E },
	{ 0x08A1337, -0x0013F28,  0x023, 0x7E }, { 0x08973A4, -0x0013AE0,  0x022, 0x7E },
	{ 0x088D635, -0x00136B0,  0x021, 0x7E }, { 0x0883ADC, -0x0013298,  0x021, 0x7E },
	{ 0x087A192, -0x0012E94,  0x020, 0x7E }, { 0x0870A49, -0x0012AA8,  0x01F, 0x7E },
	{ 0x08674F6, -0x00126D4,  0x01E, 0x7E }, { 0x085E18B, -0x0012310,  0x01E, 0x7E },
	{ 0x0855004, -0x0011F64,  0x01D, 0x7E }, { 0x084C051, -0x0011BCC,  0x01D, 0x7E },
	{ 0x084326D, -0x0011844,  0x01C, 0x7E }, { 0x083A64A, -0x00114D0,  0x01C, 0x7E },
	{ 0x0831BE4, -0x0011170,  0x01B, 0x7E }, { 0x082932E, -0x0010E20,  0x01A, 0x7E },
	{ 0x0820C1E, -0x0010AE0,  0x01A, 0x7E }, { 0x08186B0, -0x00107B0,  0x019, 0x7E },
	{ 0x08102D8, -0x0010490,  0x019, 0x7E }, { 0x0808091, -0x0010180,  0x018, 0x7E },
};

// sqrt(x) for x in [1, 4), indexed like rsqrt.
static constexpr VFPUSegment vfpu_sqrt_segments[128] = {
	{ 0x0800040,  0x001FE04, -0x020, 0x7F }, { 0x080FF40,  0x001FA1C, -0x01F, 0x7F },
	{ 0x081FC4E,  0x001F648, -0x01F, 0x7F }, { 0x082F770,  0x001F290, -0x01E, 0x7F },
	{ 0x083F0B5,  0x001EEE8, -0x01D, 0x7F }, { 0x084E828,  0x001EB54, -0x01D, 0x7F },
	{ 0x085DDD0,  0x001E7D4, -0x01C, 0x7F }, { 0x086D1BA,  0x001E468, -0x01C, 0x7F },
	{ 0x087C3EC,  0x001E110, -0x01B, 0x7F }, { 0x088B471,  0x001DDC8, -0x01A, 0x7F },
	{ 0x089A352,  0x001DA90, -0x019, 0x7F }, { 0x08A9099,  0x001D768, -0x019, 0x7F },
	{ 0x08B7C4A,  0x001D450, -0x018, 0x7F }, { 0x08C6671,  0x001D148, -0x018, 0x7F },
	{ 0x08D4F14,  0x001CE50, -0x018, 0x7F }, { 0x08E3639,  0x001CB64, -0x017, 0x7F },
	{ 0x08F1BEB,  0x001C888, -0x017, 0x7F }, { 0x090002C,  0x001C5B8, -0x016, 0x7F },
	{ 0x090E308,  0x001C2F4, -0x016, 0x7F }, { 0x091C482,  0x001C040, -0x016, 0x7F },
	{ 0x092A4A0,  0x001BD98, -0x015, 0x7F }, { 0x093836B,  0x001BAFC, -0x015, 0x7F },
	{ 0x09460E8,  0x001B868, -0x015, 0x7F }, { 0x0953D1A,  0x001B5E4, -0x014, 0x7F },
	{ 0x096180B,  0x001B368, -0x014, 0x7F }, { 0x096F1BC,  0x001B0F8, -0x013, 0x7F },
	{ 0x097CA37,  0x001AE94, -0x013, 0x7F }, { 0x098A180,  0x001AC34, -0x013, 0x7F },
	{ 0x0997798,  0x001A9E4, -0x012, 0x7F }, { 0x09A4C89,  0x001A79C, -0x012, 0x7F },
	{ 0x09B2056,  0x001A55C, -0x012, 0x7F }, { 0x09BF303,  0x001A324, -0x012, 0x7F },
	{ 0x09CC493,  0x001A0F8, -0x011, 0x7F }, { 0x09D950E,  0x0019ED4, -0x011, 0x7F },
	{ 0x09E6477,  0x0019CB4, -0x011, 0x7F }, { 0x09F32D1,  0x0019AA4, -0x011, 0x7F },
	{ 0x0A00022,  0x0019894, -0x011, 0x7F }, { 0x0A0CC6A,  0x0019690, -0x010, 0x7F },
	{ 0x0A197B2,  0x0019494, -0x010, 0x7F }, { 0x0A261FC,  0x00192A0, -0x010, 0x7F },
	{ 0x0A32B49,  0x00190B0, -0x00F, 0x7F }, { 0x0A3F3A1,  0x0018EC8, -0x00F, 0x7F },
	{ 0x0A4BB05,  0x0018CE8, -0x00F, 0x7F }, { 0x0A58178,  0x0018B10, -0x00F, 0x7F },
	{ 0x0A646FF,  0x001893C, -0x00F, 0x7F }, { 0x0A70B9B,  0x0018770, -0x00E, 0x7F },
	{ 0x0A7CF52,  0x00185A8, -0x00E, 0x7F }, { 0x0A89226,  0x00183E8, -0x00E, 0x7F },
	{ 0x0A95419,  0x0018230, -0x00E, 0x7F }, { 0x0AA1530,  0x0018078, -0x00E, 0x7F },
	{ 0x0AAD56A,  0x0017ECC, -0x00D, 0x7F }, { 0x0AB94CF,  0x0017D20, -0x00D, 0x7F },
	{ 0x0AC535F,  0x0017B80, -0x00D, 0x7F }, { 0x0AD111E,  0x00179E0, -0x00D, 0x7F },
	{ 0x0ADCE0D,  0x0017848, -0x00D, 0x7F }, { 0x0AE8A2E,  0x00176B4, -0x00C, 0x7F },
	{ 0x0AF4587,  0x0017524, -0x00C, 0x7F }, { 0x0B0001A,  0x0017398, -0x00D, 0x7F },
	{ 0x0B0B9E6,  0x0017214, -0x00D, 0x7F }, { 0x0B172EE,  0x0017094, -0x00C, 0x7F },
	{ 0x0B22B38,  0x0016F18, -0x00C, 0x7F }, { 0x0B2E2C4,  0x0016DA0, -0x00C, 0x7F },
	{ 0x0B39994,  0x0016C30, -0x00C, 0x7F }, { 0x0B44FAB,  0x0016AC0, -0x00C, 0x7F },
	{ 0x0B5054D,  0x002D148, -0x02D, 0x7F }, { 0x0B66DEE,  0x002CBC0, -0x02C, 0x7F },
	{ 0x0B7D3CB,  0x002C658, -0x02B, 0x7F }, { 0x0B936F5,  0x002C110, -0x02A, 0x7F },
	{ 0x0BA977A,  0x002BBE8, -0x029, 0x7F }, { 0x0BBF56B,  0x002B6D8, -0x028, 0x7F },
	{ 0x0BD50D5,  0x002B1E4, -0x027, 0x7F }, { 0x0BEA9C5,  0x002AD10, -0x026, 0x7F },
	{ 0x0C0004C,  0x002A850, -0x026, 0x7F }, { 0x0C15472,  0x002A3AC, -0x025, 0x7F },
	{ 0x0C2A646,  0x0029F20, -0x024, 0x7F }, { 0x0C3F5D5,  0x0029AA8, -0x024, 0x7F },
	{ 0x0C54327,  0x002964C, -0x023, 0x7F }, { 0x0C68E4A,  0x0029200, -0x022, 0x7F },
	{ 0x0C7D74A,  0x0028DCC, -0x022, 0x7F }, { 0x0C91E2E,  0x00289AC, -0x021, 0x7F },
	{ 0x0CA6302,  0x00285A0, -0x020, 0x7F }, { 0x0CBA5D0,  0x00281A8, -0x01F, 0x7F },
	{ 0x0CCE6A3,  0x0027DC0, -0x01F, 0x7F }, { 0x0CE2581,  0x00279EC, -0x01E, 0x7F },
	{ 0x0CF6276,  0x0027628, -0x01E, 0x7F }, { 0x0D09D88,  0x0027278, -0x01D, 0x7F },
	{ 0x0D1D6C3,  0x0026ED4, -0x01D, 0x7F }, { 0x0D30E2D,  0x0026B44, -0x01D, 0x7F },
	{ 0x0D443CD,  0x00267C0, -0x01C, 0x7F }, { 0x0D577AD,  0x002644C, -0x01C, 0x7F },
	{ 0x0D6A9D1,  0x00260EC, -0x01B, 0x7F }, { 0x0D7DA44,  0x0025D94, -0x01A, 0x7F },
	{ 0x0D9090D,  0x0025A4C, -0x01A, 0x7F }, { 0x0DA3632,  0x0025710, -0x01A, 0x7F },
	{ 0x0DB61B8,  0x00253E4, -0x019, 0x7F }, { 0x0DC8BA9,  0x00250C0, -0x019, 0x7F },
	{ 0x0DDB407,  0x0024DB0, -0x018, 0x7F }, { 0x0DEDADE,  0x0024AA4, -0x018, 0x7F },
	{ 0x0E00030,  0x00247A8, -0x018, 0x7F }, { 0x0E12404,  0x00244B8, -0x018, 0x7F },
	{ 0x0E2465E,  0x00241D4, -0x017, 0x7F }, { 0x0E36747,  0x0023EF8, -0x017, 0x7F },
	{ 0x0E486C3,  0x0023C28, -0x017, 0x7F }, { 0x0E5A4D5,  0x0023964, -0x016, 0x7F },
	{ 0x0E6C186,  0x00236A8, -0x016, 0x7F }, { 0x0E7DCD8,  0x00233F8, -0x015, 0x7F },
	{ 0x0E8F6D3,  0x0023150, -0x015, 0x7F }, { 0x0EA0F7A,  0x0022EB4, -0x015, 0x7F },
	{ 0x0EB26D3,  0x0022C1C, -0x015, 0x7F }, { 0x0EC3CE1,  0x0022990, -0x015, 0x7F },
	{ 0x0ED51A7,  0x0022710, -0x014, 0x7F }, { 0x0EE652F,  0x0022494, -0x014, 0x7F },
	{ 0x0EF7777,  0x0022224, -0x013, 0x7F }, { 0x0F08888,  0x0021FB8, -0x013, 0x7F },
	{ 0x0F19864,  0x0021D58, -0x013, 0x7F }, { 0x0F2A710,  0x0021B00, -0x013, 0x7F },
	{ 0x0F3B490,  0x00218AC, -0x013, 0x7F }, { 0x0F4C0E6,  0x0021664, -0x013, 0x7F },
	{ 0x0F5CC16,  0x0021424, -0x012, 0x7F }, { 0x0F6D627,  0x00211E8, -0x012, 0x7F },
	{ 0x0F7DF1A,  0x0020FB0, -0x012, 0x7F }, { 0x0F8E6F0,  0x0020D88, -0x011, 0x7F },
	{ 0x0F9EDB3,  0x0020B60, -0x011, 0x7F }, { 0x0FAF362,  0x0020940, -0x011, 0x7F },
	{ 0x0FBF801,  0x0020728, -0x011, 0x7F }, { 0x0FCFB94,  0x0020514, -0x011, 0x7F },
	{ 0x0FDFE1C,  0x0020308, -0x010, 0x7F }, { 0x0FEFF9F,  0x0020104, -0x010, 0x7F },
};

// exp2(x) for x in [0, 1).
static const VFPUSegment vfpu_exp2_segments[128] = {
	{ 0x07FFFE2,  0x00163DC,  0x00F, 0x7F }, { 0x080B1CF,  0x00165CC,  0x00F, 0x7F },
	{ 0x08164B4,  0x00167BC,  0x00F, 0x7F }, { 0x0821891,  0x00169B0,  0x00F, 0x7F },
	{ 0x082CD67,  0x0016BA4,  0x010, 0x7F }, { 0x0838339,  0x0016DA0,  0x010, 0x7F },
	{ 0x0843A0B,  0x0016F9C,  0x00F, 0x7F }, { 0x084F1D8,  0x001719C,  0x00F, 0x7F },
	{ 0x085AAA3,  0x001739C,  0x010, 0x7F }, { 0x0866471,  0x00175A4,  0x010, 0x7F },
	{ 0x0871F42,  0x00177A8,  0x010, 0x7F }, { 0x087DB15,  0x00179B4,  0x010, 0x7F },
	{ 0x08897EF,  0x0017BC0,  0x010, 0x7F }, { 0x08955CE,  0x0017DD0,  0x010, 0x7F },
	{ 0x08A14B5,  0x0017FE4,  0x010, 0x7F }, { 0x08AD4A6,  0x00181F8,  0x010, 0x7F },
	{ 0x08B95A0,  0x0018410,  0x011, 0x7F }, { 0x08C57AA,  0x001862C,  0x010, 0x7F },
	{ 0x08D1ABF,  0x001884C,  0x010, 0x7F }, { 0x08DDEE2,  0x0018A6C,  0x011, 0x7F },
	{ 0x08EA418,  0x0018C90,  0x011, 0x7F }, { 0x08F6A5F,  0x0018EB8,  0x011, 0x7F },
	{ 0x09031BA,  0x00190E4,  0x011, 0x7F }, { 0x090FA2B,  0x001930C,  0x011, 0x7F },
	{ 0x091C3B1,  0x0019540,  0x011, 0x7F }, { 0x0928E50,  0x0019774,  0x011, 0x7F },
	{ 0x0935A09,  0x00199A8,  0x011, 0x7F }, { 0x09426DD,  0x0019BE4,  0x011, 0x7F },
	{ 0x094F4CC,  0x0019E20,  0x012, 0x7F }, { 0x095C3DB,  0x001A05C,  0x012, 0x7F },
	{ 0x0969409,  0x001A2A0,  0x012, 0x7F }, { 0x0976559,  0x001A4E8,  0x012, 0x7F },
	{ 0x09837CC,  0x001A730,  0x012, 0x7F }, { 0x0990B64,  0x001A97C,  0x012, 0x7F },
	{ 0x099E022,  0x001ABCC,  0x012, 0x7F }, { 0x09AB607,  0x001AE20,  0x012, 0x7F },
	{ 0x09B8D16,  0x001B074,  0x012, 0x7F }, { 0x09C654F,  0x001B2D0,  0x012, 0x7F },
	{ 0x09D3EB4,  0x001B528,  0x013, 0x7F }, { 0x09E1948,  0x001B788,  0x013, 0x7F },
	{ 0x09EF50C,  0x001B9EC,  0x013, 0x7F }, { 0x09FD202,  0x001BC54,  0x013, 0x7F },
	{ 0x0A0B02B,  0x001BEBC,  0x013, 0x7F }, { 0x0A18F89,  0x001C128,  0x013, 0x7F },
	{ 0x0A2701D,  0x001C398,  0x013, 0x7F }, { 0x0A351E9,  0x001C610,  0x013, 0x7F },
	{ 0x0A434EE,  0x001C884,  0x014, 0x7F }, { 0x0A51930,  0x001CB00,  0x014, 0x7F },
	{ 0x0A5FEAF,  0x001CD7C,  0x014, 0x7F }, { 0x0A6E56D,  0x001D000,  0x014, 0x7F },
	{ 0x0A7CD6C,  0x001D284,  0x014, 0x7F }, { 0x0A8B6AD,  0x001D50C,  0x014, 0x7F },
	{ 0x0A9A133,  0x001D798,  0x014, 0x7F }, { 0x0AA8CFE,  0x001DA28,  0x014, 0x7F },
	{ 0x0AB7A12,  0x001DCBC,  0x014, 0x7F }, { 0x0AC686D,  0x001DF50,  0x015, 0x7F },
	{ 0x0AD5817,  0x001E1EC,  0x014, 0x7F }, { 0x0AE490C,  0x001E48C,  0x014, 0x7F },
	{ 0x0AF3B4F,  0x001E72C,  0x015, 0x7F }, { 0x0B02EE4,  0x001E9D0,  0x015, 0x7F },
	{ 0x0B123CC,  0x001EC78,  0x015, 0x7F }, { 0x0B21A08,  0x001EF24,  0x015, 0x7F },
	{ 0x0B3119A,  0x001F1D8,  0x015, 0x7F }, { 0x0B40A85,  0x001F488,  0x015, 0x7F },
	{ 0x0B504C9,  0x001F744,  0x015, 0x7F }, { 0x0B60068,  0x001F9FC,  0x016, 0x7F },
	{ 0x0B6FD66,  0x001FCBC,  0x016, 0x7F }, { 0x0B7FBC4,  0x001FF80,  0x016, 0x7F },
	{ 0x0B8FB83,  0x0020248,  0x016, 0x7F }, { 0x0B9FCA6,  0x0020514,  0x016, 0x7F },
	{ 0x0BAFF2F,  0x00207E0,  0x016, 0x7F }, { 0x0BC031E,  0x0020AB4,  0x016, 0x7F },
	{ 0x0BD0876,  0x0020D88,  0x017, 0x7F }, { 0x0BE0F3A,  0x0021064,  0x017, 0x7F },
	{ 0x0BF176C,  0x0021344,  0x017, 0x7F }, { 0x0C0210D,  0x0021624,  0x017, 0x7F },
	{ 0x0C12C1F,  0x002190C,  0x017, 0x7F }, { 0x0C238A4,  0x0021BF8,  0x017, 0x7F },
	{ 0x0C3469F,  0x0021EE4,  0x017, 0x7F }, { 0x0C45611,  0x00221D8,  0x017, 0x7F },
	{ 0x0C566FC,  0x00224D0,  0x017, 0x7F }, { 0x0C67961,  0x00227C8,  0x018, 0x7F },
	{ 0x0C78D47,  0x0022AC8,  0x017, 0x7F }, { 0x0C8A2A8,  0x0022DCC,  0x018, 0x7F },
	{ 0x0C9B98E,  0x00230D0,  0x018, 0x7F }, { 0x0CAD1F6,  0x00233E0,  0x018, 0x7F },
	{ 0x0CBEBE5,  0x00236F0,  0x018, 0x7F }, { 0x0CD075A,  0x0023A00,  0x019, 0x7F },
	{ 0x0CE245C,  0x0023D1C,  0x018, 0x7F }, { 0x0CF42E7,  0x0024038,  0x019, 0x7F },
	{ 0x0D06302,  0x0024358,  0x019, 0x7F }, { 0x0D184AD,  0x0024680,  0x019, 0x7F },
	{ 0x0D2A7EC,  0x00249A8,  0x019, 0x7F }, { 0x0D3CCBF,  0x0024CD4,  0x019, 0x7F },
	{ 0x0D4F329,  0x0025008,  0x019, 0x7F }, { 0x0D61B2C,  0x0025340,  0x019, 0x7F },
	{ 0x0D744C9,  0x0025678,  0x01A, 0x7F }, { 0x0D87005,  0x00259BC,  0x01A, 0x7F },
	{ 0x0D99CE2,  0x0025CFC,  0x01A, 0x7F }, { 0x0DACB60,  0x0026048,  0x01A, 0x7F },
	{ 0x0DBFB84,  0x0026394,  0x01A, 0x7F }, { 0x0DD2D4E,  0x00266E8,  0x01A, 0x7F },
	{ 0x0DE60BF,  0x0026A3C,  0x01B, 0x7F }, { 0x0DF95DD,  0x0026D98,  0x01B, 0x7F },
	{ 0x0E0CCA9,  0x00270F8,  0x01B, 0x7F }, { 0x0E20525,  0x002745C,  0x01B, 0x7F },
	{ 0x0E33F53,  0x00277C8,  0x01B, 0x7F }, { 0x0E47B37,  0x0027B34,  0x01B, 0x7F },
	{ 0x0E5B8CF,  0x0027EA8,  0x01C, 0x7F }, { 0x0E6F825,  0x0028220,  0x01B, 0x7F },
	{ 0x0E83932,  0x002859C,  0x01C, 0x7F }, { 0x0E97C00,  0x0028920,  0x01C, 0x7F },
	{ 0x0EAC08F,  0x0028CA4,  0x01C, 0x7F }, { 0x0EC06E1,  0x0029030,  0x01C, 0x7F },
	{ 0x0ED4EF8,  0x00293C0,  0x01C, 0x7F }, { 0x0EE98D6,  0x0029754,  0x01D, 0x7F },
	{ 0x0EFE480,  0x0029AF0,  0x01D, 0x7F }, { 0x0F131F7,  0x0029E8C,  0x01D, 0x7F },
	{ 0x0F2813D,  0x002A234,  0x01D, 0x7F }, { 0x0F3D256,  0x002A5DC,  0x01D, 0x7F },
	{ 0x0F52543,  0x002A988,  0x01D, 0x7F }, { 0x0F67A07,  0x002AD3C,  0x01D, 0x7F },
	{ 0x0F7D0A5,  0x002B0F8,  0x01D, 0x7F }, { 0x0F9291E,  0x002B4B4,  0x01E, 0x7F },
	{ 0x0FA8377,  0x002B874,  0x01E, 0x7F }, { 0x0FBDFB1,  0x002BC40,  0x01E, 0x7F },
	{ 0x0FD3DD0,  0x002C00C,  0x01E, 0x7F }, { 0x0FE9DD5,  0x002C3E0,  0x01E, 0x7F },
};

// log2(x) for x in [1, 2), in ulps of 2^-24. e is unused, see vfpu_log2.
static const VFPUSegment vfpu_log2_segments[128] = {
	{ 0x00000B6,  0x005BF94, -0x05B, 0x00 }, { 0x002E07E,  0x005B434, -0x05A, 0x00 },
	{ 0x005BA95,  0x005A904, -0x058, 0x00 }, { 0x0088F16,  0x0059E00, -0x057, 0x00 },
	{ 0x00B5E15,  0x0059324, -0x056, 0x00 }, { 0x00E27A5,  0x0058874, -0x054, 0x00 },
	{ 0x010EBDE,  0x0057DEC, -0x053, 0x00 }, { 0x013AAD3,  0x005738C, -0x052, 0x00 },
	{ 0x0166499,  0x0056954, -0x051, 0x00 }, { 0x0191941,  0x0055F40, -0x050, 0x00 },
	{ 0x01BC8DE,  0x0055550, -0x04E, 0x00 }, { 0x01E7386,  0x0054B88, -0x04D, 0x00 },
	{ 0x0211949,  0x00541E4, -0x04C, 0x00 }, { 0x023BA39,  0x0053860, -0x04B, 0x00 },
	{ 0x0265667,  0x0052F00, -0x04A, 0x00 }, { 0x028EDE6,  0x00525C0, -0x049, 0x00 },
	{ 0x02B80C4,  0x0051CA0, -0x048, 0x00 }, { 0x02E0F13,  0x00513A4, -0x047, 0x00 },
	{ 0x03098E3,  0x0050AC4, -0x046, 0x00 }, { 0x0331E44,  0x0050204, -0x045, 0x00 },
	{ 0x0359F44,  0x004F960, -0x044, 0x00 }, { 0x0381BF3,  0x004F0DC, -0x043, 0x00 },
	{ 0x03A9460,  0x004E874, -0x042, 0x00 }, { 0x03D0899,  0x004E028, -0x041, 0x00 },
	{ 0x03F78AF,  0x004D7FC, -0x041, 0x00 }, { 0x041E4AB,  0x004CFE8, -0x040, 0x00 },
	{ 0x0444C9D,  0x004C7EC, -0x03F, 0x00 }, { 0x046B093,  0x004C00C, -0x03E, 0x00 },
	{ 0x0491098,  0x004B848, -0x03D, 0x00 }, { 0x04B6CBE,  0x004B09C, -0x03D, 0x00 },
	{ 0x04DC50B,  0x004A90C, -0x03C, 0x00 }, { 0x050198F,  0x004A190, -0x03B, 0x00 },
	{ 0x0526A55,  0x0049A2C, -0x03A, 0x00 }, { 0x054B76C,  0x00492E0, -0x03A, 0x00 },
	{ 0x05700DB,  0x0048BAC, -0x039, 0x00 }, { 0x05946AF,  0x004848C, -0x038, 0x00 },
	{ 0x05B88F7,  0x0047D88, -0x038, 0x00 }, { 0x05DC7B9,  0x0047694, -0x037, 0x00 },
	{ 0x0600301,  0x0046FB8, -0x036, 0x00 }, { 0x0623ADE,  0x00468F0, -0x036, 0x00 },
	{ 0x0646F54,  0x004623C, -0x035, 0x00 }, { 0x066A071,  0x0045B9C, -0x034, 0x00 },
	{ 0x068CE40,  0x0045514, -0x034, 0x00 }, { 0x06AF8C8,  0x0044E9C, -0x033, 0x00 },
	{ 0x06D2014,  0x0044834, -0x032, 0x00 }, { 0x06F442F,  0x00441E4, -0x032, 0x00 },
	{ 0x0716520,  0x0043BA8, -0x031, 0x00 }, { 0x07382F4,  0x0043578, -0x031, 0x00 },
	{ 0x0759DAF,  0x0042F60, -0x030, 0x00 }, { 0x077B55F,  0x0042954, -0x030, 0x00 },
	{ 0x079CA08,  0x004235C, -0x02F, 0x00 }, { 0x07BDBB6,  0x0041D74, -0x02E, 0x00 },
	{ 0x07DEA71,  0x00417A0, -0x02E, 0x00 }, { 0x07FF640,  0x00411D8, -0x02D, 0x00 },
	{ 0x081FF2E,  0x0040C28, -0x02D, 0x00 }, { 0x0840542,  0x0040680, -0x02D, 0x00 },
	{ 0x0860880,  0x00400EC, -0x02C, 0x00 }, { 0x08808F6,  0x003FB64, -0x02C, 0x00 },
	{ 0x08A06A6,  0x003F5EC, -0x02B, 0x00 }, { 0x08C019C,  0x003F084, -0x02B, 0x00 },
	{ 0x08DF9DD,  0x003EB2C, -0x02A, 0x00 }, { 0x08FEF73,  0x003E5E0, -0x02A, 0x00 },
	{ 0x091E261,  0x003E0A0, -0x029, 0x00 }, { 0x093D2B2,  0x003DB74, -0x029, 0x00 },
	{ 0x095C06C,  0x003D654, -0x029, 0x00 }, { 0x097AB94,  0x003D13C, -0x028, 0x00 },
	{ 0x0999433,  0x003CC38, -0x028, 0x00 }, { 0x09B7A4E,  0x003C740, -0x027, 0x00 },
	{ 0x09D5DEE,  0x003C254, -0x027, 0x00 }, { 0x09F3F16,  0x003BD74, -0x026, 0x00 },
	{ 0x0A11DD0,  0x003B8A0, -0x026, 0x00 }, { 0x0A2FA21,  0x003B3D8, -0x026, 0x00 },
	{ 0x0A4D40C,  0x003AF20, -0x025, 0x00 }, { 0x0A6AB9D,  0x003AA70, -0x025, 0x00 },
	{ 0x0A880D6,  0x003A5D0, -0x025, 0x00 }, { 0x0AA53BC,  0x003A138, -0x024, 0x00 },
	{ 0x0AC2459,  0x0039CAC, -0x024, 0x00 }, { 0x0ADF2B0,  0x0039830, -0x024, 0x00 },
	{ 0x0AFBEC6,  0x00393B8, -0x023, 0x00 }, { 0x0B188A2,  0x0038F50, -0x023, 0x00 },
	{ 0x0B35048,  0x0038AF0, -0x022, 0x00 }, { 0x0B515C1,  0x003869C, -0x022, 0x00 },
	{ 0x0B6D90F,  0x0038254, -0x022, 0x00 }, { 0x0B89A37,  0x0037E14, -0x021, 0x00 },
	{ 0x0BA5941,  0x00379DC, -0x021, 0x00 }, { 0x0BC1630,  0x00375B4, -0x021, 0x00 },
	{ 0x0BDD108,  0x0037190, -0x020, 0x00 }, { 0x0BF89D1,  0x0036D7C, -0x020, 0x00 },
	{ 0x0C1408F,  0x003696C, -0x020, 0x00 }, { 0x0C2F546,  0x0036568, -0x020, 0x00 },
	{ 0x0C4A7F8,  0x0036170, -0x01F, 0x00 }, { 0x0C658B0,  0x0035D7C, -0x01F, 0x00 },
	{ 0x0C8076F,  0x0035994, -0x01F, 0x00 }, { 0x0C9B439,  0x00355B8, -0x01F, 0x00 },
	{ 0x0CB5F13,  0x00351E0, -0x01E, 0x00 }, { 0x0CD0803,  0x0034E10, -0x01E, 0x00 },
	{ 0x0CEAF0A,  0x0034A4C, -0x01D, 0x00 }, { 0x0D05431,  0x0034690, -0x01D, 0x00 },
	{ 0x0D1F77A,  0x00342DC, -0x01D, 0x00 }, { 0x0D398E9,  0x0033F30, -0x01D, 0x00 },
	{ 0x0D53880,  0x0033B8C, -0x01C, 0x00 }, { 0x0D6D649,  0x00337F4, -0x01D, 0x00 },
	{ 0x0D87241,  0x0033460, -0x01C, 0x00 }, { 0x0DA0C72,  0x00330D4, -0x01C, 0x00 },
	{ 0x0DBA4DC,  0x0032D54, -0x01C, 0x00 }, { 0x0DD3B84,  0x00329D4, -0x01B, 0x00 },
	{ 0x0DED06F,  0x0032660, -0x01B, 0x00 }, { 0x0E063A0,  0x00322F4, -0x01B, 0x00 },
	{ 0x0E1F51B,  0x0031F90, -0x01B, 0x00 }, { 0x0E384E3,  0x0031C34, -0x01B, 0x00 },
	{ 0x0E512FB,  0x00318DC, -0x01A, 0x00 }, { 0x0E69F69,  0x003158C, -0x01A, 0x00 },
	{ 0x0E82A2F,  0x0031244, -0x01A, 0x00 }, { 0x0E9B350,  0x0030F00, -0x019, 0x00 },
	{ 0x0EB3AD3,  0x0030BC8, -0x01A, 0x00 }, { 0x0ECC0B5,  0x0030894, -0x019, 0x00 },
	{ 0x0EE44FF,  0x0030568, -0x019, 0x00 }, { 0x0EFC7B3,  0x0030240, -0x019, 0x00 },
	{ 0x0F148D1,  0x002FF20, -0x018, 0x00 }, { 0x0F2C862,  0x002FC08, -0x018, 0x00 },
	{ 0x0F44666,  0x002F8F4, -0x018, 0x00 }, { 0x0F5C2E0,  0x002F5E8, -0x018, 0x00 },
	{ 0x0F73DD4,  0x002F2E0, -0x018, 0x00 }, { 0x0F8B744,  0x002EFE0, -0x018, 0x00 },
	{ 0x0FA2F32,  0x002ECE8, -0x017, 0x00 }, { 0x0FBA5A6,  0x002E9F0, -0x017, 0x00 },
	{ 0x0FD1A9F,  0x002E700, -0x017, 0x00 }, { 0x0FE8E20,  0x002E41C, -0x017, 0x00 },
};

// sin(pi/2 * (1 - y)) for y in [0, 1): the hardware indexes the quarter wave from the top.
static const VFPUSegment vfpu_sin_segments[128] = {
	{ 0x100013C, -0x00009DC, -0x09E, 0x7E }, { 0x0FFFC4D, -0x0001D9C, -0x09E, 0x7E },
	{ 0x0FFED7F, -0x0003158, -0x09E, 0x7E }, { 0x0FFD4D3, -0x0004510, -0x09E, 0x7E },
	{ 0x0FFB24B, -0x00058C8, -0x09E, 0x7E }, { 0x0FF85E7, -0x0006C7C, -0x09E, 0x7E },
	{ 0x0FF4FA7, -0x0008028, -0x09D, 0x7E }, { 0x0FF0F92, -0x00093D4, -0x09D, 0x7E },
	{ 0x0FEC5A7, -0x000A778, -0x09D, 0x7E }, { 0x0FE71EA, -0x000BB18, -0x09D, 0x7E },
	{ 0x0FE145E, -0x000CEB0, -0x09D, 0x7E }, { 0x0FDAD04, -0x000E240, -0x09C, 0x7E },
	{ 0x0FD3BE4, -0x000F5C4, -0x09C, 0x7E }, { 0x0FCC101, -0x0010940, -0x09C, 0x7E },
	{ 0x0FC3C5E, -0x0011CB4, -0x09B, 0x7E }, { 0x0FBAE03, -0x001301C, -0x09B, 0x7E },
	{ 0x0FB15F4, -0x0014378, -0x09B, 0x7E }, { 0x0FA7436, -0x00156C8, -0x09A, 0x7E },
	{ 0x0F9C8D1, -0x0016A08, -0x09A, 0x7E }, { 0x0F913CC, -0x0017D40, -0x09A, 0x7E },
	{ 0x0F8552A, -0x0019064, -0x099, 0x7E }, { 0x0F78CF7, -0x001A37C, -0x099, 0x7E },
	{ 0x0F6BB37, -0x001B680, -0x098, 0x7E }, { 0x0F5DFF4, -0x001C974, -0x097, 0x7E },
	{ 0x0F4FB39, -0x001DC5C, -0x097, 0x7E }, { 0x0F40D0B, -0x001EF2C, -0x097, 0x7E },
	{ 0x0F31573, -0x00201E8, -0x096, 0x7E }, { 0x0F2147C, -0x0021494, -0x095, 0x7E },
	{ 0x0F10A30, -0x0022728, -0x094, 0x7E }, { 0x0EFF69B, -0x00239A8, -0x094, 0x7E },
	{ 0x0EED9C4, -0x0024C14, -0x093, 0x7E }, { 0x0EDB3B7, -0x0025E68, -0x092, 0x7E },
	{ 0x0EC8482, -0x00270A4, -0x092, 0x7E }, { 0x0EB4C2E, -0x00282C8, -0x091, 0x7E },
	{ 0x0EA0AC7, -0x00294D4, -0x090, 0x7E }, { 0x0E8C05A, -0x002A6C8, -0x08F, 0x7E },
	{ 0x0E76CF4, -0x002B8A0, -0x08E, 0x7E }, { 0x0E610A2, -0x002CA5C, -0x08D, 0x7E },
	{ 0x0E4AB73, -0x002DBFC, -0x08D, 0x7E }, { 0x0E33D72, -0x002ED84, -0x08C, 0x7E },
	{ 0x0E1C6AE, -0x002FEEC, -0x08B, 0x7E }, { 0x0E04735, -0x0031038, -0x08A, 0x7E },
	{ 0x0DEBF17, -0x0032164, -0x089, 0x7E }, { 0x0DD2E63, -0x0033270, -0x088, 0x7E },
	{ 0x0DB9528, -0x0034360, -0x087, 0x7E }, { 0x0D9F376, -0x003542C, -0x086, 0x7E },
	{ 0x0D8495D, -0x00364DC, -0x085, 0x7E }, { 0x0D696ED, -0x0037568, -0x084, 0x7E },
	{ 0x0D4DC37, -0x00385D0, -0x083, 0x7E }, { 0x0D3194D, -0x0039618, -0x082, 0x7E },
	{ 0x0D14E3F, -0x003A63C, -0x081, 0x7E }, { 0x0CF7B1F, -0x003B638, -0x080, 0x7E },
	{ 0x0CD9FFE, -0x003C614, -0x07E, 0x7E }, { 0x0CBBCF2, -0x003D5CC, -0x07D, 0x7E },
	{ 0x0C9D20A, -0x003E558, -0x07C, 0x7E }, { 0x0C7DF5B, -0x003F4C4, -0x07B, 0x7E },
	{ 0x0C5E4F7, -0x0040404, -0x07A, 0x7E }, { 0x0C3E2F2, -0x0041320, -0x079, 0x7E },
	{ 0x0C1D95E, -0x0042210, -0x077, 0x7E }, { 0x0BFC853, -0x00430DC, -0x076, 0x7E },
	{ 0x0BDAFE3, -0x0043F7C, -0x075, 0x7E }, { 0x0BB9021, -0x0044DF0, -0x073, 0x7E },
	{ 0x0B96926, -0x0045C3C, -0x072, 0x7E }, { 0x0B73B05, -0x0046A60, -0x071, 0x7E },
	{ 0x0B505D1, -0x0047854, -0x06F, 0x7E }, { 0x0B2C9A5, -0x004861C, -0x06E, 0x7E },
	{ 0x0B08693, -0x00493B8, -0x06C, 0x7E }, { 0x0AE3CB4, -0x004A128, -0x06B, 0x7E },
	{ 0x0ABEC1C, -0x004AE68, -0x069, 0x7E }, { 0x0A994E5, -0x004BB7C, -0x068, 0x7E },
	{ 0x0A73722, -0x004C860, -0x066, 0x7E }, { 0x0A4D2EF, -0x004D518, -0x065, 0x7E },
	{ 0x0A26861, -0x004E19C, -0x064, 0x7E }, { 0x09FF78F, -0x004EDF4, -0x062, 0x7E },
	{ 0x09D8091, -0x004FA14, -0x060, 0x7E }, { 0x09B0384, -0x005060C, -0x05F, 0x7E },
	{ 0x098807A, -0x00511CC, -0x05D, 0x7E }, { 0x095F791, -0x0051D5C, -0x05C, 0x7E },
	{ 0x09368DE, -0x00528B8, -0x05A, 0x7E }, { 0x090D47D, -0x00533E4, -0x058, 0x7E },
	{ 0x08E3A88, -0x0053EDC, -0x057, 0x7E }, { 0x08B9B15, -0x00549A0, -0x055, 0x7E },
	{ 0x088F643, -0x0055430, -0x054, 0x7E }, { 0x0864C26, -0x0055E88, -0x052, 0x7E },
	{ 0x0839CDD, -0x00568B0, -0x050, 0x7E }, { 0x080E882, -0x00572A0, -0x04F, 0x7E },
	{ 0x0FC5E5B, -0x00AF8B8, -0x09A, 0x7D }, { 0x0F6E1F7, -0x00B0BC0, -0x096, 0x7D },
	{ 0x0F15C10, -0x00B1E5C, -0x093, 0x7D }, { 0x0EBCCDB, -0x00B308C, -0x090, 0x7D },
	{ 0x0E6348D, -0x00B424C, -0x08C, 0x7D }, { 0x0E09361, -0x00B539C, -0x089, 0x7D },
	{ 0x0DAE98A, -0x00B647C, -0x085, 0x7D }, { 0x0D53745, -0x00B74EC, -0x082, 0x7D },
	{ 0x0CF7CC6, -0x00B84EC, -0x07E, 0x7D }, { 0x0C9BA49, -0x00B947C, -0x07B, 0x7D },
	{ 0x0C3F003, -0x00BA394, -0x077, 0x7D }, { 0x0BE1E30, -0x00BB240, -0x073, 0x7D },
	{ 0x0B8450A, -0x00BC074, -0x070, 0x7D }, { 0x0B264C7, -0x00BCE34, -0x06C, 0x7D },
	{ 0x0AC7DA6, -0x00BDB84, -0x069, 0x7D }, { 0x0A68FDC, -0x00BE858, -0x065, 0x7D },
	{ 0x0A09BA7, -0x00BF4BC, -0x061, 0x7D }, { 0x09AA142, -0x00C00A8, -0x05E, 0x7D },
	{ 0x094A0E5, -0x00C0C1C, -0x05A, 0x7D }, { 0x08E9ACE, -0x00C171C, -0x056, 0x7D },
	{ 0x0888F37, -0x00C21A4, -0x052, 0x7D }, { 0x0827E5E, -0x00C2BB4, -0x04F, 0x7D },
	{ 0x0F8D0F8, -0x0186A98, -0x096, 0x7C }, { 0x0EC9B9D, -0x0187CD8, -0x08F, 0x7C },
	{ 0x0E05D21, -0x0188E24, -0x087, 0x7C }, { 0x0D415FF, -0x0189E7C, -0x07F, 0x7C },
	{ 0x0C7C6B0, -0x018ADE4, -0x077, 0x7C }, { 0x0BB6FAF, -0x018BC58, -0x070, 0x7C },
	{ 0x0AF1172, -0x018C9D8, -0x068, 0x7C }, { 0x0A2AC78, -0x018D664, -0x061, 0x7C },
	{ 0x0964135, -0x018E1F8, -0x059, 0x7C }, { 0x089D028, -0x018EC98, -0x051, 0x7C },
	{ 0x0FAB399, -0x031EC88, -0x093, 0x7B }, { 0x0E1BD36, -0x031FDF0, -0x084, 0x7B },
	{ 0x0C8BE1E, -0x0320D6C, -0x074, 0x7B }, { 0x0AFB74A, -0x0321AF4, -0x065, 0x7B },
	{ 0x096A9AF, -0x0322694, -0x055, 0x7B }, { 0x0FB2C8C, -0x0646088, -0x08C, 0x7A },
	{ 0x0C8FC0A, -0x0647008, -0x06D, 0x7A }, { 0x096C3C8, -0x0647BA8, -0x04E, 0x7A },
	{ 0x0C90B6A, -0x0C906D0, -0x05D, 0x79 }, { 0x0C90F0C, -0x1921D20, -0x03E, 0x78 },
};

// asin(x) * 2/pi for x in [0, 1).
static const VFPUSegment vfpu_asin_segments[128] = {
	{ 0x0000000,  0x145F3E0,  0x000, 0x77 }, { 0x0A2F978,  0x145F8F8,  0x03C, 0x77 },
	{ 0x0A2FAD2,  0x0A30194,  0x032, 0x78 }, { 0x0F47B72,  0x0A3093C,  0x047, 0x78 },
	{ 0x0A2FFF5,  0x05189B4,  0x02D, 0x79 }, { 0x0CBC4BC,  0x0519018,  0x037, 0x79 },
	{ 0x0F48CB2,  0x05197C4,  0x042, 0x79 }, { 0x08EAC42,  0x028D058,  0x025, 0x7A },
	{ 0x0A31463,  0x028D57C,  0x02B, 0x7A }, { 0x0B77F17,  0x028DB40,  0x030, 0x7A },
	{ 0x0CBECAD,  0x028E1AC,  0x035, 0x7A }, { 0x0E05D7A,  0x028E8C0,  0x03A, 0x7A },
	{ 0x0F4D1D1,  0x028F07C,  0x03F, 0x7A }, { 0x084A503,  0x0147C74,  0x022, 0x7B },
	{ 0x08EE339,  0x01480FC,  0x024, 0x7B }, { 0x09923B2,  0x01485DC,  0x027, 0x7B },
	{ 0x0A3669B,  0x0148B14,  0x02A, 0x7B }, { 0x0ADAC20,  0x01490A0,  0x02D, 0x7B },
	{ 0x0B7F46B,  0x014968C,  0x030, 0x7B }, { 0x0C23FAB,  0x0149CCC,  0x033, 0x7B },
	{ 0x0CC8E0B,  0x014A368,  0x036, 0x7B }, { 0x0D6DFBB,  0x014AA5C,  0x038, 0x7B },
	{ 0x0E134E4,  0x014B1B0,  0x03B, 0x7B }, { 0x0EB8DB6,  0x014B960,  0x03E, 0x7B },
	{ 0x0F5EA61,  0x014C170,  0x041, 0x7B }, { 0x080258A,  0x00A64F0,  0x022, 0x7C },
	{ 0x0855801,  0x00A6958,  0x023, 0x7C }, { 0x08A8CA9,  0x00A6DF4,  0x025, 0x7C },
	{ 0x08FC39F,  0x00A72C0,  0x027, 0x7C }, { 0x094FCFD,  0x00A77BC,  0x028, 0x7C },
	{ 0x09A38D8,  0x00A7CF0,  0x02A, 0x7C }, { 0x09F774E,  0x00A8258,  0x02B, 0x7C },
	{ 0x0A4B876,  0x00A87F0,  0x02D, 0x7C }, { 0x0A9FC6B,  0x00A8DC8,  0x02F, 0x7C },
	{ 0x0AF434D,  0x00A93D0,  0x030, 0x7C }, { 0x0B48D31,  0x00A9A14,  0x032, 0x7C },
	{ 0x0B9DA37,  0x00AA08C,  0x034, 0x7C }, { 0x0BF2A7A,  0x00AA744,  0x036, 0x7C },
	{ 0x0C47E19,  0x00AAE38,  0x038, 0x7C }, { 0x0C9D531,  0x00AB564,  0x03A, 0x7C },
	{ 0x0CF2FE0,  0x00ABCD4,  0x03C, 0x7C }, { 0x0D48E48,  0x00AC480,  0x03D, 0x7C },
	{ 0x0D9F083,  0x00ACC70,  0x040, 0x7C }, { 0x0DF56B7,  0x00AD4A0,  0x042, 0x7C },
	{ 0x0E4C103,  0x00ADD14,  0x044, 0x7C }, { 0x0EA2F89,  0x00AE5D0,  0x046, 0x7C },
	{ 0x0EFA26D,  0x00AEED4,  0x048, 0x7C }, { 0x0F519D1,  0x00AF81C,  0x04B, 0x7C },
	{ 0x0FA95DC,  0x00B01B4,  0x04D, 0x7C }, { 0x0800B5A,  0x00585CC,  0x027, 0x7D },
	{ 0x082CE3F,  0x0058AE4,  0x028, 0x7D }, { 0x08593AE,  0x0059028,  0x02A, 0x7D },
	{ 0x0885BC1,  0x0059594,  0x02B, 0x7D }, { 0x08B2688,  0x0059B2C,  0x02D, 0x7D },
	{ 0x08DF41C,  0x005A0F0,  0x02E, 0x7D }, { 0x090C493,  0x005A6E4,  0x02F, 0x7D },
	{ 0x0939801,  0x005AD04,  0x031, 0x7D }, { 0x0966E82,  0x005B354,  0x032, 0x7D },
	{ 0x0994829,  0x005B9D8,  0x034, 0x7D }, { 0x09C2514,  0x005C090,  0x035, 0x7D },
	{ 0x09F0559,  0x005C780,  0x037, 0x7D }, { 0x0A1E915,  0x005CEA4,  0x039, 0x7D },
	{ 0x0A4D064,  0x005D604,  0x03B, 0x7D }, { 0x0A7BB62,  0x005DD9C,  0x03D, 0x7D },
	{ 0x0AAAA2D,  0x005E574,  0x03F, 0x7D }, { 0x0AD9CE3,  0x005ED8C,  0x041, 0x7D },
	{ 0x0B093A5,  0x005F5E8,  0x043, 0x7D }, { 0x0B38E95,  0x005FE84,  0x045, 0x7D },
	{ 0x0B68DD1,  0x006076C,  0x048, 0x7D }, { 0x0B99183,  0x0061098,  0x04A, 0x7D },
	{ 0x0BC99CC,  0x0061A1C,  0x04C, 0x7D }, { 0x0BFA6D4,  0x00623E8,  0x04F, 0x7D },
	{ 0x0C2B8C4,  0x0062E0C,  0x051, 0x7D }, { 0x0C5CFC3,  0x0063888,  0x055, 0x7D },
	{ 0x0C8EC01,  0x0064360,  0x058, 0x7D }, { 0x0CC0DAD,  0x0064E94,  0x05A, 0x7D },
	{ 0x0CF34F0,  0x0065A34,  0x05E, 0x7D }, { 0x0D26204,  0x0066638,  0x061, 0x7D },
	{ 0x0D59519,  0x00672AC,  0x065, 0x7D }, { 0x0D8CE6A,  0x0067F94,  0x068, 0x7D },
	{ 0x0DC0E2D,  0x0068CF8,  0x06C, 0x7D }, { 0x0DF54A1,  0x0069ADC,  0x070, 0x7D },
	{ 0x0E2A205,  0x006A944,  0x075, 0x7D }, { 0x0E5F6A0,  0x006B840,  0x079, 0x7D },
	{ 0x0E952B6,  0x006C7D0,  0x07E, 0x7D }, { 0x0ECB694,  0x006D7FC,  0x083, 0x7D },
	{ 0x0F02287,  0x006E8D8,  0x089, 0x7D }, { 0x0F396E9,  0x006FA64,  0x08E, 0x7D },
	{ 0x0F7140F,  0x0070CAC,  0x094, 0x7D }, { 0x0FA9A58,  0x0071FC4,  0x09B, 0x7D },
	{ 0x0FE2A2C,  0x00733B0,  0x0A2, 0x7D }, { 0x080E1FC,  0x003A440,  0x054, 0x7E },
	{ 0x082B415,  0x003AF24,  0x058, 0x7E }, { 0x0848BA0,  0x003BA90,  0x05C, 0x7E },
	{ 0x08668DE,  0x003C688,  0x061, 0x7E }, { 0x0884C18,  0x003D314,  0x066, 0x7E },
	{ 0x08A3599,  0x003E048,  0x06B, 0x7E }, { 0x08C25B2,  0x003EE30,  0x071, 0x7E },
	{ 0x08E1CBD,  0x003FCD8,  0x078, 0x7E }, { 0x0901B1D,  0x0040C50,  0x07E, 0x7E },
	{ 0x0922135,  0x0041CB0,  0x086, 0x7E }, { 0x0942F7D,  0x0042E04,  0x08E, 0x7E },
	{ 0x096466E,  0x0044070,  0x097, 0x7E }, { 0x0986692,  0x0045408,  0x0A1, 0x7E },
	{ 0x09A9080,  0x00468F0,  0x0AC, 0x7E }, { 0x09CC4E0,  0x0047F44,  0x0B8, 0x7E },
	{ 0x09F0469,  0x004973C,  0x0C5, 0x7E }, { 0x0A14FE8,  0x004B104,  0x0D5, 0x7E },
	{ 0x0A3A848,  0x004CCD4,  0x0E6, 0x7E }, { 0x0A60E8A,  0x004EAF4,  0x0FA, 0x7E },
	{ 0x0A883D6,  0x0050BB8,  0x111, 0x7E }, { 0x0AB097F,  0x0052F84,  0x12B, 0x7E },
	{ 0x0ADA106,  0x00556D8,  0x149, 0x7E }, { 0x0B04C2B,  0x005824C,  0x16D, 0x7E },
	{ 0x0B30CFB,  0x005B29C,  0x198, 0x7E }, { 0x0B5E5E5,  0x005E8C0,  0x1CA, 0x7E },
	{ 0x0B8E1C8,  0x00625FC, -0x1F7, 0x7E }, { 0x0BBF42A,  0x0066BF4, -0x1A9, 0x7E },
	{ 0x0BF295F,  0x006BCE8, -0x146, 0x7E }, { 0x0C286D1,  0x0071C04, -0x0C5, 0x7E },
	{ 0x0C6137A,  0x0078DE4, -0x018, 0x7E }, { 0x0C9D887,  0x008198C,  0x0DB, 0x7E },
	{ 0x0CDEA7F,  0x008CA58, -0x1BE, 0x7E }, { 0x0D24B35,  0x009B428,  0x07D, 0x7E },
	{ 0x0D72566,  0x00AFE0C,  0x06F, 0x7E }, { 0x0DCA3BF,  0x00D035C,  0x0C6, 0x7E },
	{ 0x0E32341,  0x010ECE0,  0x1DC, 0x7E }, { 0x0EB9DD1,  0x028C52C, -0x034, 0x7E },
};

// The squarer only sees the top 10 bits of x2, as a distance from the middle of the segment.
static inline int32_t vfpu_square(uint32_t x2) {
	const int32_t t = abs(int32_t(x2 >> 6) - 512);
	return (t * t + 255) >> 8;
}

// The untruncated result, in ulps of 2^(seg.e - 150).
static inline int32_t vfpu_interp(const VFPUSegment &seg, uint32_t x2) {
	return seg.c0 + int32_t(((int64_t)seg.m * x2) >> 17) + ((seg.n * vfpu_square(x2)) >> 9);
}

// Float bits of a result in [2^(e-127), 2^(e-126)], truncated to 22 bits. The top of that range
// carries into the exponent, which gives exactly the next power of two.
static inline uint32_t vfpu_interp_bits(const VFPUSegment *segments, uint32_t index) {
	const VFPUSegment &seg = segments[index >> 16];
	return ((uint32_t(seg.e - 1) << 23) + uint32_t(vfpu_interp(seg, index & 0xFFFF))) & ~3u;
}

// Sine of a quarter-wave angle arg in [0, 2^23] (0 to pi/2), in fixed point with 28 fraction bits.
// The segments are indexed from the top of the quarter wave. Each has its own exponent, and its
// results are truncated to 4 of its ulps even where they fall into a lower binade.
static inline uint32_t vfpu_sin_fixed(uint32_t arg) {
	if (arg == 0u) return 0u;
	if (arg == 0x00800000u) return 0x10000000u;
	const uint32_t y = 0x00800000u - arg;
	const VFPUSegment &seg = vfpu_sin_segments[y >> 16];
	const uint32_t v = uint32_t(vfpu_interp(seg, y & 0xFFFF)) & ~3u;
	return seg.e >= 122 ? v << (seg.e - 122) : v >> (122 - seg.e);
}

// Reduces an angle in quarter turns (the VFPU's unit) to a half turn, in units of 2^-23 quarter
// turns. Sets *odd for an odd half turn, where the sine changes sign. Returns false for inf and NaN.
static inline bool vfpu_sin_reduce(uint32_t bits, uint32_t *angle, bool *odd) {
	const uint32_t exponent = (bits >> 23) & 0xFFu;
	uint32_t significand = (bits & 0x007FFFFFu) | 0x00800000u;
	if (exponent == 0xFFu)
		return false;
	if (exponent < 0x7Fu) {
		if (exponent < 0x7Fu - 23u) significand = 0u;
		else significand >>= (0x7F - exponent);
	} else if (exponent > 0x7Fu) {
		// There is weirdness for large exponents.
		if (exponent - 0x7Fu >= 25u && exponent - 0x7Fu < 32u) significand = 0u;
		else if ((exponent & 0x9Fu) == 0x9Fu) significand = 0u;
		else significand <<= ((exponent - 0x7Fu) & 31);
	}
	*odd = (significand >> 24) & 1;
	*angle = significand & 0x00FFFFFFu;
	return true;
}

static inline uint32_t vfpu_bits_from_float(float f) {
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	return bits;
}

static inline float vfpu_float_from_bits(uint32_t bits) {
	float f;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

static inline int vfpu_clz64_nonzero(uint64_t v) {
	return (v >> 32) != 0 ? (int)clz32_nonzero(uint32_t(v >> 32)) : 32 + (int)clz32_nonzero(uint32_t(v));
}

// Float bits of v * 2^-fracBits, with the sign bit set if negative (also for zero). v has to fit
// in a float's significand, which every result here does, since the VFPU keeps 22 bits.
static inline uint32_t vfpu_fixed_to_bits(uint64_t v, int fracBits, bool negative) {
	const uint32_t sign = negative ? 0x80000000u : 0u;
	if (v == 0)
		return sign;
	const int top = 63 - vfpu_clz64_nonzero(v);
	const uint64_t significand = top <= 23 ? v << (23 - top) : v >> (top - 23);
	return sign + (uint32_t(top - fracBits + 127) << 23) + (uint32_t(significand) & 0x007FFFFFu);
}

static inline uint32_t vfpu_sin_from_reduced(uint32_t angle, bool negate) {
	if (angle > 0x00800000u) angle = 0x01000000u - angle;
	return vfpu_fixed_to_bits(vfpu_sin_fixed(angle), 28, negate);
}

static inline uint32_t vfpu_cos_from_reduced(uint32_t angle, bool negate) {
	if (angle >= 0x00800000u) {
		angle = 0x01000000u - angle;
		negate = !negate;
	}
	return vfpu_fixed_to_bits(vfpu_sin_fixed(0x00800000u - angle), 28, negate);
}

// These work on float bits throughout. A float made from a constant NaN bit pattern can come out
// quieted (MSVC does this), and the VFPU's NaN is signaling.
static uint32_t vfpu_sin_bits(uint32_t bits) {
	uint32_t angle;
	bool odd;
	if (!vfpu_sin_reduce(bits, &angle, &odd))
		return (bits & 0x80000000u) ^ 0x7F800001u;
	return vfpu_sin_from_reduced(angle, (bits >> 31) != odd);
}

static uint32_t vfpu_cos_bits(uint32_t bits) {
	uint32_t angle;
	bool odd;
	if (!vfpu_sin_reduce(bits, &angle, &odd))
		return 0x7F800001u;
	return vfpu_cos_from_reduced(angle, odd);
}

float vfpu_sin(float x) {
	return vfpu_float_from_bits(vfpu_sin_bits(vfpu_bits_from_float(x)));
}

float vfpu_cos(float x) {
	return vfpu_float_from_bits(vfpu_cos_bits(vfpu_bits_from_float(x)));
}

// Reduces the angle once for both.
void vfpu_sincos(float a, float &s, float &c) {
	const uint32_t bits = vfpu_bits_from_float(a);
	uint32_t angle;
	bool odd;
	uint32_t sinBits, cosBits;
	if (!vfpu_sin_reduce(bits, &angle, &odd)) {
		sinBits = (bits & 0x80000000u) ^ 0x7F800001u;
		cosBits = 0x7F800001u;
	} else {
		sinBits = vfpu_sin_from_reduced(angle, (bits >> 31) != odd);
		cosBits = vfpu_cos_from_reduced(angle, odd);
	}
	s = vfpu_float_from_bits(sinBits);
	c = vfpu_float_from_bits(cosBits);
}

// The tables for the fast paths (see VFPUFastSegment). bias moves the segment's binade to the
// exponent the result's bits start from.
static constexpr std::array<VFPUFastSegment, 128> vfpu_make_fast_table(const VFPUSegment (&segments)[128], int bias) {
	std::array<VFPUFastSegment, 128> table{};
	for (int i = 0; i < 128; i++) {
		const VFPUSegment &seg = segments[i];
		table[i] = { uint32_t(seg.c0) + (uint32_t(seg.e - 1 + bias) << 23), seg.m, seg.n, 0 };
	}
	return table;
}

const std::array<VFPUFastSegment, 128> vfpu_rcp_fast = vfpu_make_fast_table(vfpu_rcp_segments, 127);
const std::array<VFPUFastSegment, 128> vfpu_rsqrt_fast = vfpu_make_fast_table(vfpu_rsqrt_segments, 64);
const std::array<VFPUFastSegment, 128> vfpu_sqrt_fast = vfpu_make_fast_table(vfpu_sqrt_segments, -64);

static inline uint32_t vfpu_fast_interp(const std::array<VFPUFastSegment, 128> &table, uint32_t w, uint32_t exponent) {
	const VFPUFastSegment &seg = table[(w >> 16) & 0x7F];
	const uint32_t x2 = w & 0xFFFF;
	const uint32_t linear = uint32_t(((int64_t)seg.m * x2) >> 17);
	const uint32_t square = uint32_t((seg.n * vfpu_square(x2)) >> 9);
	return (seg.k + exponent + linear + square) & ~3u;
}

float vfpu_sqrt(float x) {
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	if (vfpu_sqrt_is_fast(bits)) {
		const uint32_t w = (bits + 0x00800000u) >> 1;
		bits = vfpu_fast_interp(vfpu_sqrt_fast, w, w & 0x7F800000u);
	} else if ((bits & 0x7FFFFFFFu) < 0x00800000u) {
		// Zero and denormals give +0, whatever the sign.
		bits = 0;
	} else {
		// Negatives and NaN give NaN, inf gives inf.
		bits = bits == 0x7F800000u ? 0x7F800000u : 0x7F800001u;
	}
	memcpy(&x, &bits, sizeof(bits));
	return x;
}

float vfpu_rsqrt(float x) {
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	if (vfpu_sqrt_is_fast(bits)) {
		const uint32_t w = (bits + 0x00800000u) >> 1;
		bits = vfpu_fast_interp(vfpu_rsqrt_fast, w, 0u - (w & 0x7F800000u));
	} else if ((bits & 0x7FFFFFFFu) < 0x00800000u) {
		// Zero and denormals give inf of the same sign.
		bits = (bits & 0x80000000u) | 0x7F800000u;
	} else {
		// Negatives give -NaN, NaN gives NaN and inf gives 0.
		bits = (bits >> 31) ? 0xFF800001u : (bits > 0x7F800000u ? 0x7F800001u : 0u);
	}
	memcpy(&x, &bits, sizeof(bits));
	return x;
}

// asin(x) * 2/pi in fixed point with 30 fraction bits, for x in [0, 1] as 23-bit fixed point.
static inline uint32_t vfpu_asin_fixed(uint32_t x) {
	if (x == 0u) return 0u;
	if (x == 1u << 23) return 1u << 30;
	const VFPUSegment &seg = vfpu_asin_segments[x >> 16];
	// The first segment is linear, and keeps the 2^-30 step of the fixed-point output.
	const uint32_t v = uint32_t(vfpu_interp(seg, x & 0xFFFF)) & ((x >> 16) == 0 ? ~1u : ~3u);
	return seg.e >= 120 ? v << (seg.e - 120) : v >> (120 - seg.e);
}

float vfpu_asin(float x) {
	const uint32_t bits = vfpu_bits_from_float(x);
	const uint32_t sign = bits & 0x80000000u;
	const uint32_t abs = bits & 0x7FFFFFFFu;
	uint32_t result;
	if (abs > 0x3F800000u) {
		result = 0x7F800001u ^ sign;
	} else {
		// |x| * 2^23, truncated. Anything below 2^-23, denormals included, gives 0.
		const int e = int(abs >> 23);
		const uint32_t significand = (abs & 0x007FFFFFu) | 0x00800000u;
		const uint32_t fixed = e < 104 ? 0 : significand >> (127 - e);
		result = vfpu_fixed_to_bits(vfpu_asin_fixed(fixed), 30, sign != 0);
	}
	return vfpu_float_from_bits(result);
}

static uint32_t vfpu_exp2_bits(uint32_t bits) {
	const uint32_t abs = bits & 0x7FFFFFFFu;
	const bool negative = (bits >> 31) != 0;
	if (abs <= 0x007FFFFFu) {
		// Denormals are treated as 0.
		return 0x3F800000u;
	}
	if (abs > 0x7F800000u) {
		// NaN gets NaN.
		return 0x7F800001u;
	}
	if (negative && abs >= 0x42FC0000u) {
		// -126 and below get 0 (exp2(-126) is the smallest normal, but yes, -126 gives +0).
		return 0;
	}
	if (!negative && abs >= 0x43000000u) {
		// 128 and above get infinity.
		return 0x7F800000u;
	}
	// x * 2^23 truncated toward zero, and one less for a negative x. Yes, really.
	const int e = int(abs >> 23);
	const uint32_t significand = (abs & 0x007FFFFFu) | 0x00800000u;
	int32_t fixed = e >= 127 ? int32_t(significand << (e - 127)) : (e < 104 ? 0 : int32_t(significand >> (127 - e)));
	if (negative)
		fixed = -fixed - 1;
	// The fraction's exp2 is in [1, 2), so its significand bits are the offset from 1.0.
	const uint32_t frac = uint32_t(fixed) & 0x007FFFFFu;
	const uint32_t significandBits = frac == 0 ? 0 : vfpu_interp_bits(vfpu_exp2_segments, frac) - 0x3F800000u;
	return 0x3F800000u + (uint32_t(fixed) & 0xFF800000u) + significandBits;
}

float vfpu_exp2(float x) {
	return vfpu_float_from_bits(vfpu_exp2_bits(vfpu_bits_from_float(x)));
}

float vfpu_rexp2(float x) {
	return vfpu_float_from_bits(vfpu_exp2_bits(vfpu_bits_from_float(x) ^ 0x80000000u));
}

static uint32_t vfpu_log2_bits(uint32_t bits) {
	if ((bits & 0x7FFFFFFFu) <= 0x007FFFFFu) {
		// Denormals (and zeroes) get -inf.
		return 0xFF800000u;
	}
	if (bits & 0x80000000u) {
		// Other negatives get NaN.
		return 0x7F800001u;
	}
	if ((bits >> 23) == 255u) {
		// NaN gets NaN, +inf gets +inf.
		return 0x7F800000u + ((bits & 0x007FFFFFu) != 0);
	}
	// The result is exponent + log2(1.mantissa), truncated toward zero to 22 significant bits: a
	// step of 2^-22 for exponents 0 and 1, twice that for each doubling of the exponent after that,
	// and 2^-15 for every negative exponent (level d = 7). Where the step is coarser, the datapath
	// drops coefficient bits too: m to the step, |n| to 2^d, and c0 takes the part of the squared
	// term that was dropped, at the edge of the segment.
	const int32_t exponent = int32_t(bits >> 23) - 127;
	const uint32_t index = bits & 0x007FFFFFu;
	const int d = exponent < 0 ? 7 : exponent < 2 ? 0 : 31 - (int)clz32_nonzero(uint32_t(exponent));
	const int32_t p = 1 << d;
	const int64_t step = int64_t(4 << d) << 17;
	const VFPUSegment &seg = vfpu_log2_segments[index >> 16];
	const uint32_t x2 = index & 0xFFFF;
	const int32_t n = seg.n < 0 ? -(-seg.n & ~(p - 1)) : (seg.n & ~(p - 1));
	const int32_t c0 = (seg.c0 + 2 * (seg.n - n)) & ~(p - 1);
	const int32_t square = ((n * vfpu_square(x2)) >> 9) & ~(p - 1);
	const int64_t m = seg.m & ~((4 << d) - 1);
	// In units of 2^-41.
	const int64_t y = (int64_t(c0 + square) << 17) + m * x2;
	const int64_t frac = exponent >= 0 ? (y & ~(step - 1)) : -(-y & ~(step - 1));
	// A negative sum truncated to zero keeps its sign.
	const int64_t sum = (int64_t(exponent) << 41) + frac;
	return vfpu_fixed_to_bits(uint64_t(sum < 0 ? -sum : sum), 41, exponent < 0);
}

float vfpu_log2(float x) {
	return vfpu_float_from_bits(vfpu_log2_bits(vfpu_bits_from_float(x)));
}

float vfpu_rcp(float x) {
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	if (vfpu_rcp_is_fast(bits)) {
		bits = vfpu_fast_interp(vfpu_rcp_fast, bits, 0u - (bits & 0xFF800000u));
	} else {
		// Zero and denormals give inf, NaN gives NaN and the rest 0, all with the sign of x.
		const uint32_t abs = bits & 0x7FFFFFFFu;
		bits = (bits & 0x80000000u) ^ (abs < 0x00800000u ? 0x7F800000u : (abs > 0x7F800000u ? 0x7F800001u : 0u));
	}
	memcpy(&x, &bits, sizeof(x));
	return x;
}
