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

#include "../HLE/HLE.h"

#include "MIPS.h"
#include "MIPSDis.h"
#include "MIPSTables.h"
#include "MIPSDebugInterface.h"

#include "MIPSVFPUUtils.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)


#define RN(i) currentDebugMIPS->GetRegName(0,i)
#define FN(i) currentDebugMIPS->GetRegName(1,i)
//#define VN(i) currentDebugMIPS->GetRegName(2,i)


#define S_not(a,b,c) (a<<2)|(b)|(c<<5)
#define SgetA(v) (((v)>>2)&0x7)
#define SgetB(v) ((v)&3)
#define SgetC(v) (((v)>>5)&0x3)

#define HorizOff 32
#define VertOff 1
#define MtxOff 4

inline const char *VN(int v, VectorSize size)
{
	static const char *vfpuCtrlNames[VFPU_CTRL_MAX] = {
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

inline const char *MN(int v, MatrixSize size)
{
	return GetMatrixNotation(v, size);
}

inline const char *VSuff(u32 op)
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
	void Dis_SV(u32 op, char *out)
	{
		int offset = (signed short)(op&0xFFFC);
		int vt = ((op>>16)&0x1f)|((op&3)<<5);
		int rs = (op>>21) & 0x1f;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %d(%s)",name,VN(vt, V_Single),offset,RN(rs));
	}

	void Dis_SVQ(u32 op, char *out)
	{
		int offset = (signed short)(op&0xFFFC);
		int vt = (((op>>16)&0x1f))|((op&1)<<5);
		int rs = (op>>21) & 0x1f;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %d(%s)",name,VN(vt,V_Quad),offset,RN(rs));
		if (op & 2)
			strcat(out, ", wb");
	}

	void Dis_SVLRQ(u32 op, char *out)
	{
		int offset = (signed short)(op&0xFFFC);
		int vt = (((op>>16)&0x1f))|((op&1)<<5);
		int rs = (op>>21) & 0x1f;
		int lr = (op>>1)&1;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s%s.q\t%s, %d(%s)",name,lr?"r":"l",VN(vt,V_Quad),offset,RN(rs));
	}

	void Dis_Mftv(u32 op, char *out)
	{
		int vr = op & 0xFF;
		int rt = _RT;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s%s\t%s, %s",name,vr>127?"c":"", RN(rt), VN(vr, V_Single));
	}

	void Dis_VPFXST(u32 op, char *out)
	{
		int data = op & 0xFFFFF;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t",name);
		static const char *regnam[4] = {"X","Y","Z","W"};
		static const char *constan[8] = {"0","1","2","1/2","3","1/3","1/4","1/6"};
		for (int i=0; i<4; i++)
		{
			int regnum = (data>>(i*2)) & 3;
			int abs		= (data>>(8+i)) & 1;
			int negate = (data>>(16+i)) & 1;
			int constants = (data>>(12+i)) & 1;
			if (negate)
				strcat(out, "-");
			if (abs && !constants)
				strcat(out, "|");
			if (!constants)
			{
				strcat(out, regnam[regnum]);
			}
			else
			{
				if (abs)
					regnum+=4;
				strcat(out, constan[regnum]);
			}
			if (abs && !constants)
				strcat(out, "|");
			strcat(out, " ");
		}
	}

	void Dis_VPFXD(u32 op, char *out)
	{
		int data = op & 0xFFFFF;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t", name);
		static const char *satNames[4] = {"", "0-1", "X", "-1-1"};
		for (int i=0; i<4; i++)
		{
			int sat = (data>>i*2)&3;
			int mask = (data>>(8+i))&1;
			if (sat)
				strcat(out, satNames[sat]);
			if (mask)
				strcat(out, "M");
			strcat(out, " ");
		}
	}


	void Dis_Viim(u32 op, char *out)
	{
		int vt = _VT;
		int imm = op&0xFFFF;
		//V(vt) = (float)imm;
		const char *name = MIPSGetName(op);

		int type = (op >> 23) & 7;
		if (type == 6)
			sprintf(out, "%s\t%s, %i", name, VN(vt, V_Single), imm);
		else if (type == 7)
			sprintf(out, "%s\t%s, %f", name, VN(vt, V_Single), Float16ToFloat32((u16)imm));
		else
			sprintf(out, "ARGH");
	}

	void Dis_Vcst(u32 op, char *out)
	{
		int conNum = (op>>16) & 0x1f;
		int vd = _VD;
		static const char *constants[32] = 
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
		sprintf(out,"%s%s\t%s, %s",name,VSuff(op),VN(vd,V_Single), c);
	}


	void Dis_MatrixSet1(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		MatrixSize sz = GetMtxSize(op);
		sprintf(out, "%s%s\t%s",name,VSuff(op),MN(vd, sz));
	}
	void Dis_MatrixSet2(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		MatrixSize sz = GetMtxSize(op);
		sprintf(out, "%s%s\t%s, %s",name,VSuff(op),MN(vd, sz),MN(vs,sz));
	}
	void Dis_MatrixSet3(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		MatrixSize sz = GetMtxSize(op);
		sprintf(out, "%s%s\t%s, %s, %s",name,VSuff(op),MN(vd, sz),MN(vs,sz),MN(vt,sz));
	}

	void Dis_MatrixMult(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		MatrixSize sz = GetMtxSize(op);
		sprintf(out, "%s%s\t%s, %s, %s",name,VSuff(op),MN(vd, sz),MN(Xpose(vs),sz),MN(vt,sz));
	}

	void Dis_VectorDot(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		sprintf(out, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, V_Single), VN(vs,sz), VN(vt, sz));
	}

	void Dis_Vtfm(u32 op, char *out)
	{
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
			sprintf(out, "vhtfm%i%s\t%s, %s, %s", n, VSuff(op), VN(vd, sz), MN(vs, msz), VN(vt, sz));
		}
		else if (n == ins+1)
		{
			sprintf(out, "vtfm%i%s\t%s, %s, %s", n, VSuff(op), VN(vd, sz), MN(vs, msz), VN(vt, sz));
		}
		else
		{
			sprintf(out,"BADVTFM");
		}
	}

	void Dis_Vflush(u32 op, char *out)
	{
		sprintf(out,"vflush");
	}

	void Dis_Vcrs(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vt = _VT;
		int vs = _VS;
		int vd = _VS;
		VectorSize sz = GetVecSize(op);
		if (sz != V_Triple)
		{
			sprintf(out, "vcrs\tERROR");
		}
		else
			sprintf(out, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs, sz), VN(vt,sz));
	}


	void Dis_Vcmp(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vt = _VT;
		int vs = _VS;
		int cond = op&15;
		VectorSize sz = GetVecSize(op);
		const char *condNames[16] = {"FL","EQ","LT","LE","TR","NE","GE","GT","EZ","EN","EI","ES","NZ","NN","NI","NS"};
		sprintf(out, "%s%s\t%s, %s, %s", name, VSuff(op), condNames[cond], VN(vs, sz), VN(vt,sz));
	}

	void Dis_Vcmov(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		int tf = (op >> 19)&3;
		int imm3 = (op>>16)&7;
		if (tf > 1)
		{
			sprintf(out, "Vcmov\tARGH%i", tf);
			return;
		}
		if (imm3<6)
			sprintf(out, "%s%s%s\t%s, %s, CC[%i]", name, tf==0?"t":"f", VSuff(op), VN(vd, sz), VN(vs,sz), imm3);
		else if (imm3 == 6)
			sprintf(out, "%s%s%s\t%s, %s, CC[...]", name, tf==0?"t":"f", VSuff(op), VN(vd, sz), VN(vs,sz));

	}

	void Dis_Vfad(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		sprintf(out, "%s%s\t%s, %s", name, VSuff(op), VN(vd, V_Single), VN(vs,sz));
	}

	void Dis_VScl(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		sprintf(out, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs,sz), VN(vt, V_Single));
	}

	void Dis_VectorSet1(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		sprintf(out, "%s%s\t%s",name,VSuff(op),VN(vd, sz));
	}
	void Dis_VectorSet2(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		sprintf(out, "%s%s\t%s, %s",name,VSuff(op),VN(vd, sz),VN(vs, sz));
	}
	void Dis_VectorSet3(u32 op, char *out)
	{
		const char *name = MIPSGetName(op);
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		sprintf(out, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs,sz), VN(vt, sz));
	}

	void Dis_VRot(u32 op, char *out)
	{
		int vd = _VD;
		int vs = _VS;
		int imm = (op>>16) & 0x1f;
		bool negSin = (imm & 0x10) ? true : false;
		char c[5] = "....";
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
			else
				temp[pos++] = ' ';
			temp[pos++] = c[i];
			temp[pos++] = ' ';
		}
		temp[pos++] = ']';
		temp[pos]=0;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s%s\t%s, %s, %s",name,VSuff(op),VN(vd, sz),VN(vs, V_Single),temp);
	}

	void Dis_CrossQuat(u32 op, char *out)
	{
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
		sprintf(out, "%s%s\t%s, %s, %s", name, VSuff(op), VN(vd, sz), VN(vs,sz), VN(vt, sz));
	}

	void Dis_Vbfy(u32 op, char *out)
	{
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s%s\t%s, %s",name,VSuff(op),VN(vd, sz),VN(vs, sz));
	}

	void Dis_Vf2i(u32 op, char *out)
	{
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		int imm = (op>>16)&0x1f;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s%s\t%s, %s, %i",name,VSuff(op),VN(vd, sz),VN(vs, sz),imm);
	}

	void Dis_Vs2i(u32 op, char *out)
	{
		VectorSize sz = GetVecSize(op);
		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s%s\t%s, %s",name,VSuff(op),VN(vd, sz),VN(vs, sz));
	}

	void Dis_Vi2x(u32 op, char *out)
	{
		VectorSize sz = GetVecSize(op);
		VectorSize dsz = GetHalfVectorSize(sz);
		if (((op>>16)&3)==0)
			dsz = V_Single;

		int vd = _VD;
		int vs = _VS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s%s\t%s, %s",name,VSuff(op),VN(vd, dsz),VN(vs, sz));
	}

	void Dis_VBranch(u32 op, char *out)
	{
		u32 off = disPC;
		int imm = (signed short)(op&0xFFFF)<<2;
		int imm3 = (op>>18)&7;
		off += imm + 4;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t->$%08x	(CC[%i])",name,off,imm3);
	}

}
