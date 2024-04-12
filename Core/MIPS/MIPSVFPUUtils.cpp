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
#include <limits>
#include <cstdio>
#include <cstring>

#include "Common/BitScan.h"
#include "Common/CommonFuncs.h"
#include "Common/File/VFS/VFS.h"
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

void ReadVector(float *rd, VectorSize size, int reg) {
	int row;
	int length;
	switch (size) {
	case V_Single: rd[0] = currentMIPS->v[voffset[reg]]; return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
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
			rd[i] = currentMIPS->v[base + ((row + i) & 3) * 4];
	} else {
		const int base = mtx + col * 4;
		for (int i = 0; i < length; i++)
			rd[i] = currentMIPS->v[base + ((row + i) & 3)];
	}
}

void WriteVector(const float *rd, VectorSize size, int reg) {
	int row;
	int length;

	switch (size) {
	case V_Single: if (!currentMIPS->VfpuWriteMask(0)) currentMIPS->v[voffset[reg]] = rd[0]; return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
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
		if (currentMIPS->VfpuWriteMask() == 0) {
			for (int i = 0; i < length; i++)
				currentMIPS->v[base + ((row+i) & 3) * 4] = rd[i];
		} else {
			for (int i = 0; i < length; i++) {
				if (!currentMIPS->VfpuWriteMask(i)) {
					currentMIPS->v[base + ((row+i) & 3) * 4] = rd[i];
				}
			}
		}
	} else {
		const int base = mtx + col * 4;
		if (currentMIPS->VfpuWriteMask() == 0) {
			for (int i = 0; i < length; i++)
				currentMIPS->v[base + ((row + i) & 3)] = rd[i];
		} else {
			for (int i = 0; i < length; i++) {
				if (!currentMIPS->VfpuWriteMask(i)) {
					currentMIPS->v[base + ((row + i) & 3)] = rd[i];
				}
			}
		}
	}
}

u32 VFPURewritePrefix(int ctrl, u32 remove, u32 add) {
	u32 prefix = currentMIPS->vfpuCtrl[ctrl];
	return (prefix & ~remove) | add;
}

void ReadMatrix(float *rd, MatrixSize size, int reg) {
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
	const float *v = currentMIPS->v + (size_t)mtx * 16;
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

void WriteMatrix(const float *rd, MatrixSize size, int reg) {
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
	float *v = currentMIPS->v + (size_t)mtx * 16;
	if (transpose) {
		if (side == 4 && row == 0 && col == 0 && currentMIPS->VfpuWriteMask() == 0x0) {
			// Fast path: Simple 4x4 transpose. TODO: Optimize.
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					v[i * 4 + j] = rd[j * 4 + i];
				}
			}
		} else {
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					if (j != side - 1 || !currentMIPS->VfpuWriteMask(i)) {
						int index = ((row + i) & 3) * 4 + ((col + j) & 3);
						v[index] = rd[j * 4 + i];
					}
				}
			}
		}
	} else {
		if (side == 4 && row == 0 && col == 0 && currentMIPS->VfpuWriteMask() == 0x0) {
			memcpy(v, rd, sizeof(float) * 16);  // v[j * 4 + i] = rd[j * 4 + i];
		} else {
			for (int j = 0; j < side; j++) {
				for (int i = 0; i < side; i++) {
					if (j != side - 1 || !currentMIPS->VfpuWriteMask(i)) {
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
	GetVectorRegs(regs2, size1, vec2);
	int count = 0;
	for (int i = 0; i < n1; i++) {
		for (int j = 0; j < n2; j++) {
			if (regs1[i] == regs2[j])
				count++;
		}
	}
	return count;
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
		*mask = 0x3FFFFFFF;
		return true;
	default:
		return false;
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

// Reference C++ version.
static float vfpu_dot_cpp(const float a[4], const float b[4]) {
	static const int EXTRA_BITS = 2;
	float2int result;
	float2int src[2];

	int32_t exps[4];
	int32_t mants[4];
	int32_t signs[4];
	int32_t max_exp = 0;
	int32_t last_inf = -1;

	for (int i = 0; i < 4; i++) {
		src[0].f = a[i];
		src[1].f = b[i];

		int32_t aexp = get_uexp(src[0].i);
		int32_t bexp = get_uexp(src[1].i);
		int32_t amant = get_mant(src[0].i) << EXTRA_BITS;
		int32_t bmant = get_mant(src[1].i) << EXTRA_BITS;

		exps[i] = aexp + bexp - 127;
		if (aexp == 255) {
			// INF * 0 = NAN
			if ((src[0].i & 0x007FFFFF) != 0 || bexp == 0) {
				result.i = 0x7F800001;
				return result.f;
			}
			mants[i] = get_mant(0) << EXTRA_BITS;
			exps[i] = 255;
		} else if (bexp == 255) {
			if ((src[1].i & 0x007FFFFF) != 0 || aexp == 0) {
				result.i = 0x7F800001;
				return result.f;
			}
			mants[i] = get_mant(0) << EXTRA_BITS;
			exps[i] = 255;
		} else {
			// TODO: Adjust precision?
			uint64_t adjust = (uint64_t)amant * (uint64_t)bmant;
			mants[i] = (adjust >> (23 + EXTRA_BITS)) & 0x7FFFFFFF;
		}
		signs[i] = get_sign(src[0].i) ^ get_sign(src[1].i);

		if (exps[i] > max_exp) {
			max_exp = exps[i];
		}
		if (exps[i] >= 255) {
			// Infinity minus infinity is not a real number.
			if (last_inf != -1 && signs[i] != last_inf) {
				result.i = 0x7F800001;
				return result.f;
			}
			last_inf = signs[i];
		}
	}

	int32_t mant_sum = 0;
	for (int i = 0; i < 4; i++) {
		int exp = max_exp - exps[i];
		if (exp >= 32) {
			mants[i] = 0;
		} else {
			mants[i] >>= exp;
		}
		if (signs[i]) {
			mants[i] = -mants[i];
		}
		mant_sum += mants[i];
	}

	uint32_t sign_sum = 0;
	if (mant_sum < 0) {
		sign_sum = 0x80000000;
		mant_sum = -mant_sum;
	}

	// Truncate off the extra bits now.  We want to zero them for rounding purposes.
	mant_sum >>= EXTRA_BITS;

	if (mant_sum == 0 || max_exp <= 0) {
		return 0.0f;
	}

	int8_t shift = (int8_t)clz32_nonzero(mant_sum) - 8;
	if (shift < 0) {
		// Round to even if we'd shift away a 0.5.
		const uint32_t round_bit = 1 << (-shift - 1);
		if ((mant_sum & round_bit) && (mant_sum & (round_bit << 1))) {
			mant_sum += round_bit;
			shift = (int8_t)clz32_nonzero(mant_sum) - 8;
		} else if ((mant_sum & round_bit) && (mant_sum & (round_bit - 1))) {
			mant_sum += round_bit;
			shift = (int8_t)clz32_nonzero(mant_sum) - 8;
		}
		mant_sum >>= -shift;
		max_exp += -shift;
	} else {
		mant_sum <<= shift;
		max_exp -= shift;
	}
	_dbg_assert_msg_((mant_sum & 0x00800000) != 0, "Mantissa wrong: %08x", mant_sum);

	if (max_exp >= 255) {
		max_exp = 255;
		mant_sum = 0;
	} else if (max_exp <= 0) {
		return 0.0f;
	}

	result.i = sign_sum | (max_exp << 23) | (mant_sum & 0x007FFFFF);
	return result.f;
}

#if defined(__SSE2__)

#include <emmintrin.h>

static inline __m128i mulhi32x4(__m128i a, __m128i b) {
	__m128i m02 = _mm_mul_epu32(a, b);
	__m128i m13 = _mm_mul_epu32(
		_mm_shuffle_epi32(a, _MM_SHUFFLE(3, 3, 1, 1)),
		_mm_shuffle_epi32(b, _MM_SHUFFLE(3, 3, 1, 1)));
	__m128i m=_mm_unpacklo_epi32(
		_mm_shuffle_epi32(m02, _MM_SHUFFLE(3, 2, 3, 1)),
		_mm_shuffle_epi32(m13, _MM_SHUFFLE(3, 2, 3, 1)));
	return m;
}

// Values of rounding_mode:
//   -1 - detect at runtime
//    0 - assume round-to-nearest-ties-to-even
//    1 - round yourself in integer math
template<int rounding_mode=-1>
static float vfpu_dot_sse2(const float a[4], const float b[4])
{
	static const int EXTRA_BITS = 2;

	bool is_default_rounding_mode = (rounding_mode == 0);
	if(rounding_mode == -1)
	{
		volatile float test05 = 5.9604644775390625e-08f;  // 0.5*2^-23
		volatile float test15 = 1.78813934326171875e-07f; // 1.5*2^-23
		const float res15 = 1.0000002384185791015625f;    // 1+2^-22
		test05 += 1.0f;
		test15 += 1.0f;
		is_default_rounding_mode = (test05 == 1.0f && test15 == res15);
	}
	__m128 A = _mm_loadu_ps(a);
	__m128 B = _mm_loadu_ps(b);
	// Extract exponents.
	__m128 exp_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7F800000));
	__m128 eA = _mm_and_ps(A, exp_mask);
	__m128 eB = _mm_and_ps(B, exp_mask);
	__m128i exps = _mm_srli_epi32(_mm_add_epi32(
		_mm_castps_si128(eA),
		_mm_castps_si128(eB)),23);
	// Find maximum exponent, stored as float32 in [1;2),
	// so we can use _mm_max_ps() with normal arguments.
	__m128 t = _mm_or_ps(_mm_castsi128_ps(exps), _mm_set1_ps(1.0f));
	t = _mm_max_ps(t, _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(t), _MM_SHUFFLE(2, 3, 0, 1))));
	t = _mm_max_ps(t, _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(t), _MM_SHUFFLE(1, 0, 3, 2))));
	t = _mm_max_ps(t, _mm_castsi128_ps(_mm_set1_epi32(0x3F80007F)));
	int32_t mexp = _mm_cvtsi128_si32(_mm_castps_si128(t)) & 511;
	// NOTE: mexp is doubly-biased, same for exps.
	int32_t max_exp = mexp - 127;
	// Fall back on anything weird.
	__m128 finiteA = _mm_sub_ps(A, A);
	__m128 finiteB = _mm_sub_ps(B, B);
	finiteA = _mm_cmpeq_ps(finiteA, finiteA);
	finiteB = _mm_cmpeq_ps(finiteB, finiteB);
	if(max_exp >= 255 || _mm_movemask_ps(_mm_and_ps(finiteA, finiteB)) != 15) return vfpu_dot_cpp(a, b);
	// Extract significands.
	__m128i mA = _mm_or_si128(_mm_and_si128(_mm_castps_si128(A),_mm_set1_epi32(0x007FFFFF)),_mm_set1_epi32(0x00800000));
	__m128i mB = _mm_or_si128(_mm_and_si128(_mm_castps_si128(B),_mm_set1_epi32(0x007FFFFF)),_mm_set1_epi32(0x00800000));
	// Multiply.
	// NOTE: vfpu_dot does multiplication as
	// ((x<<EXTRA_BITS)*(y<<EXTRA_BITS))>>(23+EXTRA_BITS),
	// here we do (x*y)>>(23-EXTRA_BITS-1),
	// which produces twice the result (neither expression
	// overflows in our case). We need that because our
	// variable-shift scheme (below) must shift by at least 1 bit.
	static const int s = 32-(23 - EXTRA_BITS - 1), s0 = s / 2,s1 = s - s0;
	// We compute ((x*y)>>shift) as
	// (((x*y)<<(32-shift))>>32), which we express as
	// (((x<<s0)*(y<<s1))>>32) (neither shift overflows).
	__m128i m = mulhi32x4(_mm_slli_epi32(mA, s0), _mm_slli_epi32(mB, s1));
	// Shift according to max_exp. Since SSE2 doesn't have
	// variable per-lane shifts, we multiply *again*,
	// specifically, x>>y turns into (x<<(1<<(32-y)))>>32.
	// We compute 1<<(32-y) using floating-point casts.
	// NOTE: the cast for 1<<31 produces the correct value,
	// since the _mm_cvttps_epi32 error code just happens
	// to be 0x80000000.
	// So (since we pre-multiplied m by 2), we need
	// (m>>1)>>(mexp-exps),
	// i.e. m>>(mexp+1-exps),
	// i.e. (m<<(32-(mexp+1-exps)))>>32,
	// i.e. (m<<(exps-(mexp-31)))>>32.
	__m128i amounts = _mm_sub_epi32(exps, _mm_set1_epi32(mexp - 31));
	// Clamp by 0. Both zero and negative amounts produce zero,
	// since they correspond to right-shifting by 32 or more bits.
	amounts = _mm_and_si128(amounts, _mm_cmpgt_epi32(amounts, _mm_set1_epi32(0)));
	// Set up multipliers.
	__m128i bits = _mm_add_epi32(_mm_set1_epi32(0x3F800000), _mm_slli_epi32(amounts, 23));
	__m128i muls = _mm_cvttps_epi32(_mm_castsi128_ps(bits));
	m = mulhi32x4(m, muls);
	// Extract signs.
	__m128i signs = _mm_cmpgt_epi32(
			_mm_set1_epi32(0),
			_mm_xor_si128(_mm_castps_si128(A), _mm_castps_si128(B)));
	// Apply signs to m.
	m = _mm_sub_epi32(_mm_xor_si128(m, signs), signs);
	// Horizontal sum.
	// See https://stackoverflow.com/questions/6996764/fastest-way-to-do-horizontal-sse-vector-sum-or-other-reduction
	__m128i h64 = _mm_shuffle_epi32(m, _MM_SHUFFLE(1, 0, 3, 2));
	__m128i s64 = _mm_add_epi32(h64, m);
	__m128i h32 = _mm_shufflelo_epi16(s64, _MM_SHUFFLE(1, 0, 3, 2));
	__m128i s32 = _mm_add_epi32(s64, h32);
	int32_t mant_sum = _mm_cvtsi128_si32(s32);

	// The rest is scalar.
	uint32_t sign_sum = 0;
	if (mant_sum < 0) {
		sign_sum = 0x80000000;
		mant_sum = -mant_sum;
	}

	// Truncate off the extra bits now.  We want to zero them for rounding purposes.
	mant_sum >>= EXTRA_BITS;

	if (mant_sum == 0 || max_exp <= 0) {
		return 0.0f;
	}

	if(is_default_rounding_mode)
	{
		float2int r;
		r.f = (float)mant_sum;
		mant_sum = (r.i & 0x007FFFFF) | 0x00800000;
		max_exp += (r.i >> 23) - 0x96;
	}
	else
	{
		int8_t shift = (int8_t)clz32_nonzero(mant_sum) - 8;
		if (shift < 0) {
			// Round to even if we'd shift away a 0.5.
			const uint32_t round_bit = 1 << (-shift - 1);
			if ((mant_sum & round_bit) && (mant_sum & (round_bit << 1))) {
				mant_sum += round_bit;
				shift = (int8_t)clz32_nonzero(mant_sum) - 8;
			} else if ((mant_sum & round_bit) && (mant_sum & (round_bit - 1))) {
				mant_sum += round_bit;
				shift = (int8_t)clz32_nonzero(mant_sum) - 8;
			}
			mant_sum >>= -shift;
			max_exp += -shift;
		} else {
			mant_sum <<= shift;
			max_exp -= shift;
		}
		_dbg_assert_msg_((mant_sum & 0x00800000) != 0, "Mantissa wrong: %08x", mant_sum);
	}

	if (max_exp >= 255) {
		max_exp = 255;
		mant_sum = 0;
	} else if (max_exp <= 0) {
		return 0.0f;
	}

	float2int result;
	result.i = sign_sum | (max_exp << 23) | (mant_sum & 0x007FFFFF);
	return result.f;
}

#endif // defined(__SSE2__)

float vfpu_dot(const float a[4], const float b[4]) {
#if defined(__SSE2__)
	return vfpu_dot_sse2(a, b);
#else
	return vfpu_dot_cpp(a, b);
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

// Lookup tables.
// Note: these are never unloaded, and stay till program termination.
static uint32_t (*vfpu_sin_lut8192)=nullptr;
static  int8_t  (*vfpu_sin_lut_delta)[2]=nullptr;
static  int16_t (*vfpu_sin_lut_interval_delta)=nullptr;
static uint8_t  (*vfpu_sin_lut_exceptions)=nullptr;

static  int8_t  (*vfpu_sqrt_lut)[2]=nullptr;

static  int8_t  (*vfpu_rsqrt_lut)[2]=nullptr;

static uint32_t (*vfpu_exp2_lut65536)=nullptr;
static uint8_t  (*vfpu_exp2_lut)[2]=nullptr;

static uint32_t (*vfpu_log2_lut65536)=nullptr;
static uint32_t (*vfpu_log2_lut65536_quadratic)=nullptr;
static uint8_t  (*vfpu_log2_lut)[131072][2]=nullptr;

static  int32_t (*vfpu_asin_lut65536)[3]=nullptr;
static uint64_t (*vfpu_asin_lut_deltas)=nullptr;
static uint16_t (*vfpu_asin_lut_indices)=nullptr;

static  int8_t  (*vfpu_rcp_lut)[2]=nullptr;

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
		else significand <<= (exponent - 0x7Fu);
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
		else significand <<= (exponent - 0x7Fu);
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

// Integer square root of 2^23*x (rounded to zero).
// Input is in 2^23 <= x < 2^25, and representable in float.
static inline uint32_t isqrt23(uint32_t x) {
#if 0
	// Reference version.
	int dir=fesetround(FE_TOWARDZERO);
	uint32_t ret=uint32_t(int32_t(sqrtf(float(int32_t(x)) * 8388608.0f)));
	fesetround(dir);
	return ret;
#elif 1
	// Double version.
	// Verified to produce correct result for all valid inputs,
	// in all rounding modes, both in double and double-extended (x87)
	// precision.
	// Requires correctly-rounded sqrt (which on any IEEE-754 system
	// it should be).
	return uint32_t(int32_t(sqrt(double(x) * 8388608.0)));
#else
	// Pure integer version, if you don't like floating point.
	// Based on code from Hacker's Delight. See isqrt4 in
	// https://github.com/hcs0/Hackers-Delight/blob/master/isqrt.c.txt
	// Relatively slow.
	uint64_t t=uint64_t(x) << 23, m, y, b;
	m=0x4000000000000000ull;
	y=0;
	while(m != 0) // Do 32 times.
	{
		b=y | m;
		y=y >> 1;
		if(t >= b)
		{
			t = t - b;
			y = y | m;
		}
		m = m >> 2;
	}
	return y;
#endif
}

// Returns floating-point bitpattern.
static inline uint32_t vfpu_sqrt_fixed(uint32_t x) {
	// Endpoints of input.
	uint32_t lo  =(x +  0u) & -64u;
	uint32_t hi = (x + 64u) & -64u;
	// Convert input to 9.23 fixed-point.
	lo = (lo >= 0x00400000u ? 4u * lo : 0x00800000u + 2u * lo);
	hi = (hi >= 0x00400000u ? 4u * hi : 0x00800000u + 2u * hi);
	// Estimate endpoints of output.
	uint32_t A = 0x3F000000u + isqrt23(lo);
	uint32_t B = 0x3F000000u + isqrt23(hi);
	// Apply deltas, and increase the working precision.
	uint64_t a = (uint64_t(A) << 4) + uint64_t(vfpu_sqrt_lut[x >> 6][0]);
	uint64_t b = (uint64_t(B) << 4) + uint64_t(vfpu_sqrt_lut[x >> 6][1]);
	uint32_t ret = uint32_t((a + (((b - a) * (x & 63)) >> 6)) >> 4);
	// Truncate lower 2 bits.
	ret &= -4u;
	return ret;
}

float vfpu_sqrt(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_sqrt_lut, 262144);
	if (!loaded)
		return vfpu_sqrt_fallback(x);
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
	bits = vfpu_sqrt_fixed(index);
	bits += uint32_t(exponent >> 1) << 23;
	memcpy(&x, &bits, sizeof(bits));
	return x;
}

// Returns floor(2^33/sqrt(x)), for 2^22 <= x < 2^24.
static inline uint32_t rsqrt_floor22(uint32_t x) {
#if 1
	// Verified correct in all rounding directions,
	// by exhaustive search.
	return uint32_t(8589934592.0 / sqrt(double(x))); // 0x1p33
#else
	// Pure integer version, if you don't like floating point.
	// Based on code from Hacker's Delight. See isqrt4 in
	// https://github.com/hcs0/Hackers-Delight/blob/master/isqrt.c.txt
	// Relatively slow.
	uint64_t t=uint64_t(x) << 22, m, y, b;
	m = 0x4000000000000000ull;
	y = 0;
	while(m != 0) // Do 32 times.
	{
		b = y | m;
		y = y >> 1;
		if(t >= b)
		{
			t = t - b;
			y = y | m;
		}
		m = m >> 2;
	}
	y = (1ull << 44) / y;
	// Decrement if y > floor(2^33 / sqrt(x)).
	// This hack works because exhaustive
	// search (on [2^22;2^24]) says it does.
	if((y * y >> 3) * x > (1ull << 63) - 3ull * (((y & 7) == 6) << 21)) --y;
	return uint32_t(y);
#endif
}

// Returns floating-point bitpattern.
static inline uint32_t vfpu_rsqrt_fixed(uint32_t x) {
	// Endpoints of input.
	uint32_t lo = (x +  0u) & -64u;
	uint32_t hi = (x + 64u) & -64u;
	// Convert input to 10.22 fixed-point.
	lo = (lo >= 0x00400000u ? 2u * lo : 0x00400000u + lo);
	hi = (hi >= 0x00400000u ? 2u * hi : 0x00400000u + hi);
	// Estimate endpoints of output.
	uint32_t A = 0x3E800000u + 4u * rsqrt_floor22(lo);
	uint32_t B = 0x3E800000u + 4u * rsqrt_floor22(hi);
	// Apply deltas, and increase the working precision.
	uint64_t a = (uint64_t(A) << 4) + uint64_t(vfpu_rsqrt_lut[x >> 6][0]);
	uint64_t b = (uint64_t(B) << 4) + uint64_t(vfpu_rsqrt_lut[x >> 6][1]);
	// Evaluate via lerp.
	uint32_t ret = uint32_t((a + (((b - a) * (x & 63)) >> 6)) >> 4);
	// Truncate lower 2 bits.
	ret &= -4u;
	return ret;
}

float vfpu_rsqrt(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_rsqrt_lut, 262144);
	if (!loaded)
		return vfpu_rsqrt_fallback(x);
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
	bits = vfpu_rsqrt_fixed(index);
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

static inline uint32_t vfpu_exp2_approx(uint32_t x) {
	if(x == 0x00800000u) return 0x00800000u;
	uint32_t a=vfpu_exp2_lut65536[x >> 16];
	x &= 0x0000FFFFu;
	uint32_t b = uint32_t(((2977151143ull * x) >> 23) + ((1032119999ull * (x * x)) >> 46));
	return (a + uint32_t((uint64_t(a + (1u << 23)) * uint64_t(b)) >> 32)) & -4u;
}

static inline uint32_t vfpu_exp2_fixed(uint32_t x) {
	if(x == 0u) return 0u;
	if(x == 0x00800000u) return 0x00800000u;
	uint32_t A = vfpu_exp2_approx((x     ) & -64u);
	uint32_t B = vfpu_exp2_approx((x + 64) & -64u);
	uint64_t a = (A<<4)+vfpu_exp2_lut[x >> 6][0]-64u;
	uint64_t b = (B<<4)+vfpu_exp2_lut[x >> 6][1]-64u;
	uint32_t y = uint32_t((a + (((b - a) * (x & 63)) >> 6)) >> 4);
	y &= -4u;
	return y;
}

float vfpu_exp2(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_exp2_lut65536,    512)&&
		LOAD_TABLE(vfpu_exp2_lut,      262144);
	if (!loaded)
		return vfpu_exp2_fallback(x);
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
	bits = int32_t(0x3F800000) + (bits & int32_t(0xFF800000)) + int32_t(vfpu_exp2_fixed(bits & int32_t(0x007FFFFF)));
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

static inline uint32_t vfpu_rcp_approx(uint32_t i) {
	return 0x3E800000u + (uint32_t((1ull << 47) / ((1ull << 23) + i)) & -4u);
}

float vfpu_rcp(float x) {
	static bool loaded =
		LOAD_TABLE(vfpu_rcp_lut, 262144);
	if (!loaded)
		return vfpu_rcp_fallback(x);
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
	uint32_t A = vfpu_rcp_approx((i	 ) & -64u);
	uint32_t B = vfpu_rcp_approx((i + 64) & -64u);
	uint64_t a = (uint64_t(A) << 6) + uint64_t(vfpu_rcp_lut[i >> 6][0]) * 4u;
	uint64_t b = (uint64_t(B) << 6) + uint64_t(vfpu_rcp_lut[i >> 6][1]) * 4u;
	uint32_t v = uint32_t((a+(((b-a)*(i&63))>>6))>>6);
	v &= -4u;
	bits = s + (0x3F800000u - e) + v;
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
	LOAD_TABLE(vfpu_exp2_lut65536          ,     512); 
	LOAD_TABLE(vfpu_exp2_lut               ,  262144); 
	LOAD_TABLE(vfpu_log2_lut65536          ,     516); 
	LOAD_TABLE(vfpu_log2_lut65536_quadratic,     512); 
	LOAD_TABLE(vfpu_log2_lut               , 2097152); 
	LOAD_TABLE(vfpu_rcp_lut                ,  262144); 
	LOAD_TABLE(vfpu_rsqrt_lut              ,  262144); 
	LOAD_TABLE(vfpu_sin_lut8192            ,    4100); 
	LOAD_TABLE(vfpu_sin_lut_delta          ,  262144); 
	LOAD_TABLE(vfpu_sin_lut_exceptions     ,   86938); 
	LOAD_TABLE(vfpu_sin_lut_interval_delta ,  131074); 
	LOAD_TABLE(vfpu_sqrt_lut               ,  262144); 
#endif
}
