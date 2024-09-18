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

#include <cstring>
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/StringUtils.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDis.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define _FS   ((op>>11) & 0x1F)
#define _FT   ((op>>16) & 0x1F)
#define _FD   ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)


#define RN(i) (currentDebugMIPS->GetRegName(0, i).c_str())
#define FN(i) (currentDebugMIPS->GetRegName(1, i).c_str())
//#define VN(i) (currentDebugMIPS->GetRegName(2, i).c_str())


#define S_not(a,b,c) (a<<2)|(b)|(c<<5)
#define SgetA(v) (((v)>>2)&0x7)
#define SgetB(v) ((v)&3)
#define SgetC(v) (((v)>>5)&0x3)

#define HorizOff 32
#define VertOff 1
#define MtxOff 4

inline std::string VNStr(int v, VectorSize size) {
	static const char * const vfpuCtrlNames[VFPU_CTRL_MAX] = {
		"SPFX",
		"TPFX",
		"DPFX",
		"CC",
		"INF4",
		"RSV5",
		"RSV6",
		"REV",
		"RCX0",
		"RCX1",
		"RCX2",
		"RCX3",
		"RCX4",
		"RCX5",
		"RCX6",
		"RCX7",
	};
	if (size == V_Single && v >= 128 && v < 128 + VFPU_CTRL_MAX) {
		return vfpuCtrlNames[v - 128];
	} else if (size == V_Single && v == 255) {
		return "(interlock)";
	}

	return GetVectorNotation(v, size);
}

inline std::string MNStr(int v, MatrixSize size) {
	return GetMatrixNotation(v, size);
}

#define VN(v, s) (VNStr(v, s).c_str())
#define MN(v, s) (MNStr(v, s).c_str())

inline const char *VSuff(MIPSOpcode op)
{
	int a = (op>>7)&1;
	int b = (op>>15)&1;
	a+=(b<<1);
	switch (a)
	{
	case 0: return ".s";
	case 1: return ".p";
	case 2: return ".t";
	case 3: return ".q";
	default: return "%";
	}
}

namespace MIPSDis
{
	std::string SignedHex(int i);

	void Dis_SV(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int offset = SignExtend16ToS32(op & 0xFFFC);
		int vt = ((op>>16)&0x1f)|((op&3)<<5);
		int rs = (op>>21) & 0x1f;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s(%s)", name, VN(vt, V_Single), SignedHex(offset).c_str(), RN(rs));
	}

	void Dis_SVQ(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int offset = SignExtend16ToS32(op & 0xFFFC);
		int vt = (((op>>16)&0x1f))|((op&1)<<5);
		int rs = (op>>21) & 0x1f;
		const char *name = MIPSGetName(op);
		size_t outpos = 0;
		outpos += snprintf(out, outSize, "%s\t%s, %s(%s)", name, VN(vt, V_Quad), SignedHex(offset).c_str(), RN(rs));
		if ((op & 2) && outpos < outSize)
			truncate_cpy(out + outpos, outSize - outpos, ", wb");
	}

	void Dis_SVLRQ(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int offset = SignExtend16ToS32(op & 0xFFFC);
		int vt = (((op>>16)&0x1f))|((op&1)<<5);
		int rs = (op>>21) & 0x1f;
		int lr = (op>>1)&1;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s.q\t%s, %s(%s)", name, lr ? "r" : "l", VN(vt, V_Quad), SignedHex(offset).c_str(), RN(rs));
	}

	void Dis_Mftv(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int vr = op & 0xFF;
		int rt = _RT;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, vr > 127 ? "c" : "", RN(rt), VN(vr, V_Single));
	}

	void Dis_Vmfvc(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int vd = _VD;
		int vr = (op >> 8) & 0x7F;
		const char* name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s", name, VN(vd, V_Single), VN(vr + 128, V_Single));
	}

	void Dis_Vmtvc(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int vr = op & 0x7F;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s", name, VN(vs, V_Single), VN(vr + 128, V_Single));
	}

	void Dis_VPFXST(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int data = op & 0xFFFFF;
		const char *name = MIPSGetName(op);
		size_t outpos = snprintf(out, outSize, "%s\t[", name);

		static const char * const regnam[4] = {"X","Y","Z","W"};
		static const char * const constan[8] = {"0","1","2","1/2","3","1/3","1/4","1/6"};
		for (int i=0; i<4; i++)
		{
			int regnum = (data>>(i*2)) & 3;
			int abs		= (data>>(8+i)) & 1;
			int negate = (data>>(16+i)) & 1;
			int constants = (data>>(12+i)) & 1;
			if (negate && outpos < outSize)
				outpos += truncate_cpy(out + outpos, outSize - outpos, "-");
			if (abs && !constants && outpos < outSize)
				outpos += truncate_cpy(out + outpos, outSize - outpos, "|");
			if (!constants) {
				if (outpos < outSize)
					outpos += truncate_cpy(out + outpos, outSize - outpos, regnam[regnum]);
			} else {
				if (abs)
					regnum+=4;
				if (outpos < outSize)
					outpos += truncate_cpy(out + outpos, outSize - outpos, constan[regnum]);
			}
			if (abs && !constants && outpos < outSize)
				outpos += truncate_cpy(out + outpos, outSize - outpos, "|");
			if (i != 3 && outpos < outSize)
				outpos += truncate_cpy(out + outpos, outSize - outpos, ",");
		}

		if (outpos < outSize)
			outpos += truncate_cpy(out + outpos, outSize - outpos, "]");
	}

	void Dis_VPFXD(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int data = op & 0xFFFFF;
		const char *name = MIPSGetName(op);
		size_t outpos = snprintf(out, outSize, "%s\t[", name);

		static const char * const satNames[4] = {"", "0:1", "X", "-1:1"};
		for (int i=0; i<4; i++)
		{
			int sat = (data>>i*2)&3;
			int mask = (data>>(8+i))&1;
			if (sat && outpos < outSize)
				outpos += truncate_cpy(out + outpos, outSize - outpos, satNames[sat]);
			if (mask && outpos < outSize)
				outpos += truncate_cpy(out + outpos, outSize - outpos, "M");
			if (i < 4 - 1 && outpos < outSize)
				outpos += truncate_cpy(out + outpos, outSize - outpos, ",");
		}

		if (outpos < outSize)
			outpos += truncate_cpy(out + outpos, outSize - outpos, "]");
	}


	void Dis_Viim(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int vt = _VT;
		int imm = SignExtend16ToS32(op & 0xFFFF);
		const char *name = MIPSGetName(op);

		int type = (op >> 23) & 7;
		if (type == 6)
			snprintf(out, outSize, "%s\t%s, %i", name, VN(vt, V_Single), imm);
		else if (type == 7)
			snprintf(out, outSize, "%s\t%s, %f", name, VN(vt, V_Single), Float16ToFloat32((u16)imm));
		else
			snprintf(out, outSize, "%s\tARGH", name);
	}

	void Dis_Vcst(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int conNum = (op>>16) & 0x1f;
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		static const char * const constants[32] =
		{
			"(undef)",
			"MaxFloat",
			"Sqrt(2)",
			"Sqrt(1/2)",
			"2/Sqrt(PI)",
			"2/PI",
			"1/PI",
			"PI/4",
			"PI/2",
			"PI",
			"e",
			"Log2(e)",
			"Log10(e)",
			"ln(2)",
			"ln(10)",
			"2*PI",
			"PI/6",
			"Log10(2)",
			"Log2(10)",
			"Sqrt(3)/2"
		};
		const char *name = MIPSGetName(op);
		const char *c = constants[conNum];
		if (c==0) c = constants[0];
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd,sz), c);
	}


	void Dis_MatrixSet1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		MatrixSize sz = GetMtxSize(op);
		snprintf(out, outSize, "%s%s\t%s", name, VSuff(op), MN(vd, sz));
	}
	void Dis_MatrixSet2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		MatrixSize sz = GetMtxSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), MN(vd, sz), MN(vs,sz));
	}
	void Dis_MatrixSet3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		MatrixSize sz = GetMtxSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), MN(vd, sz), MN(vs,sz), MN(vt,sz));
	}

	void Dis_MatrixMult(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		MatrixSize sz = GetMtxSize(op);
		// TODO: Xpose?
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), MN(vd, sz), MN(Xpose(vs),sz), MN(vt,sz));
	}

	void Dis_Vmscl(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		MatrixSize sz = GetMtxSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), MN(vd, sz), MN(vs, sz), VN(vt, V_Single));
	}

	void Dis_VectorDot(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, V_Single), VN(vs,sz), VN(vt, sz));
	}

	void Dis_Vtfm(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		int ins = (op>>23) & 7;
		VectorSize sz = GetVecSize(op);
		MatrixSize msz = GetMtxSize(op);
		int n = GetNumVectorElements(sz);

		if (n == ins)
		{
			//homogenous
			snprintf(out, outSize, "vhtfm%i%s\t%s, %s, %s", n, VSuff(op), VN(vd, sz), MN(vs, msz), VN(vt, sz));
		}
		else if (n == ins+1)
		{
			snprintf(out, outSize, "vtfm%i%s\t%s, %s, %s", n, VSuff(op), VN(vd, sz), MN(vs, msz), VN(vt, sz));
		}
		else
		{
			truncate_cpy(out, outSize, "BADVTFM");
		}
	}

	void Dis_Vflush(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		truncate_cpy(out, outSize, "vflush");
	}

	void Dis_Vcrs(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vt = _VT;
		int vs = _VS;
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		if (sz != V_Triple)
		{
			truncate_cpy(out, outSize, "vcrs\tERROR");
		}
		else
			snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs, sz), VN(vt,sz));
	}


	void Dis_Vcmp(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vt = _VT;
		int vs = _VS;
		int cond = op&15;
		VectorSize sz = GetVecSize(op);
		const char *condNames[16] = {"FL","EQ","LT","LE","TR","NE","GE","GT","EZ","EN","EI","ES","NZ","NN","NI","NS"};
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), condNames[cond], VN(vs, sz), VN(vt,sz));
	}

	void Dis_Vcmov(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		int tf = (op >> 19)&3;
		int imm3 = (op>>16)&7;
		if (tf > 1)
		{
			snprintf(out, outSize, "%s\tARGH%i", name, tf);
			return;
		}
		if (imm3<6)
			snprintf(out, outSize, "%s%s%s\t%s, %s, CC[%i]", name, tf==0?"t":"f", VSuff(op), VN(vd, sz), VN(vs,sz), imm3);
		else if (imm3 == 6)
			snprintf(out, outSize, "%s%s%s\t%s, %s, CC[...]", name, tf==0?"t":"f", VSuff(op), VN(vd, sz), VN(vs,sz));
	}

	void Dis_Vfad(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, V_Single), VN(vs,sz));
	}

	void Dis_VScl(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs,sz), VN(vt, V_Single));
	}

	void Dis_VectorSet1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		snprintf(out, outSize, "%s%s\t%s", name, VSuff(op), VN(vd, sz));
	}
	void Dis_VectorSet2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, sz), VN(vs, sz));
	}
	void Dis_VectorSet3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs,sz), VN(vt, sz));
	}

	void Dis_VRot(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int vd = _VD;
		int vs = _VS;
		int imm = (op>>16) & 0x1f;
		bool negSin = (imm & 0x10) ? true : false;
		char c[5] = "0000";
		char temp[16]={""};
		if (((imm>>2)&3)==(imm&3))
		{
			for (int i=0; i<4; i++)
				c[i]='S';
		}
		c[(imm>>2) & 3] = 'S';
		c[imm&3] = 'C';
		VectorSize sz = GetVecSize(op);
		int numElems = GetNumVectorElements(sz);
		int pos = 0;
		temp[pos++] = '[';
		for (int i=0; i<numElems; i++)
		{
			if (c[i] == 'S' && negSin)
				temp[pos++] = '-';
			temp[pos++] = c[i];
			if (i != numElems-1)
				temp[pos++] = ',';
		}
		temp[pos++] = ']';
		temp[pos]=0;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs, V_Single),temp);
	}

	void Dis_CrossQuat(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		const char *name;
		switch (sz)
		{
		case V_Triple:
			name = "vcrsp";
			//Ah, a regular cross product.
			break;
		case V_Quad:
			name = "vqmul";
			//Ah, a quaternion multiplication.
			break;
		default:
			// invalid
			name = "???";
			break;
		}
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		snprintf(out, outSize, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs,sz), VN(vt, sz));
	}

	void Dis_Vbfy(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, sz), VN(vs, sz));
	}

	void Dis_Vf2i(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		int imm = (op>>16)&0x1f;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %i", name, VSuff(op), VN(vd, sz), VN(vs, sz), imm);
	}

	void Dis_Vs2i(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, sz), VN(vs, sz));
	}

	void Dis_Vi2x(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		VectorSize dsz = GetHalfVectorSizeSafe(sz);
		if (((op>>16)&3)==0)
			dsz = V_Single;

		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, dsz), VN(vs, sz));
	}

	void Dis_Vwbn(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);

		int vd = _VD;
		int vs = _VS;
		int imm = (int)((op >> 16) & 0xFF);
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s, %d", name, VSuff(op), VN(vd, sz), VN(vs, sz), imm);
	}

	void Dis_Vf2h(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		VectorSize dsz = GetHalfVectorSizeSafe(sz);
		if (((op>>16)&3)==0)
			dsz = V_Single;

		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, dsz), VN(vs, sz));
	}

	void Dis_Vh2f(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		VectorSize dsz = GetDoubleVectorSizeSafe(sz);

		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, dsz), VN(vs, sz));
	}

	void Dis_ColorConv(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);
		VectorSize dsz = GetHalfVectorSizeSafe(sz);

		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s, %s", name, VSuff(op), VN(vd, dsz), VN(vs, sz));
	}

	void Dis_Vrnds(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int vd = _VD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s", name, VSuff(op), VN(vd, V_Single));
	}

	void Dis_VrndX(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		VectorSize sz = GetVecSize(op);

		int vd = _VD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s%s\t%s", name, VSuff(op), VN(vd, sz));
	}

	void Dis_VBranch(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 off = pc;
		int imm = SignExtend16ToS32(op&0xFFFF) << 2;
		int imm3 = (op>>18)&7;
		off += imm + 4;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t->$%08x	(CC[%i])", name, off, imm3);
	}

}
