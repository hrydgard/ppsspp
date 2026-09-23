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
#include "Common/File/VFS/VFS.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/StringUtils.h"
#include "Core/Reporting.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/MIPSVFPUFallbacks.h"

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
// The code below attempts to exactly match the output of
// several PSP's VFPU functions. For the sake of
// making lookup tables smaller the code is
// somewhat gnarly.
// Lookup tables sometimes store deltas from (explicitly computable)
// estimations, to allow to store them in smaller types.
// See https://github.com/hrydgard/ppsspp/issues/16946 for details.

// rcp, rsqrt, sqrt and exp2 share one quadratic interpolator. The top 7 bits of a 23-bit input
// index pick one of 128 segments with their own coefficients; the other 16 bits, x2, enter the
// linear term in full, while the squared term only sees the top 10 of them, as a distance t from the
// middle of the segment. The squarer rounds t^2 up to a multiple of 256, and the squared term is
// floored separately from the rest. The sum is the significand (implicit bit included) in ulps of the
// segment's exponent, truncated to 22 bits like every VFPU result. Derived from the output of the
// table-based versions this replaces, and bit-exact with them over every input.
struct VFPUSegment {
	int32_t c0;  // value at x2 = 0, in ulps of 2^(e - 150), implicit bit included
	int32_t m;   // slope in 2^-17 ulps per step of x2
	int16_t n;   // squared term coefficient
	int16_t e;   // binade whose ulps the segment works in
};

// 1/x for x in [1, 2).
static const VFPUSegment vfpu_rcp_segments[128] = {
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
static const VFPUSegment vfpu_rsqrt_segments[128] = {
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
static const VFPUSegment vfpu_sqrt_segments[128] = {
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

// Lookup tables.
// Note: these are never unloaded, and stay till program termination.
static uint32_t *vfpu_sin_lut8192=nullptr;
static  int8_t  (*vfpu_sin_lut_delta)[2]=nullptr;
static  int16_t *vfpu_sin_lut_interval_delta=nullptr;
static uint8_t  *vfpu_sin_lut_exceptions=nullptr;

static uint32_t *vfpu_log2_lut65536=nullptr;
static uint32_t *vfpu_log2_lut65536_quadratic=nullptr;
static uint8_t  (*vfpu_log2_lut)[131072][2]=nullptr;

static  int32_t (*vfpu_asin_lut65536)[3]=nullptr;
static uint64_t *vfpu_asin_lut_deltas=nullptr;
static uint16_t *vfpu_asin_lut_indices=nullptr;

template<typename T>
static inline bool load_vfpu_table(T *&ptr, const char *filename, size_t expected_size) {
#if COMMON_BIG_ENDIAN
	// Tables are little-endian.
#error Byteswap for VFPU tables not implemented
#endif
	if (ptr) return true; // Already loaded.
	size_t size = 0u;
	INFO_LOG(Log::CPU, "Loading '%s'...", filename);
	ptr = reinterpret_cast<decltype(&*ptr)>(g_VFS.ReadFile(filename, &size));
	if (!ptr || size != expected_size) {
		ERROR_LOG(Log::CPU, "Error loading '%s' (size=%u, expected: %u)", filename, (unsigned)size, (unsigned)expected_size);
		delete[] ptr;
		ptr = nullptr;
		return false;
	}
	INFO_LOG(Log::CPU, "Successfully loaded '%s'", filename);
	return true;
}

#define LOAD_TABLE(name, expected_size)\
	load_vfpu_table(name,"vfpu/" #name ".dat",expected_size)

// Note: PSP sin/cos output only has 22 significant
// binary digits.
static inline uint32_t vfpu_sin_quantum(uint32_t x) {
	return x < 1u << 22?
		1u:
		1u << (32 - 22 - clz32_nonzero(x));
}

static inline uint32_t vfpu_sin_truncate_bits(u32 x) {
	return x & -vfpu_sin_quantum(x);
}

static inline uint32_t vfpu_sin_fixed(uint32_t arg) {
	// Handle endpoints.
	if(arg == 0u) return 0u;
	if(arg == 0x00800000) return 0x10000000;
	// Get endpoints for 8192-wide interval.
	uint32_t L = vfpu_sin_lut8192[(arg >> 13) + 0];
	uint32_t H = vfpu_sin_lut8192[(arg >> 13) + 1];
	// Approximate endpoints for 64-wide interval via lerp.
	uint32_t A = L+(((H - L)*(((arg >> 6) & 127) + 0)) >> 7);
	uint32_t B = L+(((H - L)*(((arg >> 6) & 127) + 1)) >> 7);
	// Adjust endpoints from deltas, and increase working precision.
	uint64_t a = (uint64_t(A) << 5) + uint64_t(vfpu_sin_lut_delta[arg >> 6][0]) * vfpu_sin_quantum(A);
	uint64_t b = (uint64_t(B) << 5) + uint64_t(vfpu_sin_lut_delta[arg >> 6][1]) * vfpu_sin_quantum(B);
	// Compute approximation via lerp. Is off by at most 1 quantum.
	uint32_t v = uint32_t(((a * (64 - (arg & 63)) + b * (arg & 63)) >> 6) >> 5);
	v=vfpu_sin_truncate_bits(v);
	// Look up exceptions via binary search.
	// Note: vfpu_sin_lut_interval_delta stores
	// deltas from interval estimation.
	uint32_t lo = ((169u * ((arg >> 7) + 0)) >> 7)+uint32_t(vfpu_sin_lut_interval_delta[(arg >> 7) + 0]) + 16384u;
	uint32_t hi = ((169u * ((arg >> 7) + 1)) >> 7)+uint32_t(vfpu_sin_lut_interval_delta[(arg >> 7) + 1]) + 16384u;
	while(lo < hi) {
		uint32_t m = (lo + hi) / 2;
		// Note: vfpu_sin_lut_exceptions stores
		// index&127 (for each initial interval the
		// upper bits of index are the same, namely
		// arg&-128), plus direction (0 for +1, and
		// 128 for -1).
		uint32_t b = vfpu_sin_lut_exceptions[m];
		uint32_t e = (arg & -128u)+(b & 127u);
		if(e == arg) {
			v += vfpu_sin_quantum(v) * (b >> 7 ? -1u : +1u);
			break;
		}
		else if(e < arg) lo = m + 1;
		else			 hi = m;
	}
	return v;
}

float vfpu_sin(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_sin_lut8192,              4100)&&
		LOAD_TABLE(vfpu_sin_lut_delta,          262144)&&
		LOAD_TABLE(vfpu_sin_lut_interval_delta, 131074)&&
		LOAD_TABLE(vfpu_sin_lut_exceptions,      86938);
	if (!loaded)
		return vfpu_sin_fallback(x);
	uint32_t bits;
	memcpy(&bits, &x, sizeof(x));
	uint32_t sign = bits & 0x80000000u;
	uint32_t exponent = (bits >> 23) & 0xFFu;
	uint32_t significand = (bits & 0x007FFFFFu) | 0x00800000u;
	if(exponent == 0xFFu) {
		// NOTE: this bitpattern is a signaling
		// NaN on x86, so maybe just return
		// a normal qNaN?
		float y;
		bits=sign ^ 0x7F800001u;
		memcpy(&y, &bits, sizeof(y));
		return y;
	}
	if(exponent < 0x7Fu) {
		if(exponent < 0x7Fu-23u) significand = 0u;
		else significand >>= (0x7F - exponent);
	}
	else if(exponent > 0x7Fu) {
		// There is weirdness for large exponents.
		if(exponent - 0x7Fu >= 25u && exponent - 0x7Fu < 32u) significand = 0u;
		else if((exponent & 0x9Fu) == 0x9Fu) significand = 0u;
		else significand <<= ((exponent - 0x7Fu) & 31);
	}
	sign ^= ((significand << 7) & 0x80000000u);
	significand &= 0x00FFFFFFu;
	if(significand > 0x00800000u) significand = 0x01000000u - significand;
	uint32_t ret = vfpu_sin_fixed(significand);
	return (sign ? -1.0f : +1.0f) * float(int32_t(ret)) * 3.7252903e-09f; // 0x1p-28f
}

float vfpu_cos(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_sin_lut8192,              4100)&&
		LOAD_TABLE(vfpu_sin_lut_delta,          262144)&&
		LOAD_TABLE(vfpu_sin_lut_interval_delta, 131074)&&
		LOAD_TABLE(vfpu_sin_lut_exceptions,      86938);
	if (!loaded)
		return vfpu_cos_fallback(x);
	uint32_t bits;
	memcpy(&bits, &x, sizeof(x));
	bits &= 0x7FFFFFFFu;
	uint32_t sign = 0u;
	uint32_t exponent = (bits >> 23) & 0xFFu;
	uint32_t significand = (bits & 0x007FFFFFu) | 0x00800000u;
	if(exponent == 0xFFu) {
		// NOTE: this bitpattern is a signaling
		// NaN on x86, so maybe just return
		// a normal qNaN?
		float y;
		bits = sign ^ 0x7F800001u;
		memcpy(&y, &bits, sizeof(y));
		return y;
	}
	if(exponent < 0x7Fu) {
		if(exponent < 0x7Fu - 23u) significand = 0u;
		else significand >>= (0x7F - exponent);
	}
	else if(exponent > 0x7Fu) {
		// There is weirdness for large exponents.
		if(exponent - 0x7Fu >= 25u && exponent - 0x7Fu < 32u) significand = 0u;
		else if((exponent & 0x9Fu) == 0x9Fu) significand = 0u;
		else significand <<= ((exponent - 0x7Fu) & 31);
	}
	sign ^= ((significand << 7) & 0x80000000u);
	significand &= 0x00FFFFFFu;
	if(significand >= 0x00800000u) {
		significand = 0x01000000u - significand;
		sign ^= 0x80000000u;
	}
	uint32_t ret = vfpu_sin_fixed(0x00800000u - significand);
	return (sign ? -1.0f : +1.0f) * float(int32_t(ret)) * 3.7252903e-09f; // 0x1p-28f
}

void vfpu_sincos(float a, float &s, float &c) {
	// Just invoke both sin and cos.
	// Suboptimal but whatever.
	s = vfpu_sin(a);
	c = vfpu_cos(a);
}

float vfpu_sqrt(float x) {
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	if((bits & 0x7FFFFFFFu) <= 0x007FFFFFu) {
		// Denormals (and zeroes) get +0, regardless
		// of sign.
		return +0.0f;
	}
	if(bits >> 31) {
		// Other negatives get NaN.
		bits = 0x7F800001u;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	if((bits >> 23) == 255u) {
		// Inf/NaN gets Inf/NaN.
		bits = 0x7F800000u + ((bits & 0x007FFFFFu) != 0u);
		memcpy(&x, &bits, sizeof(bits));
		return x;
	}
	int32_t exponent = int32_t(bits >> 23) - 127;
	// Bottom bit of exponent (inverted) + significand (except bottom bit).
	uint32_t index = ((bits + 0x00800000u) >> 1) & 0x007FFFFFu;
	bits = vfpu_interp_bits(vfpu_sqrt_segments, index);
	bits += uint32_t(exponent >> 1) << 23;
	memcpy(&x, &bits, sizeof(bits));
	return x;
}

float vfpu_rsqrt(float x) {
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	if((bits & 0x7FFFFFFFu) <= 0x007FFFFFu) {
		// Denormals (and zeroes) get inf of the same sign.
		bits = 0x7F800000u | (bits & 0x80000000u);
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	if(bits >> 31) {
		// Other negatives get negative NaN.
		bits = 0xFF800001u;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	if((bits >> 23) == 255u) {
		// inf gets 0, NaN gets NaN.
		bits = ((bits & 0x007FFFFFu) ? 0x7F800001u : 0u);
		memcpy(&x, &bits, sizeof(bits));
		return x;
	}
	int32_t exponent = int32_t(bits >> 23) - 127;
	// Bottom bit of exponent (inverted) + significand (except bottom bit).
	uint32_t index = ((bits + 0x00800000u) >> 1) & 0x007FFFFFu;
	bits = vfpu_interp_bits(vfpu_rsqrt_segments, index);
	bits -= uint32_t(exponent >> 1) << 23;
	memcpy(&x, &bits, sizeof(bits));
	return x;
}

static inline uint32_t vfpu_asin_quantum(uint32_t x) {
	return x<1u<<23?
		1u:
		1u<<(32-23-clz32_nonzero(x));
}

static inline uint32_t vfpu_asin_truncate_bits(uint32_t x) {
	return x & -vfpu_asin_quantum(x);
}

// Input is fixed 9.23, output is fixed 2.30.
static inline uint32_t vfpu_asin_approx(uint32_t x) {
	const int32_t *C = vfpu_asin_lut65536[x >> 16];
	x &= 0xFFFFu;
	return vfpu_asin_truncate_bits(uint32_t((((((int64_t(C[2]) * x) >> 16) + int64_t(C[1])) * x) >> 16) + C[0]));
}

// Input is fixed 9.23, output is fixed 2.30.
static uint32_t vfpu_asin_fixed(uint32_t x) {
	if(x == 0u) return 0u;
	if(x == 1u << 23) return 1u << 30;
	uint32_t ret = vfpu_asin_approx(x);
	uint32_t index = vfpu_asin_lut_indices[x / 21u];
	uint64_t deltas = vfpu_asin_lut_deltas[index];
	return ret + (3u - uint32_t((deltas >> (3u * (x % 21u))) & 7u)) * vfpu_asin_quantum(ret);
}

float vfpu_asin(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_asin_lut65536,      1536)&&
		LOAD_TABLE(vfpu_asin_lut_indices, 798916)&&
		LOAD_TABLE(vfpu_asin_lut_deltas,  517448);
	if (!loaded)
		return vfpu_asin_fallback(x);

	uint32_t bits;
	memcpy(&bits, &x, sizeof(x));
	uint32_t sign = bits & 0x80000000u;
	bits = bits & 0x7FFFFFFFu;
	if(bits > 0x3F800000u) {
		bits = 0x7F800001u ^ sign;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}

	bits = vfpu_asin_fixed(uint32_t(int32_t(fabsf(x) * 8388608.0f))); // 0x1p23
	x=float(int32_t(bits)) * 9.31322574615478515625e-10f; // 0x1p-30
	if(sign) x = -x;
	return x;
}

float vfpu_exp2(float x) {
	int32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	if((bits & 0x7FFFFFFF) <= 0x007FFFFF) {
		// Denormals are treated as 0.
		return 1.0f;
	}
	if(x != x) {
		// NaN gets NaN.
		bits = 0x7F800001u;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	if(x <= -126.0f) {
		// Small numbers get 0 (exp2(-126) is smallest positive non-denormal).
		// But yes, -126.0f produces +0.0f.
		return 0.0f;
	}
	if(x >= +128.0f) {
		// Large numbers get infinity.
		bits = 0x7F800000u;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	bits = int32_t(x * 0x1p23f);
	if(x < 0.0f) --bits; // Yes, really.
	// The fraction's exp2 is in [1, 2), so its significand bits are the offset from 1.0.
	const uint32_t frac = bits & 0x007FFFFF;
	const int32_t significand = frac == 0 ? 0 : int32_t(vfpu_interp_bits(vfpu_exp2_segments, frac) - 0x3F800000u);
	bits = int32_t(0x3F800000) + (bits & int32_t(0xFF800000)) + significand;
	memcpy(&x, &bits, sizeof(bits));
	return x;
}

float vfpu_rexp2(float x) {
	return vfpu_exp2(-x);
}

// Input fixed 9.23, output fixed 10.22.
// Returns log2(1+x).
static inline uint32_t vfpu_log2_approx(uint32_t x) {
	uint32_t a = vfpu_log2_lut65536[(x >> 16) + 0];
	uint32_t b = vfpu_log2_lut65536[(x >> 16) + 1];
	uint32_t c = vfpu_log2_lut65536_quadratic[x >> 16];
	x &= 0xFFFFu;
	uint64_t ret = uint64_t(a) * (0x10000u - x) + uint64_t(b) * x;
	uint64_t d = (uint64_t(c) * x * (0x10000u-x)) >> 40;
	ret += d;
	return uint32_t(ret >> 16);
}

// Matches PSP output on all known values.
float vfpu_log2(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_log2_lut65536,               516)&&
		LOAD_TABLE(vfpu_log2_lut65536_quadratic,     512)&&
		LOAD_TABLE(vfpu_log2_lut,                2097152);
	if (!loaded)
		return vfpu_log2_fallback(x);
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	if((bits & 0x7FFFFFFFu) <= 0x007FFFFFu) {
		// Denormals (and zeroes) get -inf.
		bits = 0xFF800000u;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	if(bits & 0x80000000u) {
		// Other negatives get NaN.
		bits = 0x7F800001u;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	if((bits >> 23) == 255u) {
		// NaN gets NaN, +inf gets +inf.
		bits = 0x7F800000u + ((bits & 0x007FFFFFu) != 0);
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	uint32_t e = (bits & 0x7F800000u) - 0x3F800000u;
	uint32_t i = bits & 0x007FFFFFu;
	if(e >> 31 && i >= 0x007FFE00u) {
		// Process 1-2^{-14}<=x*2^n<1 (for n>0) separately,
		// since the table doesn't give the right answer.
		float c = float(int32_t(~e) >> 23);
		// Note: if c is 0 the sign of -0 output is correct.
		return i < 0x007FFEF7u ? // 1-265*2^{-24}
			-3.05175781e-05f - c:
			-0.0f - c;
	}
	int d = (e < 0x01000000u ? 0 : 8 - clz32_nonzero(e) - int(e >> 31));
	//assert(d >= 0 && d < 8);
	uint32_t q = 1u << d;
	uint32_t A = vfpu_log2_approx((i     ) & -64u) & -q;
	uint32_t B = vfpu_log2_approx((i + 64) & -64u) & -q;
	uint64_t a = (A << 6)+(uint64_t(vfpu_log2_lut[d][i >> 6][0]) - 80ull) * q;
	uint64_t b = (B << 6)+(uint64_t(vfpu_log2_lut[d][i >> 6][1]) - 80ull) * q;
	uint32_t v = uint32_t((a +(((b - a) * (i & 63)) >> 6)) >> 6);
	v &= -q;
	bits = e ^ (2u * v);
	x = float(int32_t(bits)) * 1.1920928955e-7f; // 0x1p-23f
	return x;
}

float vfpu_rcp(float x) {
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	uint32_t s = bits & 0x80000000u;
	uint32_t e = bits & 0x7F800000u;
	uint32_t i = bits & 0x007FFFFFu;
	if((bits & 0x7FFFFFFFu) > 0x7E800000u) {
		bits = (e == 0x7F800000u && i ? s ^ 0x7F800001u : s);
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	if(e==0u) {
		bits = s^0x7F800000u;
		memcpy(&x, &bits, sizeof(x));
		return x;
	}
	bits = s + (0x3F800000u - e) + vfpu_interp_bits(vfpu_rcp_segments, i);
	memcpy(&x, &bits, sizeof(x));
	return x;
}

//==============================================================================

void InitVFPU() {
#if 0
	// Load all in advance.
	LOAD_TABLE(vfpu_asin_lut65536          ,    1536); 
	LOAD_TABLE(vfpu_asin_lut_deltas        ,  517448); 
	LOAD_TABLE(vfpu_asin_lut_indices       ,  798916); 
	LOAD_TABLE(vfpu_log2_lut65536          ,     516); 
	LOAD_TABLE(vfpu_log2_lut65536_quadratic,     512); 
	LOAD_TABLE(vfpu_log2_lut               , 2097152); 
	LOAD_TABLE(vfpu_sin_lut8192            ,    4100); 
	LOAD_TABLE(vfpu_sin_lut_delta          ,  262144); 
	LOAD_TABLE(vfpu_sin_lut_exceptions     ,   86938); 
	LOAD_TABLE(vfpu_sin_lut_interval_delta ,  131074); 
#endif
}
