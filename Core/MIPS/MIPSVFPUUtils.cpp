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

#include "Core/CPU.h"
#include "Core/Reporting.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#include <limits>

#define V(i)   (currentMIPS->v[i])
#define VI(i)   (*(u32*)(&(currentMIPS->v[i])))

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

	switch (N) {
	case M_2x2: row = (matrixReg>>5)&2; side = 2; break;
	case M_3x3: row = (matrixReg>>6)&1; side = 3; break;
	case M_4x4: row = (matrixReg>>5)&2; side = 4; break;
	}

	int transpose = (matrixReg>>5) & 1;

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

void ReadVector(float *rd, VectorSize size, int reg) {
	const int mtx = (reg >> 2) & 7;
	const int col = reg & 3;
	int row = 0;
	int length = 0;
	int transpose = (reg>>5) & 1;

	switch (size) {
	case V_Single: transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	}

	u32 *rdu = (u32 *)rd;
	if (transpose) {
		const int base = mtx * 4 + col * 32;
		for (int i = 0; i < length; i++)
			rdu[i] = VI(base + ((row+i)&3));
	} else {
		const int base = mtx * 4 + col;
		for (int i = 0; i < length; i++)
			rdu[i] = VI(base + ((row+i)&3)*32);
	}
}

void WriteVector(const float *rd, VectorSize size, int reg) {
	const int mtx = (reg>>2)&7;
	const int col = reg & 3;
	int row = 0;
	int length = 0;
	int transpose = (reg>>5)&1;

	switch (size) {
	case V_Single: transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	}

	u32 *rdu = (u32 *)rd;
	if (currentMIPS->VfpuWriteMask() == 0) {
		if (transpose) {
			const int base = mtx * 4 + col * 32;
			for (int i = 0; i < length; i++)
				VI(base + ((row+i)&3)) = rdu[i];
		} else {
			const int base = mtx * 4 + col;
			for (int i = 0; i < length; i++)
				VI(base + ((row+i)&3)*32) = rdu[i];
		}
	} else {
		for (int i = 0; i < length; i++) {
			if (!currentMIPS->VfpuWriteMask(i)) {
				int index = mtx * 4;
				if (transpose)
					index += ((row+i)&3) + col*32;
				else
					index += col + ((row+i)&3)*32;
				VI(index) = rdu[i];
			}
		}
	}
}

void ReadMatrix(float *rd, MatrixSize size, int reg) {
	int mtx = (reg >> 2) & 7;
	int col = reg & 3;

	int row = 0;
	int side = 0;

	switch (size) {
	case M_2x2: row = (reg>>5)&2; side = 2; break;
	case M_3x3: row = (reg>>6)&1; side = 3; break;
	case M_4x4: row = (reg>>5)&2; side = 4; break;
	}

	int transpose = (reg>>5) & 1;

	for (int i = 0; i < side; i++) {
		for (int j = 0; j < side; j++) {
      int index = mtx * 4;
			if (transpose)
        index += ((row+i)&3) + ((col+j)&3)*32;
      else
        index += ((col+j)&3) + ((row+i)&3)*32;
      rd[j*4 + i] = V(index);
		}
	}
}

void WriteMatrix(const float *rd, MatrixSize size, int reg) {
	int mtx = (reg>>2)&7;
	int col = reg&3;

	int row = 0;
	int side = 0;

	switch (size) {
	case M_2x2: row = (reg>>5)&2; side = 2; break;
	case M_3x3: row = (reg>>6)&1; side = 3; break;
	case M_4x4: row = (reg>>5)&2; side = 4; break;
	}

	int transpose = (reg>>5)&1;
	if (currentMIPS->VfpuWriteMask() != 0) {
		ERROR_LOG_REPORT(CPU, "Write mask used with vfpu matrix instruction.");
	}

	for (int i=0; i<side; i++) {
		for (int j=0; j<side; j++) {
			// Hm, I wonder if this should affect matrices at all.
			if (j != side -1 || !currentMIPS->VfpuWriteMask(i))
			{
				int index = mtx * 4;
				if (transpose)
					index += ((row+i)&3) + ((col+j)&3)*32;
				else
					index += ((col+j)&3) + ((row+i)&3)*32;
				V(index) = rd[j*4+i];
			}
		}
	}
}


int GetNumVectorElements(VectorSize sz)
{
	switch (sz)
	{
		case V_Single: return 1;
		case V_Pair:   return 2;
		case V_Triple: return 3;
		case V_Quad:   return 4;
		default:       return 0;
	}
}

VectorSize GetHalfVectorSize(VectorSize sz)
{
	switch (sz)
	{
	case V_Pair: return V_Single;
	case V_Quad: return V_Pair;
	default:
		return V_Single;
	}
}

VectorSize GetVecSize(u32 op)
{
	int a = (op>>7)&1;
	int b = (op>>15)&1;
	a += (b<<1);
	switch (a)
	{
		case 0: return V_Single;
		case 1: return V_Pair;
		case 2: return V_Triple;
		case 3: return V_Quad;
		default: return V_Quad;
	}
}

MatrixSize GetMtxSize(u32 op)
{
	int a = (op>>7)&1;
	int b = (op>>15)&1;
	a += (b<<1);
	switch (a)
	{
	case 0: ERROR_LOG_REPORT(CPU, "Unexpected matrix size 1x1."); return M_2x2;
	case 1: return M_2x2;
	case 2: return M_3x3;
	case 3: return M_4x4;
	default: return M_4x4;
	}
}

int GetMatrixSide(MatrixSize sz)
{
	switch (sz)
	{
	case M_2x2: return 2;
	case M_3x3: return 3;
	case M_4x4: return 4;
	default: return 0;
	}
}

const char *GetVectorNotation(int reg, VectorSize size)
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
	case V_Single:  transpose=0; c='S'; row=(reg>>5)&3; break;
	case V_Pair:    c='C'; row=(reg>>5)&2; break;
	case V_Triple:	c='C'; row=(reg>>6)&1; break;
	case V_Quad:    c='C'; row=(reg>>5)&2; break;
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
  }
  if (transpose && c=='M') c='E';
  sprintf(hej[yo],"%c%i%i%i",c,mtx,col,row);
  return hej[yo];
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

	float signf = (sign == 1) ? -1.0f : 1.0f;

	float f;
	if (exponent == VFPU_FLOAT16_EXP_MAX)
	{
		if (fraction == 0)
			f = std::numeric_limits<float>::infinity(); //(*info->fprintf_func) (info->stream, "%cInf", signchar);
		else
			f = std::numeric_limits<float>::quiet_NaN(); //(*info->fprintf_func) (info->stream, "%cNaN", signchar);
	}
	else if (exponent == 0 && fraction == 0)
	{
		f = 0.0f * signf;
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
