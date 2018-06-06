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

#include "Core/MemMap.h"

#include "ge_constants.h"
#include "GPU/GPU.h"
#include "GPU/GPUState.h"

void GeDescribeVertexType(u32 op, char *buffer, int len) {
	bool through = (op & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH;
	int tc = (op & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT;
	int col = (op & GE_VTYPE_COL_MASK) >> GE_VTYPE_COL_SHIFT;
	int nrm = (op & GE_VTYPE_NRM_MASK) >> GE_VTYPE_NRM_SHIFT;
	int pos = (op & GE_VTYPE_POS_MASK) >> GE_VTYPE_POS_SHIFT;
	int weight = (op & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT;
	int weightCount = ((op & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT) + 1;
	int morphCount = (op & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT;
	int idx = (op & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT;

	static const char *colorNames[] = {
		NULL,
		"unsupported1",
		"unsupported2",
		"unsupported3",
		"BGR 565",
		"ABGR 1555",
		"ABGR 4444",
		"ABGR 8888",
	};
	static const char *typeNames[] = {
		NULL,
		"u8",
		"u16",
		"float",
	};
	static const char *typeNamesI[] = {
		NULL,
		"u8",
		"u16",
		"u32",
	};
	static const char *typeNamesS[] = {
		NULL,
		"s8",
		"s16",
		"float",
	};

	char *w = buffer, *end = buffer + len;
	if (through)
		w += snprintf(w, end - w, "through, ");
	if (typeNames[tc] && w < end)
		w += snprintf(w, end - w, "%s texcoords, ", typeNames[tc]);
	if (colorNames[col] && w < end)
		w += snprintf(w, end - w, "%s colors, ", colorNames[col]);
	if (typeNames[nrm] && w < end)
		w += snprintf(w, end - w, "%s normals, ", typeNamesS[nrm]);
	if (typeNames[pos] && w < end)
		w += snprintf(w, end - w, "%s positions, ", typeNamesS[pos]);
	if (typeNames[weight] && w < end)
		w += snprintf(w, end - w, "%s weights (%d), ", typeNames[weight], weightCount);
	else if (weightCount > 1 && w < end)
		w += snprintf(w, end - w, "unknown weights (%d), ", weightCount);
	if (morphCount > 0 && w < end)
		w += snprintf(w, end - w, "%d morphs, ", morphCount);
	if (typeNamesI[idx] && w < end)
		w += snprintf(w, end - w, "%s indexes, ", typeNamesI[idx]);

	if (w < buffer + 2)
		snprintf(buffer, len, "none");
	// Otherwise, get rid of the pesky trailing comma.
	else if (w < end)
		w[-2] = '\0';
}

void GeDisassembleOp(u32 pc, u32 op, u32 prev, char *buffer, int bufsize) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
	case GE_CMD_NOP:
		if (data != 0)
			snprintf(buffer, bufsize, "NOP: data= %06x", data);
		else
			snprintf(buffer, bufsize, "NOP");
		break;

		// Pretty sure this is some sort of NOP to eat some pipelining issue,
		// often seen after CALL instructions.
	case GE_CMD_NOP_FF:
		if (data != 0)
			snprintf(buffer, bufsize, "NOP_FF: data= %06x", data);
		else
			snprintf(buffer, bufsize, "NOP_FF");
		break;

	case GE_CMD_BASE:
		snprintf(buffer, bufsize, "BASE: %06x", data);
		break;

	case GE_CMD_VADDR:
		snprintf(buffer, bufsize, "VADDR: %06x => %08x", data, gstate_c.getRelativeAddress(data));
		break;

	case GE_CMD_IADDR:
		snprintf(buffer, bufsize, "IADDR: %06x => %08x", data, gstate_c.getRelativeAddress(data));
		break;

	case GE_CMD_PRIM:
		{
			u32 count = data & 0xFFFF;
			u32 type = (data >> 16) & 7;
			static const char* types[8] = {
				"POINTS",
				"LINES",
				"LINE_STRIP",
				"TRIANGLES",
				"TRIANGLE_STRIP",
				"TRIANGLE_FAN",
				"RECTANGLES",
				"CONTINUE_PREVIOUS",
			};
			if (gstate.vertType & GE_VTYPE_IDX_MASK)
				snprintf(buffer, bufsize, "DRAW PRIM %s: count= %i vaddr= %08x, iaddr= %08x", type < 7 ? types[type] : "INVALID", count, gstate_c.vertexAddr, gstate_c.indexAddr);
			else
				snprintf(buffer, bufsize, "DRAW PRIM %s: count= %i vaddr= %08x", type < 7 ? types[type] : "INVALID", count, gstate_c.vertexAddr);
		}
		break;

	case GE_CMD_BEZIER:
		{
			int bz_ucount = data & 0xFF;
			int bz_vcount = (data >> 8) & 0xFF;
			if (data & 0xFF0000)
				snprintf(buffer, bufsize, "DRAW BEZIER: %i x %i (extra %x)", bz_ucount, bz_vcount, data >> 16);
			else
				snprintf(buffer, bufsize, "DRAW BEZIER: %i x %i", bz_ucount, bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "DRAW SPLINE: %i x %i (type %ix%i, extra %x)", sp_ucount, sp_vcount, sp_utype, sp_vtype, data >> 20);
			else
				snprintf(buffer, bufsize, "DRAW SPLINE: %i x %i (type %ix%i)", sp_ucount, sp_vcount, sp_utype, sp_vtype);
		}
		break;

	case GE_CMD_JUMP:
		{
			u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
			snprintf(buffer, bufsize, "JUMP: %08x to %08x", pc, target);
		}
		break;

	case GE_CMD_CALL:
		{
			u32 retval = pc + 4;
			u32 target = gstate_c.getRelativeAddress(op & 0xFFFFFF);
			snprintf(buffer, bufsize, "CALL: %08x to %08x, ret=%08x", pc, target, retval);
		}
		break;

	case GE_CMD_RET:
		if (data)
			snprintf(buffer, bufsize, "RET: data= %06x", data);
		else
			snprintf(buffer, bufsize, "RET");
		break;

	case GE_CMD_SIGNAL:
		snprintf(buffer, bufsize, "SIGNAL %06x", data);
		break;

	case GE_CMD_FINISH:
		snprintf(buffer, bufsize, "FINISH %06x", data);
		break;

	case GE_CMD_END:
		switch (prev >> 24)
		{
		case GE_CMD_SIGNAL:
			{
				snprintf(buffer, bufsize, "END - ");
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				int behaviour = (prev >> 16) & 0xFF;
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFFFF;
				// We should probably defer to sceGe here, no sense in implementing this stuff in every GPU
				switch (behaviour) {
				case 1:
					snprintf(buffer, bufsize, "Signal with wait. signal/end: %04x %04x", signal, enddata);
					break;
				case 2:
					snprintf(buffer, bufsize, "Signal without wait. signal/end: %04x %04x", signal, enddata);
					break;
				case 3:
					snprintf(buffer, bufsize, "Signal with pause. signal/end: %04x %04x", signal, enddata);
					break;
				case 8:
					snprintf(buffer, bufsize, "Signal with sync. signal/end: %04x %04x", signal, enddata);
					break;
				case 0x10:
					snprintf(buffer, bufsize, "Signal with jump. signal/end: %04x %04x", signal, enddata);
					break;
				case 0x11:
					snprintf(buffer, bufsize, "Signal with call. signal/end: %04x %04x", signal, enddata);
					break;
				case 0x12:
					snprintf(buffer, bufsize, "Signal with return. signal/end: %04x %04x", signal, enddata);
					break;
				default:
					snprintf(buffer, bufsize, "UNKNOWN Signal UNIMPLEMENTED %i! signal/end: %04x %04x", behaviour, signal, enddata);
					break;
				}
			}
			break;
		case GE_CMD_FINISH:
			if (data)
				snprintf(buffer, bufsize, "END: data= %06x", data);
			else
				snprintf(buffer, bufsize, "END");
			break;
		default:
			snprintf(buffer, bufsize, "END: %06x, not finished (%08x)", data, prev);
			break;
		}
		break;

	case GE_CMD_BJUMP:
		{
			u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
			snprintf(buffer, bufsize, "BBOX_JUMP: target= %08x", target);
		}
		break;

	case GE_CMD_BOUNDINGBOX:
		snprintf(buffer, bufsize, "BBOX_TEST: %06x", data);
		break;

	case GE_CMD_ORIGIN:
		snprintf(buffer, bufsize, "ORIGIN: %06x", data);
		break;

	case GE_CMD_VERTEXTYPE:
		{
			int len = snprintf(buffer, bufsize, "SetVertexType: ");
			GeDescribeVertexType(op, buffer + len, bufsize - len);
		}
		break;

	case GE_CMD_OFFSETADDR:
		snprintf(buffer, bufsize, "OffsetAddr: %06x", data);
		break;

	case GE_CMD_REGION1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Region TL: %d %d (extra %x)", x1, y1, data >> 20);
			else
				snprintf(buffer, bufsize, "Region TL: %d %d", x1, y1);
		}
		break;

	case GE_CMD_REGION2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Region BR: %d %d (extra %x)", x2, y2, data >> 20);
			else
				snprintf(buffer, bufsize, "Region BR: %d %d", x2, y2);
		}
		break;

	case GE_CMD_CLIPENABLE:
		snprintf(buffer, bufsize, "Clip enable: %i", data);
		break;

	case GE_CMD_CULLFACEENABLE:
		snprintf(buffer, bufsize, "CullFace enable: %i", data);
		break;

	case GE_CMD_TEXTUREMAPENABLE:
		snprintf(buffer, bufsize, "Texture map enable: %i", data);
		break;

	case GE_CMD_LIGHTINGENABLE:
		snprintf(buffer, bufsize, "Lighting enable: %i", data);
		break;

	case GE_CMD_FOGENABLE:
		snprintf(buffer, bufsize, "Fog enable: %i", data);
		break;

	case GE_CMD_DITHERENABLE:
		snprintf(buffer, bufsize, "Dither enable: %i", data);
		break;

	case GE_CMD_OFFSETX:
		snprintf(buffer, bufsize, "Offset X: %i", data);
		break;

	case GE_CMD_OFFSETY:
		snprintf(buffer, bufsize, "Offset Y: %i", data);
		break;

	case GE_CMD_TEXSCALEU:
		snprintf(buffer, bufsize, "Texture U scale: %f", getFloat24(data));
		break;

	case GE_CMD_TEXSCALEV:
		snprintf(buffer, bufsize, "Texture V scale: %f", getFloat24(data));
		break;

	case GE_CMD_TEXOFFSETU:
		snprintf(buffer, bufsize, "Texture U offset: %f", getFloat24(data));
		break;

	case GE_CMD_TEXOFFSETV:
		snprintf(buffer, bufsize, "Texture V offset: %f", getFloat24(data));
		break;

	case GE_CMD_SCISSOR1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Scissor TL: %i, %i (extra %x)", x1, y1, data >> 20);
			else
				snprintf(buffer, bufsize, "Scissor TL: %i, %i", x1, y1);
		}
		break;
	case GE_CMD_SCISSOR2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Scissor BR: %i, %i (extra %x)", x2, y2, data >> 20);
			else
				snprintf(buffer, bufsize, "Scissor BR: %i, %i", x2, y2);
		}
		break;

	case GE_CMD_MINZ:
		{
			float zMin = getFloat24(data) / 65535.f;
			snprintf(buffer, bufsize, "MinZ: %f", zMin);
		}
		break;

	case GE_CMD_MAXZ:
		{
			float zMax = getFloat24(data) / 65535.f;
			snprintf(buffer, bufsize, "MaxZ: %f", zMax);
		}
		break;

	case GE_CMD_FRAMEBUFPTR:
		{
			snprintf(buffer, bufsize, "FramebufPtr: %08x", data);
		}
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		{
			snprintf(buffer, bufsize, "FramebufWidth: %x, address high %02x", data & 0xFFFF, data >> 16);
		}
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		snprintf(buffer, bufsize, "FramebufPixelFormat: %i", data);
		break;

	case GE_CMD_TEXADDR0:
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		snprintf(buffer, bufsize, "Texture address %i: %06x", cmd-GE_CMD_TEXADDR0, data);
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		snprintf(buffer, bufsize, "Texture BUFWIDTH %i: %06x", cmd-GE_CMD_TEXBUFWIDTH0, data);
		break;

	case GE_CMD_CLUTADDR:
		snprintf(buffer, bufsize, "CLUT base addr: %06x", data);
		break;

	case GE_CMD_CLUTADDRUPPER:
		snprintf(buffer, bufsize, "CLUT addr upper %08x", data);
		break;

	case GE_CMD_LOADCLUT:
		// This could be used to "dirty" textures with clut.
		if (data)
			snprintf(buffer, bufsize, "Clut load: %06x", data);
		else
			snprintf(buffer, bufsize, "Clut load");
		break;

	case GE_CMD_TEXMAPMODE:
		snprintf(buffer, bufsize, "Tex map mode: %06x", data);
		break;

	case GE_CMD_TEXSHADELS:
		snprintf(buffer, bufsize, "Tex shade light sources: %06x", data);
		break;

	case GE_CMD_CLUTFORMAT:
		{
			const char *clutformats[] = {
				"BGR 5650",
				"ABGR 1555",
				"ABGR 4444",
				"ABGR 8888",
			};
			snprintf(buffer, bufsize, "Clut format: %06x (%s)", data, clutformats[data & 3]);
		}
		break;

	case GE_CMD_TRANSFERSRC:
		{
			if (data & 0xF)
				snprintf(buffer, bufsize, "Block transfer src: %06x (extra: %x)", data & ~0xF, data & 0xF);
			else
				snprintf(buffer, bufsize, "Block transfer src: %06x", data);
			// Nothing to do, the next one prints
		}
		break;

	case GE_CMD_TRANSFERSRCW:
		{
			u32 xferSrc = (gstate.transfersrc & 0x00FFFFFF) | ((data & 0xFF0000) << 8);
			u32 xferSrcW = data & 0x3FF;
			if (data & ~0xFF03FF)
				snprintf(buffer, bufsize, "Block transfer src: %08x	W: %i (extra %x)", xferSrc, xferSrcW, data);
			else
				snprintf(buffer, bufsize, "Block transfer src: %08x	W: %i", xferSrc, xferSrcW);
			break;
		}

	case GE_CMD_TRANSFERDST:
		{
			// Nothing to do, the next one prints
			if (data & 0xF)
				snprintf(buffer, bufsize, "Block transfer dst: %06x (extra: %x)", data & ~0xF, data & 0xF);
			else
				snprintf(buffer, bufsize, "Block transfer dst: %06x", data);
		}
		break;

	case GE_CMD_TRANSFERDSTW:
		{
			u32 xferDst = (gstate.transferdst & 0x00FFFFFF) | ((data & 0xFF0000) << 8);
			u32 xferDstW = data & 0x3FF;
			if (data & ~0xFF03FF)
				snprintf(buffer, bufsize, "Block transfer dest: %08x	W: %i (extra %x)", xferDst, xferDstW, data);
			else
				snprintf(buffer, bufsize, "Block transfer dest: %08x	W: %i", xferDst, xferDstW);
			break;
		}

	case GE_CMD_TRANSFERSRCPOS:
		{
			u32 x = (data & 1023);
			u32 y = ((data>>10) & 1023);
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Block transfer src rect TL: %i, %i (extra %x)", x, y, data >> 20);
			else
				snprintf(buffer, bufsize, "Block transfer src rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERDSTPOS:
		{
			u32 x = (data & 1023);
			u32 y = ((data>>10) & 1023);
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Block transfer dest rect TL: %i, %i (extra %x)", x, y, data >> 20);
			else
				snprintf(buffer, bufsize, "Block transfer dest rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERSIZE:
		{
			u32 w = (data & 1023)+1;
			u32 h = ((data>>10) & 1023)+1;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Block transfer rect size: %i x %i (extra %x)", w, h, data >> 20);
			else
				snprintf(buffer, bufsize, "Block transfer rect size: %i x %i", w, h);
			break;
		}

	case GE_CMD_TRANSFERSTART:
		if (data & ~1)
			snprintf(buffer, bufsize, "Block transfer start: %d (extra %x)", data & 1, data & ~1);
		else
			snprintf(buffer, bufsize, "Block transfer start: %d", data);
		break;

	case GE_CMD_TEXSIZE0:
	case GE_CMD_TEXSIZE1:
	case GE_CMD_TEXSIZE2:
	case GE_CMD_TEXSIZE3:
	case GE_CMD_TEXSIZE4:
	case GE_CMD_TEXSIZE5:
	case GE_CMD_TEXSIZE6:
	case GE_CMD_TEXSIZE7:
		{
			int w = 1 << (data & 0xf);
			int h = 1 << ((data>>8) & 0xf);
			snprintf(buffer, bufsize, "Texture size %i: %06x, width : %d, height : %d", cmd - GE_CMD_TEXSIZE0, data, w, h);
		}
		break;

	case GE_CMD_ZBUFPTR:
		{
			snprintf(buffer, bufsize, "Zbuf ptr: %06x", data);
		}
		break;

	case GE_CMD_ZBUFWIDTH:
		snprintf(buffer, bufsize, "Zbuf width: %06x", data);
		break;

	case GE_CMD_AMBIENTCOLOR:
		snprintf(buffer, bufsize, "Ambient color: %06x", data);
		break;

	case GE_CMD_AMBIENTALPHA:
		snprintf(buffer, bufsize, "Ambient alpha: %06x", data);
		break;

	case GE_CMD_MATERIALAMBIENT:
		snprintf(buffer, bufsize, "Material ambient color: %06x", data);
		break;

	case GE_CMD_MATERIALDIFFUSE:
		snprintf(buffer, bufsize, "Material diffuse color: %06x", data);
		break;

	case GE_CMD_MATERIALEMISSIVE:
		snprintf(buffer, bufsize, "Material emissive color: %06x", data);
		break;

	case GE_CMD_MATERIALSPECULAR:
		snprintf(buffer, bufsize, "Material specular color: %06x", data);
		break;

	case GE_CMD_MATERIALALPHA:
		snprintf(buffer, bufsize, "Material alpha color: %06x", data);
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		snprintf(buffer, bufsize, "Material specular coef: %f", getFloat24(data));
		break;

	case GE_CMD_SHADEMODE:
		if (data & ~1)
			snprintf(buffer, bufsize, "Shade: %06x (%s, extra %x)", data, data ? "gouraud" : "flat", data);
		else
			snprintf(buffer, bufsize, "Shade: %06x (%s)", data, data ? "gouraud" : "flat");
		break;

	case GE_CMD_LIGHTMODE:
		if (data & ~1)
			snprintf(buffer, bufsize, "Lightmode: %06x (%s, extra %x)", data, data ? "separate spec" : "single color", data);
		else
			snprintf(buffer, bufsize, "Lightmode: %06x (%s)", data, data ? "separate spec" : "single color");
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		snprintf(buffer, bufsize, "Light %i type: %06x", cmd-GE_CMD_LIGHTTYPE0, data);
		break;

	case GE_CMD_LX0:case GE_CMD_LY0:case GE_CMD_LZ0:
	case GE_CMD_LX1:case GE_CMD_LY1:case GE_CMD_LZ1:
	case GE_CMD_LX2:case GE_CMD_LY2:case GE_CMD_LZ2:
	case GE_CMD_LX3:case GE_CMD_LY3:case GE_CMD_LZ3:
		{
			int n = cmd - GE_CMD_LX0;
			int l = n / 3;
			int c = n % 3;
			float val = getFloat24(data);
			snprintf(buffer, bufsize, "Light %i %c pos: %f", l, c+'X', val);
		}
		break;

	case GE_CMD_LDX0:case GE_CMD_LDY0:case GE_CMD_LDZ0:
	case GE_CMD_LDX1:case GE_CMD_LDY1:case GE_CMD_LDZ1:
	case GE_CMD_LDX2:case GE_CMD_LDY2:case GE_CMD_LDZ2:
	case GE_CMD_LDX3:case GE_CMD_LDY3:case GE_CMD_LDZ3:
		{
			int n = cmd - GE_CMD_LDX0;
			int l = n / 3;
			int c = n % 3;
			float val = getFloat24(data);
			snprintf(buffer, bufsize, "Light %i %c dir: %f", l, c+'X', val);
		}
		break;

	case GE_CMD_LKA0:case GE_CMD_LKB0:case GE_CMD_LKC0:
	case GE_CMD_LKA1:case GE_CMD_LKB1:case GE_CMD_LKC1:
	case GE_CMD_LKA2:case GE_CMD_LKB2:case GE_CMD_LKC2:
	case GE_CMD_LKA3:case GE_CMD_LKB3:case GE_CMD_LKC3:
		{
			int n = cmd - GE_CMD_LKA0;
			int l = n / 3;
			int c = n % 3;
			float val = getFloat24(data);
			snprintf(buffer, bufsize, "Light %i %c att: %f", l, c+'X', val);
		}
		break;

	case GE_CMD_LAC0:case GE_CMD_LAC1:case GE_CMD_LAC2:case GE_CMD_LAC3:
	case GE_CMD_LDC0:case GE_CMD_LDC1:case GE_CMD_LDC2:case GE_CMD_LDC3:
	case GE_CMD_LSC0:case GE_CMD_LSC1:case GE_CMD_LSC2:case GE_CMD_LSC3:
		{
			float r = (float)(data & 0xff)/255.0f;
			float g = (float)((data>>8) & 0xff)/255.0f;
			float b = (float)(data>>16)/255.0f;

			int l = (cmd - GE_CMD_LAC0) / 3;
			int t = (cmd - GE_CMD_LAC0) % 3;
			snprintf(buffer, bufsize, "Light %i color %i: %f %f %f", l, t, r, g, b);
		}
		break;

	case GE_CMD_VIEWPORTXSCALE:
	case GE_CMD_VIEWPORTYSCALE:
	case GE_CMD_VIEWPORTXCENTER:
	case GE_CMD_VIEWPORTYCENTER:
		snprintf(buffer, bufsize, "Viewport param %i: %f", cmd-GE_CMD_VIEWPORTXSCALE, getFloat24(data));
		break;
	case GE_CMD_VIEWPORTZSCALE:
		{
			float zScale = getFloat24(data) / 65535.f;
			snprintf(buffer, bufsize, "Viewport Z scale: %f", zScale);
		}
		break;
	case GE_CMD_VIEWPORTZCENTER:
		{
			float zOff = getFloat24(data) / 65535.f;
			snprintf(buffer, bufsize, "Viewport Z pos: %f", zOff);
		}
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		snprintf(buffer, bufsize, "Light %i enable: %d", cmd-GE_CMD_LIGHTENABLE0, data);
		break;

	case GE_CMD_CULL:
		snprintf(buffer, bufsize, "Cull: %06x", data);
		break;

	case GE_CMD_PATCHDIVISION:
		{
			int patch_div_s = data & 0xFF;
			int patch_div_t = (data >> 8) & 0xFF;
			if (data & 0xFF0000)
				snprintf(buffer, bufsize, "Patch subdivision: %i x %i (extra %x)", patch_div_s, patch_div_t, data & 0xFF0000);
			else
				snprintf(buffer, bufsize, "Patch subdivision: %i x %i", patch_div_s, patch_div_t);
		}
		break;

	case GE_CMD_PATCHPRIMITIVE:
		snprintf(buffer, bufsize, "Patch Primitive: %d", data);
		break;

	case GE_CMD_PATCHFACING:
		snprintf(buffer, bufsize, "Patch Facing: %d", data);
		break;

	case GE_CMD_REVERSENORMAL:
		snprintf(buffer, bufsize, "Reverse normal: %d", data);
		break;

	case GE_CMD_MATERIALUPDATE:
		snprintf(buffer, bufsize, "Material update: %d", data);
		break;


	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
		{
			const char *clearModes[] = {
				"on",
				"on, color",
				"on, alpha/stencil",
				"on, color, alpha/stencil",
				"on, depth",
				"on, color, depth",
				"on, alpha/stencil, depth",
				"on, color, alpha/stencil, depth",
			};

			const char *mode;
			if (data & 1)
				mode = clearModes[(data >> 8) & 7];
			else
				mode = "off";
			snprintf(buffer, bufsize, "Clear mode: %06x (%s)", data, mode);
		}
		break;


	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
		snprintf(buffer, bufsize, "Alpha blend enable: %d", data);
		break;

	case GE_CMD_BLENDMODE:
		{
			const char *blendModes[] = {
				"add",
				"subtract",
				"reverse subtract",
				"min",
				"max",
				"abs subtract",
				"unsupported1",
				"unsupported2",
			};
			const char *blendFactorsA[16] = {
				"dst",                       
				"1.0 - dst",
				"src.a",
				"1.0 - src.a",
				"dst.a",
				"1.0 - dst.a",
				"2.0 * src.a",
				"1.0 - 2.0 * src.a",
				"2.0 * dst.a",
				"1.0 - 2.0 * dst.a",
				"fixed",
				"fixed2",
				"fixed3",
				"fixed4",
				"fixed5",
			};
			const char *blendFactorsB[16] = {
				"src",
				"1.0 - src",
				"src.a",
				"1.0 - src.a",
				"dst.a",
				"1.0 - dst.a",
				"2.0 * src.a",
				"1.0 - 2.0 * src.a",
				"2.0 * dst.a",
				"1.0 - 2.0 * dst.a",
				"fixed",
				"fixed2",
				"fixed3",
				"fixed4",
				"fixed5",
			};

			const char *blendFactorA = blendFactorsA[(data >> 0) & 0xF];
			const char *blendFactorB = blendFactorsB[(data >> 4) & 0xF];
			const char *blendMode = blendModes[(data >> 8) & 0x7];

			if (data & ~0xFF0007FF)
				snprintf(buffer, bufsize, "Blend mode: %s %s, %s (extra: %06x)", blendMode, blendFactorA, blendFactorB, data & ~0xFF0007FF);
			else
				snprintf(buffer, bufsize, "Blend mode: %s %s, %s", blendMode, blendFactorA, blendFactorB);
		}
		break;

	case GE_CMD_BLENDFIXEDA:
		snprintf(buffer, bufsize, "Blend fix A: %06x", data);
		break;

	case GE_CMD_BLENDFIXEDB:
		snprintf(buffer, bufsize, "Blend fix B: %06x", data);
		break;

	case GE_CMD_ALPHATESTENABLE:
		snprintf(buffer, bufsize, "Alpha test enable: %d", data);
		break;

	case GE_CMD_ALPHATEST:
		{
			const char *alphaTestFuncs[] = { " NEVER ", " ALWAYS ", " == ", " != ", " < ", " <= ", " > ", " >= " };
			snprintf(buffer, bufsize, "Alpha test settings: %06x ((c & %02x)%s%02x)", data, (data >> 16) & 0xFF, alphaTestFuncs[data & 7], (data >> 8) & 0xFF);
		}
		break;

	case GE_CMD_ANTIALIASENABLE:
		snprintf(buffer, bufsize, "Antialias enable: %d", data);
		break;

	case GE_CMD_PATCHCULLENABLE:
		snprintf(buffer, bufsize, "Patch cull enable: %d", data);
		break;

	case GE_CMD_COLORTESTENABLE:
		snprintf(buffer, bufsize, "Color test enable: %d", data);
		break;

	case GE_CMD_LOGICOPENABLE:
		snprintf(buffer, bufsize, "Logic op enable: %d", data);
		break;

	case GE_CMD_TEXFUNC:
		{
			const char *texfuncs[] = {
				"modulate",
				"decal",
				"blend",
				"replace",
				"add",
				"unsupported1",
				"unsupported2",
				"unsupported3",
			};
			if (data & ~0x10107)
				snprintf(buffer, bufsize, "TexFunc %i %s %s%s (extra %x)", data & 7, data & 0x100 ? "RGBA" : "RGB", texfuncs[data & 7], data & 0x10000 ? " color double" : "", data);
			else
				snprintf(buffer, bufsize, "TexFunc %i %s %s%s", data & 7, data & 0x100 ? "RGBA" : "RGB", texfuncs[data & 7], data & 0x10000 ? " color double" : "");
		}
		break;

	case GE_CMD_TEXFILTER:
		{
			int min = data & 7;
			int mag = (data >> 8) & 1;
			if (data & ~0x107)
				snprintf(buffer, bufsize, "TexFilter min: %i mag: %i (extra %x)", min, mag, data);
			else
				snprintf(buffer, bufsize, "TexFilter min: %i mag: %i", min, mag);
		}
		break;

	case GE_CMD_TEXENVCOLOR:
		snprintf(buffer, bufsize, "TexEnvColor %06x", data);
		break;

	case GE_CMD_TEXMODE:
		snprintf(buffer, bufsize, "TexMode %06x (%s, %d levels, %s)", data, data & 1 ? "swizzle" : "no swizzle", (data >> 16) & 7, (data >> 8) & 1 ? "separate cluts" : "shared clut");
		break;

	case GE_CMD_TEXFORMAT:
		{
			const char *texformats[] = {
				"5650",
				"5551",
				"4444",
				"8888",
				"CLUT4",
				"CLUT8",
				"CLUT16",
				"CLUT32",
				"DXT1",
				"DXT3",
				"DXT5",
				"unsupported1",
				"unsupported2",
				"unsupported3",
				"unsupported4",
				"unsupported5",
			};
			snprintf(buffer, bufsize, "TexFormat %06x (%s)", data, texformats[data & 0xF]);
		}
		break;

	case GE_CMD_TEXFLUSH:
		if (data)
			snprintf(buffer, bufsize, "TexFlush: %x", data);
		else
			snprintf(buffer, bufsize, "TexFlush");
		break;

	case GE_CMD_TEXSYNC:
		if (data)
			snprintf(buffer, bufsize, "TexSync: %x", data);
		else
			snprintf(buffer, bufsize, "TexSync");
		break;

	case GE_CMD_TEXWRAP:
		if (data & ~0x0101)
			snprintf(buffer, bufsize, "TexWrap %s s, %s t (extra %x)", data & 1 ? "clamp" : "wrap", data & 0x100 ? "clamp" : "wrap", data);
		else
			snprintf(buffer, bufsize, "TexWrap %s s, %s t", data & 1 ? "clamp" : "wrap", data & 0x100 ? "clamp" : "wrap");
		break;

	case GE_CMD_TEXLEVEL:
		if (data & ~0xFF0003)
			snprintf(buffer, bufsize, "TexLevel mode: %i Offset: %i (extra %x)", data&3, data >> 16, data);
		else
			snprintf(buffer, bufsize, "TexLevel mode: %i Offset: %i", data&3, data >> 16);
		break;

	case GE_CMD_FOG1:
		snprintf(buffer, bufsize, "Fog1 %f", getFloat24(data));
		break;

	case GE_CMD_FOG2:
		snprintf(buffer, bufsize, "Fog2 %f", getFloat24(data));
		break;

	case GE_CMD_FOGCOLOR:
		snprintf(buffer, bufsize, "FogColor %06x", data);
		break;

	case GE_CMD_TEXLODSLOPE:
		snprintf(buffer, bufsize, "TexLodSlope %06x", data);
		break;

	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
		if (data & ~1)
			snprintf(buffer, bufsize, "Z test enable: %d (extra %x)", data & 1, data);
		else
			snprintf(buffer, bufsize, "Z test enable: %d", data & 1);
		break;

	case GE_CMD_STENCILOP:
		{
			const char *stencilOps[] = { "KEEP", "ZERO", "REPLACE", "INVERT", "INCREMENT", "DECREMENT", "unsupported1", "unsupported2" };
			snprintf(buffer, bufsize, "Stencil op: fail=%s, pass/depthfail=%s, pass=%s", stencilOps[data & 7], stencilOps[(data >> 8) & 7], stencilOps[(data >> 16) & 7]);
		}
		break;

	case GE_CMD_STENCILTEST:
		{
			const char *zTestFuncs[] = { "NEVER", "ALWAYS", " == ", " != ", " < ", " <= ", " > ", " >= " };
			snprintf(buffer, bufsize, "Stencil test: %06x (%02x %s (c & %02x))", data, (data >> 8) & 0xFF, zTestFuncs[data & 7], (data >> 16) & 0xFF);
		}
		break;

	case GE_CMD_STENCILTESTENABLE:
		snprintf(buffer, bufsize, "Stencil test enable: %d", data);
		break;

	case GE_CMD_ZTEST:
		{
			const char *zTestFuncs[] = { "NEVER", "ALWAYS", " == ", " != ", " < ", " <= ", " > ", " >= " };
			snprintf(buffer, bufsize, "Z test mode: %i (%s)", data, zTestFuncs[data & 7]);
		}
		break;

	case GE_CMD_MORPHWEIGHT0:
	case GE_CMD_MORPHWEIGHT1:
	case GE_CMD_MORPHWEIGHT2:
	case GE_CMD_MORPHWEIGHT3:
	case GE_CMD_MORPHWEIGHT4:
	case GE_CMD_MORPHWEIGHT5:
	case GE_CMD_MORPHWEIGHT6:
	case GE_CMD_MORPHWEIGHT7:
		{
			int index = cmd - GE_CMD_MORPHWEIGHT0;
			float weight = getFloat24(data);
			snprintf(buffer, bufsize, "MorphWeight %i = %f", index, weight);
		}
		break;

	case GE_CMD_DITH0:
	case GE_CMD_DITH1:
	case GE_CMD_DITH2:
	case GE_CMD_DITH3:
		snprintf(buffer, bufsize, "DitherMatrix %i = %06x",cmd-GE_CMD_DITH0,data);
		break;

	case GE_CMD_LOGICOP:
		{
			const char *logicOps[] = {
				"clear",
				"and",
				"reverse and",
				"copy",
				"inverted and",
				"noop",
				"xor",
				"or",
				"negated or",
				"equivalence",
				"inverted",
				"reverse or",
				"inverted copy",
				"inverted or",
				"negated and",
				"set",
			};
			snprintf(buffer, bufsize, "LogicOp: %06x (%s)", data, logicOps[data & 0xF]);
		}
		break;

	case GE_CMD_ZWRITEDISABLE:
		snprintf(buffer, bufsize, "ZMask: %06x", data);
		break;

	case GE_CMD_COLORTEST:
		{
			const char *colorTests[] = {"NEVER", "ALWAYS", " == ", " != "};
			snprintf(buffer, bufsize, "ColorTest: %06x (ref%s(c & cmask))", data, colorTests[data & 3]);
		}
		break;

	case GE_CMD_COLORREF:
		snprintf(buffer, bufsize, "ColorRef: %06x", data);
		break;

	case GE_CMD_COLORTESTMASK:
		snprintf(buffer, bufsize, "ColorTestMask: %06x", data);
		break;

	case GE_CMD_MASKRGB:
		snprintf(buffer, bufsize, "MaskRGB: %06x", data);
		break;

	case GE_CMD_MASKALPHA:
		snprintf(buffer, bufsize, "MaskAlpha: %06x", data);
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "World # %i (extra %x)", data & 0xF, data);
		else
			snprintf(buffer, bufsize, "World # %i", data & 0xF);
		break;

	case GE_CMD_WORLDMATRIXDATA:
		snprintf(buffer, bufsize, "World data # %f", getFloat24(data));
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "VIEW # %i (extra %x)", data & 0xF, data);
		else
			snprintf(buffer, bufsize, "VIEW # %i", data & 0xF);
		break;

	case GE_CMD_VIEWMATRIXDATA:
		snprintf(buffer, bufsize, "VIEW data # %f", getFloat24(data));
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "PROJECTION # %i (extra %x)", data & 0xF, data);
		else
			snprintf(buffer, bufsize, "PROJECTION # %i", data & 0xF);
		break;

	case GE_CMD_PROJMATRIXDATA:
		snprintf(buffer, bufsize, "PROJECTION matrix data # %f", getFloat24(data));
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "TGEN # %i (extra %x)", data & 0xF, data);
		else
			snprintf(buffer, bufsize, "TGEN # %i", data & 0xF);
		break;

	case GE_CMD_TGENMATRIXDATA:
		snprintf(buffer, bufsize, "TGEN data # %f", getFloat24(data));
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		if (data & ~0x7F)
			snprintf(buffer, bufsize, "BONE #%i (extra %x)", data & 0x7F, data);
		else
			snprintf(buffer, bufsize, "BONE #%i", data & 0x7F);
		break;

	case GE_CMD_BONEMATRIXDATA:
		snprintf(buffer, bufsize, "BONE data #%i %f", gstate.boneMatrixNumber & 0x7f, getFloat24(data));
		break;

	default:
		snprintf(buffer, bufsize, "Unknown: %08x", op);
		break;
	}
}

