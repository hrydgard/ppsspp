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

#include "../Core/MemMap.h"

#include "GPUState.h"
#include "ge_constants.h"

void GeDescribeVertexType(u32 op, char *buffer, int len) {
	bool through = (op & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_THROUGH;
	int tc = (op & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT;
	int col = (op & GE_VTYPE_COL_MASK) >> GE_VTYPE_COL_SHIFT;
	int nrm = (op & GE_VTYPE_NRM_MASK) >> GE_VTYPE_NRM_SHIFT;
	int pos = (op & GE_VTYPE_POS_MASK) >> GE_VTYPE_POS_SHIFT;
	int weight = (op & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT;
	int weightCount = (op & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT;
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

	char *w = buffer, *end = buffer + len;
	if (through)
		w += snprintf(w, end - w, "through, ");
	if (typeNames[tc])
		w += snprintf(w, end - w, "%s UVs, ", typeNames[tc]);
	if (colorNames[col])
		w += snprintf(w, end - w, "%s colors, ", colorNames[col]);
	if (typeNames[nrm])
		w += snprintf(w, end - w, "%s normals, ", typeNames[nrm]);
	if (typeNames[pos])
		w += snprintf(w, end - w, "%s coords, ", typeNames[pos]);
	if (typeNames[weight])
		w += snprintf(w, end - w, "%s weights (%d), ", typeNames[weight], weightCount);
	else if (weightCount > 0)
		w += snprintf(w, end - w, "unknown weights (%d), ", weightCount);
	if (morphCount > 0)
		w += snprintf(w, end - w, "%d morphs, ", morphCount);
	if (typeNames[idx])
		w += snprintf(w, end - w, "%s indexes, ", typeNames[idx]);

	if (w < buffer + 2)
		snprintf(buffer, len, "none");
	// Otherwise, get rid of the pesky trailing comma.
	else
		w[-2] = '\0';
}

void GeDisassembleOp(u32 pc, u32 op, u32 prev, char *buffer) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
	case GE_CMD_NOP:
		if (data != 0)
			sprintf(buffer, "NOP: %06x", data);
		else
			sprintf(buffer, "NOP");
		break;

	case GE_CMD_BASE:
		sprintf(buffer, "BASE: %06x", data);
		break;

	case GE_CMD_VADDR:
		sprintf(buffer, "VADDR: %06x => %08x", data, gstate_c.getRelativeAddress(data));
		break;

	case GE_CMD_IADDR:
		sprintf(buffer, "IADDR: %06x => %08x", data, gstate_c.getRelativeAddress(data));
		break;

	case GE_CMD_PRIM:
		{
			u32 count = data & 0xFFFF;
			u32 type = data >> 16;
			static const char* types[7] = {
				"POINTS",
				"LINES",
				"LINE_STRIP",
				"TRIANGLES",
				"TRIANGLE_STRIP",
				"TRIANGLE_FAN",
				"RECTANGLES",
			};
			if (gstate.vertType & GE_VTYPE_IDX_MASK)
				sprintf(buffer, "DrawPrim indexed type: %s count: %i vaddr= %08x, iaddr= %08x", type < 7 ? types[type] : "INVALID", count, gstate_c.vertexAddr, gstate_c.indexAddr);
			else
				sprintf(buffer, "DrawPrim type: %s count: %i vaddr= %08x", type < 7 ? types[type] : "INVALID", count, gstate_c.vertexAddr);
		}
		break;

	// The arrow and other rotary items in Puzbob are bezier patches, strangely enough.
	case GE_CMD_BEZIER:
		{
			int bz_ucount = data & 0xFF;
			int bz_vcount = (data >> 8) & 0xFF;
			if (data & 0xFF0000)
				sprintf(buffer, "DRAW BEZIER: %i x %i (extra %x)", bz_ucount, bz_vcount, data >> 16);
			else
				sprintf(buffer, "DRAW BEZIER: %i x %i", bz_ucount, bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;
			if (data & 0xF00000)
				sprintf(buffer, "DRAW SPLINE: %i x %i, %i x %i (extra %x)", sp_ucount, sp_vcount, sp_utype, sp_vtype, data >> 20);
			else
				sprintf(buffer, "DRAW SPLINE: %i x %i, %i x %i", sp_ucount, sp_vcount, sp_utype, sp_vtype);
		}
		break;

	case GE_CMD_JUMP:
		{
			u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
			sprintf(buffer, "CMD JUMP - %08x to %08x", pc, target);
		}
		break;

	case GE_CMD_CALL:
		{
			u32 retval = pc + 4;
			u32 target = gstate_c.getRelativeAddress(op & 0xFFFFFF);
			sprintf(buffer, "CMD CALL - %08x to %08x, ret=%08x", pc, target, retval);
		}
		break;

	case GE_CMD_RET:
		if (data)
			sprintf(buffer, "CMD RET: %06x", data);
		else
			sprintf(buffer, "CMD RET");
		break;

	case GE_CMD_SIGNAL:
		sprintf(buffer, "GE_CMD_SIGNAL %06x", data);
		break;

	case GE_CMD_FINISH:
		sprintf(buffer, "CMD FINISH %06x", data);
		break;

	case GE_CMD_END:
		switch (prev >> 24)
		{
		case GE_CMD_SIGNAL:
			{
				sprintf(buffer, "CMD END - ");
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				int behaviour = (prev >> 16) & 0xFF;
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFFFF;
				// We should probably defer to sceGe here, no sense in implementing this stuff in every GPU
				switch (behaviour) {
				case 1:
					sprintf(buffer, "Signal with wait. signal/end: %04x %04x", signal, enddata);
					break;
				case 2:
					sprintf(buffer, "Signal without wait. signal/end: %04x %04x", signal, enddata);
					break;
				case 3:
					sprintf(buffer, "Signal with pause. signal/end: %04x %04x", signal, enddata);
					break;
				case 8:
					sprintf(buffer, "Signal with sync. signal/end: %04x %04x", signal, enddata);
					break;
				case 0x10:
					sprintf(buffer, "Signal with jump. signal/end: %04x %04x", signal, enddata);
					break;
				case 0x11:
					sprintf(buffer, "Signal with call. signal/end: %04x %04x", signal, enddata);
					break;
				case 0x12:
					sprintf(buffer, "Signal with return. signal/end: %04x %04x", signal, enddata);
					break;
				default:
					sprintf(buffer, "UNKNOWN Signal UNIMPLEMENTED %i! signal/end: %04x %04x", behaviour, signal, enddata);
					break;
				}
			}
			break;
		case GE_CMD_FINISH:
			if (data)
				sprintf(buffer, "CMD END: %06x", data);
			else
				sprintf(buffer, "CMD END");
			break;
		default:
			sprintf(buffer, "CMD END: %06x, not finished (%08x)", data, prev);
			break;
		}
		break;

	case GE_CMD_BJUMP:
		// bounding box jump. Let's just not jump, for now.
		sprintf(buffer, "BBOX JUMP - unimplemented: %06x", data);
		break;

	case GE_CMD_BOUNDINGBOX:
		// bounding box test. Let's do nothing.
		sprintf(buffer, "BBOX TEST - unimplemented: %06x", data);
		break;

	case GE_CMD_ORIGIN:
		sprintf(buffer, "Origin: %06x", data);
		break;

	case GE_CMD_VERTEXTYPE:
		{
			int len = sprintf(buffer, "SetVertexType: ");
			GeDescribeVertexType(op, buffer + len, 256 - len);
		}
		break;

	case GE_CMD_OFFSETADDR:
		sprintf(buffer, "OffsetAddr: %06x", data);
		break;

	case GE_CMD_REGION1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			if (data & 0xF00000)
				sprintf(buffer, "Region TL: %d %d (extra %x)", x1, y1, data >> 20);
			else
				sprintf(buffer, "Region TL: %d %d", x1, y1);
		}
		break;

	case GE_CMD_REGION2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			if (data & 0xF00000)
				sprintf(buffer, "Region BR: %d %d (extra %x)", x2, y2, data >> 20);
			else
				sprintf(buffer, "Region BR: %d %d", x2, y2);
		}
		break;

	case GE_CMD_CLIPENABLE:
		sprintf(buffer, "Clip enable: %i", data);
		break;

	case GE_CMD_CULLFACEENABLE:
		sprintf(buffer, "CullFace enable: %i", data);
		break;

	case GE_CMD_TEXTUREMAPENABLE:
		sprintf(buffer, "Texture map enable: %i", data);
		break;

	case GE_CMD_LIGHTINGENABLE:
		sprintf(buffer, "Lighting enable: %i", data);
		break;

	case GE_CMD_FOGENABLE:
		sprintf(buffer, "Fog enable: %i", data);
		break;

	case GE_CMD_DITHERENABLE:
		sprintf(buffer, "Dither enable: %i", data);
		break;

	case GE_CMD_OFFSETX:
		sprintf(buffer, "Offset X: %i", data);
		break;

	case GE_CMD_OFFSETY:
		sprintf(buffer, "Offset Y: %i", data);
		break;

	case GE_CMD_TEXSCALEU:
		sprintf(buffer, "Texture U scale: %f", getFloat24(data));
		break;

	case GE_CMD_TEXSCALEV:
		sprintf(buffer, "Texture V scale: %f", getFloat24(data));
		break;

	case GE_CMD_TEXOFFSETU:
		sprintf(buffer, "Texture U offset: %f", getFloat24(data));
		break;

	case GE_CMD_TEXOFFSETV:
		sprintf(buffer, "Texture V offset: %f", getFloat24(data));
		break;

	case GE_CMD_SCISSOR1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			if (data & 0xF00000)
				sprintf(buffer, "Scissor TL: %i, %i (extra %x)", x1, y1, data >> 20);
			else
				sprintf(buffer, "Scissor TL: %i, %i", x1, y1);
		}
		break;
	case GE_CMD_SCISSOR2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			if (data & 0xF00000)
				sprintf(buffer, "Scissor BR: %i, %i (extra %x)", x2, y2, data >> 20);
			else
				sprintf(buffer, "Scissor BR: %i, %i", x2, y2);
		}
		break;

	case GE_CMD_MINZ:
		{
			float zMin = getFloat24(data) / 65535.f;
			sprintf(buffer, "MinZ: %f", zMin);
		}
		break;

	case GE_CMD_MAXZ:
		{
			float zMax = getFloat24(data) / 65535.f;
			sprintf(buffer, "MaxZ: %f", zMax);
		}
		break;

	case GE_CMD_FRAMEBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			if (data & ~0xFFE000)
				sprintf(buffer, "FramebufPtr: %08x (extra %x)", ptr, data);
			else
				sprintf(buffer, "FramebufPtr: %08x", ptr);
		}
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		{
			sprintf(buffer, "FramebufWidth: %i", data);
		}
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		sprintf(buffer, "FramebufPixelFormat: %i", data);
		break;

	case GE_CMD_TEXADDR0:
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		sprintf(buffer, "Texture address %i: %06x", cmd-GE_CMD_TEXADDR0, data);
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		sprintf(buffer, "Texture BUFWIDTHess %i: %06x", cmd-GE_CMD_TEXBUFWIDTH0, data);
		break;

	case GE_CMD_CLUTADDR:
		sprintf(buffer, "CLUT base addr: %06x", data);
		break;

	case GE_CMD_CLUTADDRUPPER:
		sprintf(buffer, "CLUT addr upper %08x", data);
		break;

	case GE_CMD_LOADCLUT:
		// This could be used to "dirty" textures with clut.
		if (data)
			sprintf(buffer, "Clut load: %06x", data);
		else
			sprintf(buffer, "Clut load");
		break;

	case GE_CMD_TEXMAPMODE:
		sprintf(buffer, "Tex map mode: %06x", data);
		break;

	case GE_CMD_TEXSHADELS:
		sprintf(buffer, "Tex shade light sources: %06x", data);
		break;

	case GE_CMD_CLUTFORMAT:
		{
			const char *clutformats[] = {
				"BGR 5650",
				"ABGR 1555",
				"ABGR 4444",
				"ABGR 8888",
			};
			sprintf(buffer, "Clut format: %06x (%s)", data, clutformats[data & 3]);
		}
		break;

	case GE_CMD_TRANSFERSRC:
		{
			sprintf(buffer, "Block transfer src: %06x", data);
			// Nothing to do, the next one prints
		}
		break;

	case GE_CMD_TRANSFERSRCW:
		{
			u32 xferSrc = (gstate.transfersrc & 0x00FFFFFF) | ((data & 0xFF0000) << 8);
			u32 xferSrcW = gstate.transfersrcw & 1023;
			if (data & ~0xFF03FF)
				sprintf(buffer, "Block transfer src: %08x	W: %i (extra %x)", xferSrc, xferSrcW, data);
			else
				sprintf(buffer, "Block transfer src: %08x	W: %i", xferSrc, xferSrcW);
			break;
		}

	case GE_CMD_TRANSFERDST:
		{
			// Nothing to do, the next one prints
			sprintf(buffer, "Block transfer dst: %06x", data);
		}
		break;

	case GE_CMD_TRANSFERDSTW:
		{
			u32 xferDst = (gstate.transferdst & 0x00FFFFFF) | ((data & 0xFF0000) << 8);
			u32 xferDstW = gstate.transferdstw & 1023;
			if (data & ~0xFF03FF)
				sprintf(buffer, "Block transfer dest: %08x	W: %i (extra %x)", xferDst, xferDstW, data);
			else
				sprintf(buffer, "Block transfer dest: %08x	W: %i", xferDst, xferDstW);
			break;
		}

	case GE_CMD_TRANSFERSRCPOS:
		{
			u32 x = (data & 1023);
			u32 y = ((data>>10) & 1023);
			if (data & 0xF00000)
				sprintf(buffer, "Block transfer src rect TL: %i, %i (extra %x)", x, y, data >> 20);
			else
				sprintf(buffer, "Block transfer src rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERDSTPOS:
		{
			u32 x = (data & 1023);
			u32 y = ((data>>10) & 1023);
			if (data & 0xF00000)
				sprintf(buffer, "Block transfer dest rect TL: %i, %i (extra %x)", x, y, data >> 20);
			else
				sprintf(buffer, "Block transfer dest rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERSIZE:
		{
			u32 w = (data & 1023)+1;
			u32 h = ((data>>10) & 1023)+1;
			if (data & 0xF00000)
				sprintf(buffer, "Block transfer rect size: %i x %i (extra %x)", w, h, data >> 20);
			else
				sprintf(buffer, "Block transfer rect size: %i x %i", w, h);
			break;
		}

	case GE_CMD_TRANSFERSTART:  // Orphis calls this TRXKICK
		if (data)
			sprintf(buffer, "Block transfer start: %x", data);
		else
			sprintf(buffer, "Block transfer start");
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
			sprintf(buffer, "Texture size %i: %06x, width : %d, height : %d", cmd - GE_CMD_TEXSIZE0, data, w, h);
		}
		break;

	case GE_CMD_ZBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			if (data & ~0xFFE000)
				sprintf(buffer, "Zbuf ptr: %06x (extra %x)", ptr, data);
			else
				sprintf(buffer, "Zbuf ptr: %06x", ptr);
		}
		break;

	case GE_CMD_ZBUFWIDTH:
		sprintf(buffer, "Zbuf width: %06x", data);
		break;

	case GE_CMD_AMBIENTCOLOR:
		sprintf(buffer, "Ambient color: %06x", data);
		break;

	case GE_CMD_AMBIENTALPHA:
		sprintf(buffer, "Ambient alpha: %06x", data);
		break;

	case GE_CMD_MATERIALAMBIENT:
		sprintf(buffer, "Material ambient color: %06x", data);
		break;

	case GE_CMD_MATERIALDIFFUSE:
		sprintf(buffer, "Material diffuse color: %06x", data);
		break;

	case GE_CMD_MATERIALEMISSIVE:
		sprintf(buffer, "Material emissive color: %06x", data);
		break;

	case GE_CMD_MATERIALSPECULAR:
		sprintf(buffer, "Material specular color: %06x", data);
		break;

	case GE_CMD_MATERIALALPHA:
		sprintf(buffer, "Material alpha color: %06x", data);
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		sprintf(buffer, "Material specular coef: %f", getFloat24(data));
		break;

	case GE_CMD_SHADEMODE:
		if (data & ~1)
			sprintf(buffer, "Shade: %06x (%s, extra %x)", data, data ? "gouraud" : "flat", data);
		else
			sprintf(buffer, "Shade: %06x (%s)", data, data ? "gouraud" : "flat");
		break;

	case GE_CMD_LIGHTMODE:
		if (data & ~1)
			sprintf(buffer, "Lightmode: %06x (%s, extra %x)", data, data ? "separate spec" : "single color", data);
		else
			sprintf(buffer, "Lightmode: %06x (%s)", data, data ? "separate spec" : "single color");
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		sprintf(buffer, "Light %i type: %06x", cmd-GE_CMD_LIGHTTYPE0, data);
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
			sprintf(buffer, "Light %i %c pos: %f", l, c+'X', val);
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
			sprintf(buffer, "Light %i %c dir: %f", l, c+'X', val);
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
			sprintf(buffer, "Light %i %c att: %f", l, c+'X', val);
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
			sprintf(buffer, "Light %i color %i: %f %f %f", l, t, r, g, b);
		}
		break;

	case GE_CMD_VIEWPORTX1:
	case GE_CMD_VIEWPORTY1:
	case GE_CMD_VIEWPORTX2:
	case GE_CMD_VIEWPORTY2:
		sprintf(buffer, "Viewport param %i: %f", cmd-GE_CMD_VIEWPORTX1, getFloat24(data));
		break;
	case GE_CMD_VIEWPORTZ1:
		{
			float zScale = getFloat24(data) / 65535.f;
			sprintf(buffer, "Viewport Z scale: %f", zScale);
		}
		break;
	case GE_CMD_VIEWPORTZ2:
		{
			float zOff = getFloat24(data) / 65535.f;
			sprintf(buffer, "Viewport Z pos: %f", zOff);
		}
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		sprintf(buffer, "Light %i enable: %d", cmd-GE_CMD_LIGHTENABLE0, data);
		break;

	case GE_CMD_CULL:
		sprintf(buffer, "Cull: %06x", data);
		break;

	case GE_CMD_PATCHDIVISION:
		{
			int patch_div_s = data & 0xFF;
			int patch_div_t = (data >> 8) & 0xFF;
			if (data & 0xFF0000)
				sprintf(buffer, "Patch subdivision: %i x %i (extra %x)", patch_div_s, patch_div_t, data & 0xFF0000);
			else
				sprintf(buffer, "Patch subdivision: %i x %i", patch_div_s, patch_div_t);
		}
		break;

	case GE_CMD_PATCHPRIMITIVE:
		sprintf(buffer, "Patch Primitive: %d", data);
		break;

	case GE_CMD_PATCHFACING:
		sprintf(buffer, "Patch Facing: %d", data);
		break;

	case GE_CMD_REVERSENORMAL:
		sprintf(buffer, "Reverse normal: %d", data);
		break;

	case GE_CMD_MATERIALUPDATE:
		sprintf(buffer, "Material update: %d", data);
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
			sprintf(buffer, "Clear mode: %06x (%s)", data, mode);
		}
		break;


	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
		sprintf(buffer, "Alpha blend enable: %d", data);
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
			const char *blendFactors[] = {
				"a",
				"1.0 - a",
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
				"fixed6",
			};

			const char *blendFactorA = blendFactors[(data >> 0) & 0xF];
			const char *blendFactorB = blendFactors[(data >> 4) & 0xF];
			const char *blendMode = blendModes[(data >> 8) & 0x7];

			if (data & ~0xFF0007FF)
				sprintf(buffer, "Blend mode: %s %s, %s (extra: %06x)", blendMode, blendFactorA, blendFactorB, data & ~0xFF0007FF);
			else
				sprintf(buffer, "Blend mode: %s %s, %s", blendMode, blendFactorA, blendFactorB);
		}
		break;

	case GE_CMD_BLENDFIXEDA:
		sprintf(buffer, "Blend fix A: %06x", data);
		break;

	case GE_CMD_BLENDFIXEDB:
		sprintf(buffer, "Blend fix B: %06x", data);
		break;

	case GE_CMD_ALPHATESTENABLE:
		sprintf(buffer, "Alpha test enable: %d", data);
		break;

	case GE_CMD_ALPHATEST:
		{
			const char *alphaTestFuncs[] = { "NEVER", "ALWAYS", " == ", " != ", " < ", " <= ", " > ", " >= " };
			sprintf(buffer, "Alpha test settings: %06x (%02x%s(c & %02x))", data, (data >> 8) & 0xFF, alphaTestFuncs[data & 7], (data >> 16) & 0xFF);
		}
		break;

	case GE_CMD_ANTIALIASENABLE:
		sprintf(buffer, "Antialias enable: %d", data);
		break;

	case GE_CMD_PATCHCULLENABLE:
		sprintf(buffer, "Patch cull enable: %d", data);
		break;

	case GE_CMD_COLORTESTENABLE:
		sprintf(buffer, "Color test enable: %d", data);
		break;

	case GE_CMD_LOGICOPENABLE:
		sprintf(buffer, "Logic op enable: %d", data);
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
				sprintf(buffer, "TexFunc %i %s %s%s (extra %x)", data & 7, data & 0x100 ? "RGBA" : "RGB", texfuncs[data & 7], data & 0x10000 ? " color double" : "", data);
			else
				sprintf(buffer, "TexFunc %i %s %s%s", data & 7, data & 0x100 ? "RGBA" : "RGB", texfuncs[data & 7], data & 0x10000 ? " color double" : "");
		}
		break;

	case GE_CMD_TEXFILTER:
		{
			int min = data & 7;
			int mag = (data >> 8) & 1;
			if (data & ~0x107)
				sprintf(buffer, "TexFilter min: %i mag: %i (extra %x)", min, mag, data);
			else
				sprintf(buffer, "TexFilter min: %i mag: %i", min, mag);
		}
		break;

	case GE_CMD_TEXENVCOLOR:
		sprintf(buffer, "TexEnvColor %06x", data);
		break;

	case GE_CMD_TEXMODE:
		sprintf(buffer, "TexMode %06x (%s, %d levels, %s)", data, data & 1 ? "swizzle" : "no swizzle", (data >> 16) & 7, (data >> 8) & 1 ? "separate cluts" : "shared clut");
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
			sprintf(buffer, "TexFormat %06x (%s)", data, texformats[data & 0xF]);
		}
		break;

	case GE_CMD_TEXFLUSH:
		if (data)
			sprintf(buffer, "TexFlush: %x", data);
		else
			sprintf(buffer, "TexFlush");
		break;

	case GE_CMD_TEXSYNC:
		if (data)
			sprintf(buffer, "TexSync: %x", data);
		else
			sprintf(buffer, "TexSync");
		break;

	case GE_CMD_TEXWRAP:
		sprintf(buffer, "TexWrap %06x", data);
		break;

	case GE_CMD_TEXLEVEL:
		if (data & ~0xFF0003)
			sprintf(buffer, "TexWrap mode: %i Offset: %i (extra %x)", data&3, data >> 16, data);
		else
			sprintf(buffer, "TexWrap mode: %i Offset: %i", data&3, data >> 16);
		break;

	case GE_CMD_FOG1:
		sprintf(buffer, "Fog1 %f", getFloat24(data));
		break;

	case GE_CMD_FOG2:
		sprintf(buffer, "Fog2 %f", getFloat24(data));
		break;

	case GE_CMD_FOGCOLOR:
		sprintf(buffer, "FogColor %06x", data);
		break;

	case GE_CMD_TEXLODSLOPE:
		sprintf(buffer, "TexLodSlope %06x", data);
		break;

	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
		if (data & ~1)
			sprintf(buffer, "Z test enable: %d (extra %x)", data & 1, data);
		else
			sprintf(buffer, "Z test enable: %d", data & 1);
		break;

	case GE_CMD_STENCILOP:
		{
			const char *stencilOps[] = { "KEEP", "ZERO", "REPLACE", "INVERT", "INCREMENT", "DECREMENT", "unsupported1", "unsupported2" };
			sprintf(buffer, "Stencil op: fail=%s, pass/depthfail=%s, pass=%s", stencilOps[data & 7], stencilOps[(data >> 8) & 7], stencilOps[(data >> 16) & 7]);
		}
		break;

	case GE_CMD_STENCILTEST:
		{
			const char *zTestFuncs[] = { "NEVER", "ALWAYS", " == ", " != ", " < ", " <= ", " > ", " >= " };
			sprintf(buffer, "Stencil test: %06x (%02x %s (c & %02x))", data, (data >> 8) & 0xFF, zTestFuncs[data & 7], (data >> 16) & 0xFF);
		}
		break;

	case GE_CMD_STENCILTESTENABLE:
		sprintf(buffer, "Stencil test enable: %d", data);
		break;

	case GE_CMD_ZTEST:
		{
			const char *zTestFuncs[] = { "NEVER", "ALWAYS", " == ", " != ", " < ", " <= ", " > ", " >= " };
			sprintf(buffer, "Z test mode: %i (%s)", data, zTestFuncs[data & 7]);
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
			sprintf(buffer, "MorphWeight %i = %f", index, weight);
		}
		break;

	case GE_CMD_DITH0:
	case GE_CMD_DITH1:
	case GE_CMD_DITH2:
	case GE_CMD_DITH3:
		sprintf(buffer, "DitherMatrix %i = %06x",cmd-GE_CMD_DITH0,data);
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
			sprintf(buffer, "LogicOp: %06x (%s)", data, logicOps[data & 0xF]);
		}
		break;

	case GE_CMD_ZWRITEDISABLE:
		sprintf(buffer, "ZMask: %06x", data);
		break;

	case GE_CMD_COLORTEST:
		{
			const char *colorTests[] = {"NEVER", "ALWAYS", " == ", " != "};
			sprintf(buffer, "ColorTest: %06x (ref%s(c & cmask))", data, colorTests[data & 3]);
		}
		break;

	case GE_CMD_COLORREF:
		sprintf(buffer, "ColorRef: %06x", data);
		break;

	case GE_CMD_COLORTESTMASK:
		sprintf(buffer, "ColorTestMask: %06x", data);
		break;

	case GE_CMD_MASKRGB:
		sprintf(buffer, "MaskRGB: %06x", data);
		break;

	case GE_CMD_MASKALPHA:
		sprintf(buffer, "MaskAlpha: %06x", data);
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		if (data & ~0xF)
			sprintf(buffer, "World # %i (extra %x)", data & 0xF, data);
		else
			sprintf(buffer, "World # %i", data & 0xF);
		break;

	case GE_CMD_WORLDMATRIXDATA:
		sprintf(buffer, "World data # %f", getFloat24(data));
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		if (data & ~0xF)
			sprintf(buffer, "VIEW # %i (extra %x)", data & 0xF, data);
		else
			sprintf(buffer, "VIEW # %i", data & 0xF);
		break;

	case GE_CMD_VIEWMATRIXDATA:
		sprintf(buffer, "VIEW data # %f", getFloat24(data));
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		if (data & ~0xF)
			sprintf(buffer, "PROJECTION # %i (extra %x)", data & 0xF, data);
		else
			sprintf(buffer, "PROJECTION # %i", data & 0xF);
		break;

	case GE_CMD_PROJMATRIXDATA:
		sprintf(buffer, "PROJECTION matrix data # %f", getFloat24(data));
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		if (data & ~0xF)
			sprintf(buffer, "TGEN # %i (extra %x)", data & 0xF, data);
		else
			sprintf(buffer, "TGEN # %i", data & 0xF);
		break;

	case GE_CMD_TGENMATRIXDATA:
		sprintf(buffer, "TGEN data # %f", getFloat24(data));
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		if (data & ~0x7F)
			sprintf(buffer, "BONE #%i (extra %x)", data & 0x7F, data);
		else
			sprintf(buffer, "BONE #%i", data & 0xF);
		break;

	case GE_CMD_BONEMATRIXDATA:
		sprintf(buffer, "BONE data #%i %f", gstate.boneMatrixNumber & 0x7f, getFloat24(data));
		break;

	default:
		sprintf(buffer, "Unknown: %08x", op);
		break;
	}
}

