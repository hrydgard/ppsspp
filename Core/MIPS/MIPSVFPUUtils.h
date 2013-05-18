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



#define _VD (op & 0x7F)
#define _VS ((op>>8) & 0x7F)
#define _VT ((op>>16) & 0x7F)

inline int Xpose(int v)
{
	return v^0x20;
}

#define VFPU_FLOAT16_EXP_MAX	0x1f
#define VFPU_SH_FLOAT16_SIGN	15
#define VFPU_MASK_FLOAT16_SIGN	0x1
#define VFPU_SH_FLOAT16_EXP	10
#define VFPU_MASK_FLOAT16_EXP	0x1f
#define VFPU_SH_FLOAT16_FRAC	0
#define VFPU_MASK_FLOAT16_FRAC	0x3ff

enum VectorSize
{
	V_Single,
	V_Pair,
	V_Triple,
	V_Quad,
};

enum MatrixSize
{
	M_2x2,
	M_3x3,
	M_4x4,
};

void ReadMatrix(float *rd, MatrixSize size, int reg);
void WriteMatrix(const float *rs, MatrixSize size, int reg);

void WriteVector(const float *rs, VectorSize N, int reg);
void ReadVector(float *rd, VectorSize N, int reg);

void GetVectorRegs(u8 regs[4], VectorSize N, int vectorReg);
void GetMatrixRegs(u8 regs[16], MatrixSize N, int matrixReg);

VectorSize GetVecSize(u32 op);
MatrixSize GetMtxSize(u32 op);
VectorSize GetHalfVectorSize(VectorSize sz);
int GetNumVectorElements(VectorSize sz);
int GetMatrixSide(MatrixSize sz);
const char *GetVectorNotation(int reg, VectorSize size);
const char *GetMatrixNotation(int reg, MatrixSize size);

float Float16ToFloat32(unsigned short l);
