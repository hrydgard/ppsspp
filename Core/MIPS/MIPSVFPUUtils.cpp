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
#include "Core/Reporting.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#define V(i)   (currentMIPS->v[voffset[i]])
#define VI(i)  (currentMIPS->vi[voffset[i]])

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
	default: _assert_msg_(JIT, 0, "%s: Bad vector size", __FUNCTION__);
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
	default: _assert_msg_(JIT, 0, "%s: Bad matrix size", __FUNCTION__);
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
			ERROR_LOG(JIT, "GetMatrixName: Invalid row %i or column %i for size %i", row, column, msize);
		}
		break;

	case M_3x3:
		if (row & ~2) {
			ERROR_LOG(JIT, "GetMatrixName: Invalid row %i for size %i", row, msize);
		}
		if (column & ~2) {
			ERROR_LOG(JIT, "GetMatrixName: Invalid col %i for size %i", column, msize);
		}
		name |= (row << 6) | column;
		break;

	case M_2x2:
		if (row & ~2) {
			ERROR_LOG(JIT, "GetMatrixName: Invalid row %i for size %i", row, msize);
		}
		if (column & ~2) {
			ERROR_LOG(JIT, "GetMatrixName: Invalid col %i for size %i", column, msize);
		}
		name |= (row << 5) | column;
		break;

	default: _assert_msg_(JIT, 0, "%s: Bad matrix size", __FUNCTION__);
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
	int row = 0;
	int length = 0;

	switch (size) {
	case V_Single: rd[0] = V(reg); return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	default: _assert_msg_(JIT, 0, "%s: Bad vector size", __FUNCTION__);
	}
	int transpose = (reg>>5) & 1;
	const int mtx = (reg >> 2) & 7;
	const int col = reg & 3;

	if (transpose) {
		const int base = mtx * 4 + col * 32;
		for (int i = 0; i < length; i++)
			rd[i] = V(base + ((row+i)&3));
	} else {
		const int base = mtx * 4 + col;
		for (int i = 0; i < length; i++)
			rd[i] = V(base + ((row+i)&3)*32);
	}
}

void WriteVector(const float *rd, VectorSize size, int reg) {
	if (size == V_Single) {
		// Optimize the common case.
		if (!currentMIPS->VfpuWriteMask(0)) {
			V(reg) = rd[0];
		}
		return;
	}

	const int mtx = (reg>>2)&7;
	const int col = reg & 3;
	int transpose = (reg>>5)&1;
	int row = 0;
	int length = 0;

	switch (size) {
	case V_Single: _dbg_assert_(JIT, 0); return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	default: _assert_msg_(JIT, 0, "%s: Bad vector size", __FUNCTION__);
	}

	if (currentMIPS->VfpuWriteMask() == 0) {
		if (transpose) {
			const int base = mtx * 4 + col * 32;
			for (int i = 0; i < length; i++)
				V(base + ((row+i)&3)) = rd[i];
		} else {
			const int base = mtx * 4 + col;
			for (int i = 0; i < length; i++)
				V(base + ((row+i)&3)*32) = rd[i];
		}
	} else {
		for (int i = 0; i < length; i++) {
			if (!currentMIPS->VfpuWriteMask(i)) {
				int index = mtx * 4;
				if (transpose)
					index += ((row+i)&3) + col*32;
				else
					index += col + ((row+i)&3)*32;
				V(index) = rd[i];
			}
		}
	}
}

u32 VFPURewritePrefix(int ctrl, u32 remove, u32 add) {
	u32 prefix = currentMIPS->vfpuCtrl[ctrl];
	return (prefix & ~remove) | add;
}

void ReadMatrix(float *rd, MatrixSize size, int reg) {
	int mtx = (reg >> 2) & 7;
	int col = reg & 3;

	int row = 0;
	int side = 0;
	int transpose = (reg >> 5) & 1;

	switch (size) {
	case M_1x1: transpose = 0; row = (reg >> 5) & 3; side = 1; break;
	case M_2x2: row = (reg >> 5) & 2; side = 2; break;
	case M_3x3: row = (reg >> 6) & 1; side = 3; break;
	case M_4x4: row = (reg >> 5) & 2; side = 4; break;
	default: _assert_msg_(JIT, 0, "%s: Bad matrix size", __FUNCTION__);
	}

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

	int row = 0;
	int side = 0;
	int transpose = (reg >> 5) & 1;

	switch (size) {
	case M_1x1: transpose = 0; row = (reg >> 5) & 3; side = 1; break;
	case M_2x2: row = (reg >> 5) & 2; side = 2; break;
	case M_3x3: row = (reg >> 6) & 1; side = 3; break;
	case M_4x4: row = (reg >> 5) & 2; side = 4; break;
	default: _assert_msg_(JIT, 0, "%s: Bad matrix size", __FUNCTION__);
	}

	if (currentMIPS->VfpuWriteMask() != 0) {
		ERROR_LOG_REPORT(CPU, "Write mask used with vfpu matrix instruction.");
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

int GetNumVectorElements(VectorSize sz) {
	switch (sz) {
		case V_Single: return 1;
		case V_Pair:   return 2;
		case V_Triple: return 3;
		case V_Quad:   return 4;
		default:       return 0;
	}
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
	_assert_msg_(JIT, res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
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
	_assert_msg_(JIT, res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

VectorSize GetVecSizeSafe(MIPSOpcode op) {
	int a = (op >> 7) & 1;
	int b = (op >> 15) & 1;
	a += (b << 1);
	switch (a) {
	case 0: return V_Single;
	case 1: return V_Pair;
	case 2: return V_Triple;
	case 3: return V_Quad;
	default: return V_Invalid;
	}
}

VectorSize GetVecSize(MIPSOpcode op) {
	VectorSize res = GetVecSizeSafe(op);
	_assert_msg_(JIT, res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
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
	_assert_msg_(JIT, res != V_Invalid, "%s: Bad vector size", __FUNCTION__);
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
	_assert_msg_(JIT, res != M_Invalid, "%s: Bad vector size", __FUNCTION__);
	return res;
}

MatrixSize GetMtxSizeSafe(MIPSOpcode op) {
	int a = (op >> 7) & 1;
	int b = (op >> 15) & 1;
	a += (b << 1);
	switch (a) {
	case 0: return M_1x1;  // This happens in disassembly of junk, but has predictable behavior.
	case 1: return M_2x2;
	case 2: return M_3x3;
	case 3: return M_4x4;
	default: return M_Invalid;
	}
}

MatrixSize GetMtxSize(MIPSOpcode op) {
	MatrixSize res = GetMtxSizeSafe(op);
	_assert_msg_(JIT, res != M_Invalid, "%s: Bad matrix size", __FUNCTION__);
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
	_assert_msg_(JIT, res != V_Invalid, "%s: Bad matrix size", __FUNCTION__);
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
	_assert_msg_(JIT, res != 0, "%s: Bad matrix size", __FUNCTION__);
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

const char *GetVectorNotation(int reg, VectorSize size)
{
	static char hej[4][16];
	static int yo = 0; yo++; yo &= 3;

	int mtx = (reg>>2)&7;
	int col = reg&3;
	int row = 0;
	int transpose = (reg>>5)&1;
	char c;
	switch (size)
	{
	case V_Single:  transpose=0; c='S'; row=(reg>>5)&3; break;
	case V_Pair:    c='C'; row=(reg>>5)&2; break;
	case V_Triple:	c='C'; row=(reg>>6)&1; break;
	case V_Quad:    c='C'; row=(reg>>5)&2; break;
	default:        c='?'; break;
	}
	if (transpose && c == 'C') c='R';
	if (transpose)
		sprintf(hej[yo],"%c%i%i%i",c,mtx,row,col);
	else
		sprintf(hej[yo],"%c%i%i%i",c,mtx,col,row);
	return hej[yo];
}

const char *GetMatrixNotation(int reg, MatrixSize size)
{
  static char hej[4][16];
  static int yo=0;yo++;yo&=3;
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
    sprintf(hej[yo],"%c%i%i%i",c,mtx,row,col);
  else
    sprintf(hej[yo],"%c%i%i%i",c,mtx,col,row);
  return hej[yo];
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
	union float2int {
		unsigned int i;
		float f;
	} float2int;

	unsigned short float16 = l;
	unsigned int sign = (float16 >> VFPU_SH_FLOAT16_SIGN) & VFPU_MASK_FLOAT16_SIGN;
	int exponent = (float16 >> VFPU_SH_FLOAT16_EXP) & VFPU_MASK_FLOAT16_EXP;
	unsigned int fraction = float16 & VFPU_MASK_FLOAT16_FRAC;

	float f;
	if (exponent == VFPU_FLOAT16_EXP_MAX)
	{
		float2int.i = sign << 31;
		float2int.i |= 255 << 23;
		float2int.i |= fraction;
		f = float2int.f;
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
		float2int.i = sign << 31;
		float2int.i |= (exponent + 112) << 23;
		float2int.i |= fraction << 13;
		f=float2int.f;
	}
	return f;
}

static uint32_t get_uexp(uint32_t x) {
	return (x >> 23) & 0xFF;
}

int32_t get_exp(uint32_t x) {
	return get_uexp(x) - 127;
}

static int32_t get_mant(uint32_t x) {
	// Note: this returns the hidden 1.
	return (x & 0x007FFFFF) | 0x00800000;
}

static int32_t get_sign(uint32_t x) {
	return x & 0x80000000;
}

float vfpu_dot(float a[4], float b[4]) {
	static const int EXTRA_BITS = 2;
	union float2int {
		uint32_t i;
		float f;
	};
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
	_dbg_assert_msg_(JIT, (mant_sum & 0x00800000) != 0, "Mantissa wrong: %08x", mant_sum);

	if (max_exp >= 255) {
		max_exp = 255;
		mant_sum = 0;
	} else if (max_exp <= 0) {
		return 0.0f;
	}

	result.i = sign_sum | (max_exp << 23) | (mant_sum & 0x007FFFFF);
	return result.f;
}

// TODO: This is still not completely accurate compared to the PSP's vsqrt.
float vfpu_sqrt(float a) {
	union float2int {
		uint32_t u;
		float f;
	};
	float2int val;
	val.f = a;

	if ((val.u & 0xff800000) == 0x7f800000) {
		if ((val.u & 0x007fffff) != 0) {
			val.u = 0x7f800001;
		}
		return val.f;
	}
	if ((val.u & 0x7f800000) == 0) {
		// Kill any sign.
		val.u = 0;
		return val.f;
	}
	if (val.u & 0x80000000) {
		val.u = 0x7f800001;
		return val.f;
	}

	int k = get_exp(val.u);
	uint32_t sp = get_mant(val.u);
	int less_bits = k & 1;
	k >>= 1;

	uint32_t z = 0x00C00000 >> less_bits;
	int64_t halfsp = sp >> 1;
	halfsp <<= 23 - less_bits;
	for (int i = 0; i < 6; ++i) {
		z = (z >> 1) + (uint32_t)(halfsp / z);
	}

	val.u = ((k + 127) << 23) | ((z << less_bits) & 0x007FFFFF);
	// The lower two bits never end up set on the PSP, it seems like.
	val.u &= 0xFFFFFFFC;

	return val.f;
}

static inline uint32_t mant_mul(uint32_t a, uint32_t b) {
	uint64_t m = (uint64_t)a * (uint64_t)b;
	if (m & 0x007FFFFF) {
		m += 0x01437000;
	}
	return m >> 23;
}

float vfpu_rsqrt(float a) {
	union float2int {
		uint32_t u;
		float f;
	};
	float2int val;
	val.f = a;

	if (val.u == 0x7f800000) {
		return 0.0f;
	}
	if ((val.u & 0x7fffffff) > 0x7f800000) {
		val.u = (val.u & 0x80000000) | 0x7f800001;
		return val.f;
	}
	if ((val.u & 0x7f800000) == 0) {
		val.u = (val.u & 0x80000000) | 0x7f800000;
		return val.f;
	}
	if (val.u & 0x80000000) {
		val.u = 0xff800001;
		return val.f;
	}

	int k = get_exp(val.u);
	uint32_t sp = get_mant(val.u);
	int less_bits = k & 1;
	k = -(k >> 1);

	uint32_t z = 0x00800000 >> less_bits;
	uint32_t halfsp = sp >> (1 + less_bits);
	for (int i = 0; i < 6; ++i) {
		uint32_t oldz = z;
		uint32_t zsq = mant_mul(z, z);
		uint32_t correction = 0x00C00000 - mant_mul(halfsp, zsq);
		z = mant_mul(z, correction);
	}

	int8_t shift = (int8_t)clz32_nonzero(z) - 8 + less_bits;
	if (shift < 1) {
		z >>= -shift;
		k += -shift;
	} else if (shift > 0) {
		z <<= shift;
		k -= shift;
	}

	z >>= less_bits;

	val.u = ((k + 127) << 23) | (z & 0x007FFFFF);
	val.u &= 0xFFFFFFFC;

	return val.f;
}
