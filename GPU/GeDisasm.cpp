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

#include <cstdio>
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/GeDisasm.h"

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

	static const char * const colorNames[] = {
		NULL,
		"unsupported1",
		"unsupported2",
		"unsupported3",
		"BGR 565",
		"ABGR 1555",
		"ABGR 4444",
		"ABGR 8888",
	};
	static const char * const typeNames[] = {
		NULL,
		"u8",
		"u16",
		"float",
	};
	static const char * const typeNamesI[] = {
		NULL,
		"u8",
		"u16",
		"u32",
	};
	static const char * const typeNamesS[] = {
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

	static const char * const primTypes[8] = {
		"POINTS",
		"LINES",
		"LINE_STRIP",
		"TRIANGLES",
		"TRIANGLE_STRIP",
		"TRIANGLE_FAN",
		"RECTANGLES",
		"CONTINUE_PREVIOUS",
	};

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
		if ((data & 0x000F0000) == data)
			snprintf(buffer, bufsize, "BASE: high=%02x", data >> 16);
		else
			snprintf(buffer, bufsize, "BASE: high=%02x (extra %06x)", data >> 16, data & ~0x000F0000);
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
			if (gstate.vertType & GE_VTYPE_IDX_MASK)
				snprintf(buffer, bufsize, "DRAW PRIM %s: count= %i vaddr= %08x, iaddr= %08x", type < 7 ? primTypes[type] : "INVALID", count, gstate_c.vertexAddr, gstate_c.indexAddr);
			else
				snprintf(buffer, bufsize, "DRAW PRIM %s: count= %i vaddr= %08x", type < 7 ? primTypes[type] : "INVALID", count, gstate_c.vertexAddr);
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
			u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
			snprintf(buffer, bufsize, "JUMP: %08x to %08x (%06x)", pc, target, data);
		}
		break;

	case GE_CMD_CALL:
		{
			u32 retval = pc + 4;
			u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
			snprintf(buffer, bufsize, "CALL: %08x to %08x (%06x), ret=%08x", pc, target, data, retval);
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
				u32 target = (((signal << 16) | (enddata & 0xFFFF)) & 0xFFFFFFFC);
				switch (behaviour) {
				case PSP_GE_SIGNAL_HANDLER_SUSPEND:
					snprintf(buffer, bufsize, "Signal with wait. signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_HANDLER_CONTINUE:
					snprintf(buffer, bufsize, "Signal without wait. signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_HANDLER_PAUSE:
					snprintf(buffer, bufsize, "Signal with pause. signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_SYNC:
					snprintf(buffer, bufsize, "Signal with sync. signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_JUMP:
					snprintf(buffer, bufsize, "Signal with jump. signal/end: %04x %04x, target: %08x", signal, enddata, target);
					break;
				case PSP_GE_SIGNAL_CALL:
					snprintf(buffer, bufsize, "Signal with call. signal/end: %04x %04x, target: %08x", signal, enddata, target);
					break;
				case PSP_GE_SIGNAL_RET:
					snprintf(buffer, bufsize, "Signal with return. signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_RJUMP:
					target += pc;
					snprintf(buffer, bufsize, "Signal with jump (relative.) signal/end: %04x %04x, target: %08x", signal, enddata, target);
					break;
				case PSP_GE_SIGNAL_RCALL:
					target += pc;
					snprintf(buffer, bufsize, "Signal with call (relative.) signal/end: %04x %04x, target: %08x", signal, enddata, target);
					break;
				case PSP_GE_SIGNAL_OJUMP:
					target = gstate_c.getRelativeAddress(target);
					snprintf(buffer, bufsize, "Signal with jump (offset.) signal/end: %04x %04x, target: %08x", signal, enddata, target);
					break;
				case PSP_GE_SIGNAL_OCALL:
					target = gstate_c.getRelativeAddress(target);
					snprintf(buffer, bufsize, "Signal with call (offset.) signal/end: %04x %04x, target: %08x", signal, enddata, target);
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
		if (data)
			snprintf(buffer, bufsize, "ORIGIN offset=%08x (extra %06x)", pc, data);
		else
			snprintf(buffer, bufsize, "ORIGIN offset=%08x", pc);
		break;

	case GE_CMD_VERTEXTYPE:
		{
			int len = snprintf(buffer, bufsize, "SetVertexType: ");
			GeDescribeVertexType(op, buffer + len, bufsize - len);
		}
		break;

	case GE_CMD_OFFSETADDR:
		snprintf(buffer, bufsize, "OffsetAddr: %06x (offset=%08x)", data, data << 8);
		break;

	case GE_CMD_REGION1:
		{
			int x1 = data & 0x3ff;
			int y1 = (data >> 10) & 0x3ff;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Region Rate: %d %d (extra %x)", x1, y1, data >> 20);
			else
				snprintf(buffer, bufsize, "Region Rate: %d %d", x1, y1);
		}
		break;

	case GE_CMD_REGION2:
		{
			int x2 = data & 0x3ff;
			int y2 = (data >> 10) & 0x3ff;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Region BR: %d %d (extra %x)", x2, y2, data >> 20);
			else
				snprintf(buffer, bufsize, "Region BR: %d %d", x2, y2);
		}
		break;

	case GE_CMD_DEPTHCLAMPENABLE:
		snprintf(buffer, bufsize, "Depth clamp enable: %i", data);
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
		if (data & ~0xFFFF)
			snprintf(buffer, bufsize, "Offset X: %04x / %d with sub %d (extra %02x)", data & 0xFFFF, data >> 4, data & 0xF, data >> 16);
		else
			snprintf(buffer, bufsize, "Offset X: %04x / %d with sub %d", data & 0xFFFF, data >> 4, data & 0xF);
		break;

	case GE_CMD_OFFSETY:
		if (data & ~0xFFFF)
			snprintf(buffer, bufsize, "Offset Y: %04x / %d with sub %d (extra %02x)", data & 0xFFFF, data >> 4, data & 0xF, data >> 16);
		else
			snprintf(buffer, bufsize, "Offset Y: %04x / %d with sub %d", data & 0xFFFF, data >> 4, data & 0xF);
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
			int y1 = (data >> 10) & 0x3ff;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Scissor TL: %i, %i (extra %x)", x1, y1, data >> 20);
			else
				snprintf(buffer, bufsize, "Scissor TL: %i, %i", x1, y1);
		}
		break;
	case GE_CMD_SCISSOR2:
		{
			int x2 = data & 0x3ff;
			int y2 = (data >> 10) & 0x3ff;
			if (data & 0xF00000)
				snprintf(buffer, bufsize, "Scissor BR: %i, %i (extra %x)", x2, y2, data >> 20);
			else
				snprintf(buffer, bufsize, "Scissor BR: %i, %i", x2, y2);
		}
		break;

	case GE_CMD_MINZ:
		if (data & 0xFF0000)
			snprintf(buffer, bufsize, "MinZ: 0x%04x / %f (extra %02x)", data & 0xFFFF, (float)(data & 0xFFFF) / 65535.0f, data >> 16);
		else
			snprintf(buffer, bufsize, "MinZ: 0x%04x / %f", data, (float)data / 65535.0f);
		break;

	case GE_CMD_MAXZ:
		if (data & 0xFF0000)
			snprintf(buffer, bufsize, "MaxZ: 0x%04x / %f (extra %02x)", data & 0xFFFF, (float)(data & 0xFFFF) / 65535.0f, data >> 16);
		else
			snprintf(buffer, bufsize, "MaxZ: 0x%04x / %f", data, (float)data / 65535.0f);
		break;

	case GE_CMD_FRAMEBUFPTR:
		{
			snprintf(buffer, bufsize, "Framebuf ptr: 0x04%06x", data);
		}
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		if (data & ~0x07FC)
			snprintf(buffer, bufsize, "Framebuf stride: 0x%x (extra %06x)", data & 0x07FC, data & ~0x07FC);
		else
			snprintf(buffer, bufsize, "Framebuf stride: %04x", data);
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		if (data <= 3)
			snprintf(buffer, bufsize, "Framebuf PixelFormat: %s", GeBufferFormatToString((GEBufferFormat)data));
		else
			snprintf(buffer, bufsize, "Framebuf PixelFormat: invalid %x", data);
		break;

	case GE_CMD_TEXADDR0:
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		snprintf(buffer, bufsize, "Texture address %d: low=%06x", cmd - GE_CMD_TEXADDR0, data);
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		snprintf(buffer, bufsize, "Texture stride %d: 0x%04x, address high=%02x", cmd - GE_CMD_TEXBUFWIDTH0, data & 0xFFFF, data >> 16);
		break;

	case GE_CMD_CLUTADDR:
		snprintf(buffer, bufsize, "CLUT addr: low=%06x", data);
		break;

	case GE_CMD_CLUTADDRUPPER:
		if ((data & 0x000F0000) == data)
			snprintf(buffer, bufsize, "CLUT addr: high=%02x", data >> 16);
		else
			snprintf(buffer, bufsize, "CLUT addr: high=%02x (extra %06x)", data >> 16, data & ~0x000F0000);
		break;

	case GE_CMD_LOADCLUT:
		if ((data & 0xFFFFC0) != 0)
			snprintf(buffer, bufsize, "Clut load: %08x, %d bytes (extra %06x)", gstate.getClutAddress(), (data & 0x3F) << 5, data & 0xFFFFC0);
		else
			snprintf(buffer, bufsize, "Clut load: %08x, %d bytes", gstate.getClutAddress(), (data & 0x3F) << 5);
		break;

	case GE_CMD_TEXMAPMODE:
		{
			static const char * const uvgen[] = {
				"texcoords",
				"texgen matrix",
				"env map",
				"invalid"
			};
			static const char * const uvproj[] = {
				"pos",
				"uv",
				"normalized normal",
				"normal",
			};
			if ((data & 0x000303) == data)
				snprintf(buffer, bufsize, "Tex map mode: uvgen=%s, uvproj=%s", uvgen[data & 3], uvproj[(data >> 8) & 3]);
			else
				snprintf(buffer, bufsize, "Tex map mode: uvgen=%s, uvproj=%s (extra %06x)", uvgen[data & 3], uvproj[(data >> 8) & 3], data & ~0x000303);
		}
		break;

	case GE_CMD_TEXSHADELS:
		if ((data & 0x000303) == data)
			snprintf(buffer, bufsize, "Tex shade light sources: %d, %d", data & 3, (data >> 8) & 3);
		else
			snprintf(buffer, bufsize, "Tex shade light sources: %d, %d (extra %06x)", data & 3, (data >> 8) & 3, data & ~0x000303);
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
				snprintf(buffer, bufsize, "Block transfer src: low=%06x (extra: %x)", data & ~0xF, data & 0xF);
			else
				snprintf(buffer, bufsize, "Block transfer src: low=%06x", data);
			// Nothing to do, the next one prints
		}
		break;

	case GE_CMD_TRANSFERSRCW:
		{
			u32 xferSrc = (gstate.transfersrc & 0x00FFFFFF) | ((data & 0xFF0000) << 8);
			u32 xferSrcW = (data & 0x400) != 0 ? 0x400 : (data & 0x3FF);
			u32 validMask = (data & 0x400) != 0 ? 0xFF0400 : 0xFF03FF;
			if (data & ~validMask)
				snprintf(buffer, bufsize, "Block transfer src: high=%02x, w=%d (addr %08x, extra %x)", data >> 16, xferSrcW, xferSrc, data & ~validMask);
			else
				snprintf(buffer, bufsize, "Block transfer src: high=%02x, w=%d (addr %08x)", data >> 16, xferSrcW, xferSrc);
			break;
		}

	case GE_CMD_TRANSFERDST:
		{
			// Nothing to do, the next one prints
			if (data & 0xF)
				snprintf(buffer, bufsize, "Block transfer dst: low=%06x (extra: %x)", data & ~0xF, data & 0xF);
			else
				snprintf(buffer, bufsize, "Block transfer dst: low=%06x", data);
		}
		break;

	case GE_CMD_TRANSFERDSTW:
		{
			u32 xferDst = (gstate.transferdst & 0x00FFFFFF) | ((data & 0xFF0000) << 8);
			u32 xferDstW = (data & 0x400) != 0 ? 0x400 : (data & 0x3FF);
			u32 validMask = (data & 0x400) != 0 ? 0xFF0400 : 0xFF03FF;
			if (data & ~validMask)
				snprintf(buffer, bufsize, "Block transfer dst: high=%02x, w=%d (addr %08x, extra %x)", data >> 16, xferDstW, xferDst, data & ~validMask);
			else
				snprintf(buffer, bufsize, "Block transfer dst: high=%02x, w=%d (addr %08x)", data >> 16, xferDstW, xferDst);
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
				snprintf(buffer, bufsize, "Block transfer dst rect TL: %d, %d (extra %x)", x, y, data >> 20);
			else
				snprintf(buffer, bufsize, "Block transfer dst rect TL: %d, %d", x, y);
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
			snprintf(buffer, bufsize, "Block transfer start: bpp=%d (extra %x)", (data & 1) ? 4 : 2, data & ~1);
		else
			snprintf(buffer, bufsize, "Block transfer start: bpp=%d", (data & 1) ? 4 : 2);
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
			if ((data & ~0x0F0F) == 0 && w <= 512 && h <= 512)
				snprintf(buffer, bufsize, "Texture size %d: %dx%d", cmd - GE_CMD_TEXSIZE0, w, h);
			else
				snprintf(buffer, bufsize, "Texture size %d: %dx%d (extra %06x)", cmd - GE_CMD_TEXSIZE0, w, h, data);
		}
		break;

	case GE_CMD_ZBUFPTR:
		{
			snprintf(buffer, bufsize, "Zbuf ptr: %06x", data);
		}
		break;

	case GE_CMD_ZBUFWIDTH:
		if (data & ~0x07FC)
			snprintf(buffer, bufsize, "Zbuf stride: 0x%x (extra %06x)", data & 0x07FC, data & ~0x07FC);
		else
			snprintf(buffer, bufsize, "Zbuf stride: %04x", data);
		break;

	case GE_CMD_AMBIENTCOLOR:
		snprintf(buffer, bufsize, "Ambient color: %06x", data);
		break;

	case GE_CMD_AMBIENTALPHA:
		if (data & ~0xFF)
			snprintf(buffer, bufsize, "Ambient alpha: %02x (extra %04x)", data & 0xFF, data >> 8);
		else
			snprintf(buffer, bufsize, "Ambient alpha: %02x", data);
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
		if (data & ~0xFF)
			snprintf(buffer, bufsize, "Material alpha: %02x (extra %04x)", data & 0xFF, data >> 8);
		else
			snprintf(buffer, bufsize, "Material alpha: %02x", data);
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		snprintf(buffer, bufsize, "Material specular coef: %f", getFloat24(data));
		break;

	case GE_CMD_SHADEMODE:
		if (data & ~1)
			snprintf(buffer, bufsize, "Shade: %d (%s, extra %x)", data & 1, (data & 1) ? "gouraud" : "flat", data & ~1);
		else
			snprintf(buffer, bufsize, "Shade: %d (%s)", data & 1, (data & 1) ? "gouraud" : "flat");
		break;

	case GE_CMD_LIGHTMODE:
		if (data & ~1)
			snprintf(buffer, bufsize, "Lightmode: %d (%s, extra %x)", data & 1, (data & 1) ? "separate spec" : "single color", data & ~1);
		else
			snprintf(buffer, bufsize, "Lightmode: %d (%s)", data & 1, (data & 1) ? "separate spec" : "single color");
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		{
			static const char *lightComputations[] = {
				"diffuse",
				"diffuse + spec",
				"pow(diffuse)",
				"unknown (diffuse?)",
			};
			static const char *lightTypes[] = {
				"directional",
				"point",
				"spot",
				"unknown (directional?)",
			};
			const int comp = (data & 0x0003) >> 0;
			const int type = (data & 0x0300) >> 8;

			if (data & ~0x0303)
				snprintf(buffer, bufsize, "Light %d type: %s, comp: %s (extra %06x)", cmd - GE_CMD_LIGHTTYPE0, lightTypes[type], lightComputations[comp], data & ~0x0303);
			else
				snprintf(buffer, bufsize, "Light %d type: %s, comp: %s", cmd - GE_CMD_LIGHTTYPE0, lightTypes[type], lightComputations[comp]);
		}
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

	case GE_CMD_LKS0:
	case GE_CMD_LKS1:
	case GE_CMD_LKS2:
	case GE_CMD_LKS3:
		snprintf(buffer, bufsize, "Light %d spot exponent: %f", cmd - GE_CMD_LKS0, getFloat24(data));
		break;

	case GE_CMD_LKO0:
	case GE_CMD_LKO1:
	case GE_CMD_LKO2:
	case GE_CMD_LKO3:
		snprintf(buffer, bufsize, "Light %d spot cutoff: %f", cmd - GE_CMD_LKO0, getFloat24(data));
		break;

	case GE_CMD_LAC0:case GE_CMD_LAC1:case GE_CMD_LAC2:case GE_CMD_LAC3:
	case GE_CMD_LDC0:case GE_CMD_LDC1:case GE_CMD_LDC2:case GE_CMD_LDC3:
	case GE_CMD_LSC0:case GE_CMD_LSC1:case GE_CMD_LSC2:case GE_CMD_LSC3:
		{
			float r = (float)(data & 0xff)/255.0f;
			float g = (float)((data>>8) & 0xff)/255.0f;
			float b = (float)(data>>16)/255.0f;

			static const char * const lightColorTypes[] = {
				"ambient",
				"diffuse",
				"specular",
			};

			int l = (cmd - GE_CMD_LAC0) / 3;
			int t = (cmd - GE_CMD_LAC0) % 3;
			snprintf(buffer, bufsize, "Light %d %s color: %f %f %f", l, lightColorTypes[t], r, g, b);
		}
		break;

	case GE_CMD_VIEWPORTXSCALE:
		snprintf(buffer, bufsize, "Viewport X scale: %f", getFloat24(data));
		break;
	case GE_CMD_VIEWPORTYSCALE:
		snprintf(buffer, bufsize, "Viewport Y scale: %f", getFloat24(data));
		break;
	case GE_CMD_VIEWPORTZSCALE:
		snprintf(buffer, bufsize, "Viewport Z scale: %f", getFloat24(data));
		break;
	case GE_CMD_VIEWPORTXCENTER:
		snprintf(buffer, bufsize, "Viewport X center: %f", getFloat24(data));
		break;
	case GE_CMD_VIEWPORTYCENTER:
		snprintf(buffer, bufsize, "Viewport Y center: %f", getFloat24(data));
		break;
	case GE_CMD_VIEWPORTZCENTER:
		snprintf(buffer, bufsize, "Viewport Z center: %f", getFloat24(data));
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		snprintf(buffer, bufsize, "Light %i enable: %d", cmd-GE_CMD_LIGHTENABLE0, data);
		break;

	case GE_CMD_CULL:
		if (data & ~1)
			snprintf(buffer, bufsize, "Cull: %s (extra %06x)", (data & 1) ? "back (CCW)" : "front (CW)", data & ~1);
		else
			snprintf(buffer, bufsize, "Cull: %s", (data & 1) ? "back (CCW)" : "front (CW)");
		break;

	case GE_CMD_PATCHDIVISION:
		{
			int patch_div_s = data & 0x7F;
			int patch_div_t = (data >> 8) & 0x7F;
			if (data & 0xFF8080)
				snprintf(buffer, bufsize, "Patch subdivision: %d x %d (extra %x)", patch_div_s, patch_div_t, data & 0xFF8080);
			else
				snprintf(buffer, bufsize, "Patch subdivision: %d x %d", patch_div_s, patch_div_t);
		}
		break;

	case GE_CMD_PATCHPRIMITIVE:
		{
			static const char *patchPrims[] = {
				"triangles",
				"lines",
				"points",
				"unknown/points",
			};
			if (data & ~3)
				snprintf(buffer, bufsize, "Patch Primitive: %s (extra %06x)", patchPrims[data & 3], data & ~3);
			else
				snprintf(buffer, bufsize, "Patch Primitive: %s", patchPrims[data & 3]);
		}
		break;

	case GE_CMD_PATCHFACING:
		if (data & ~1)
			snprintf(buffer, bufsize, "Patch Facing: %s (extra %06x)", (data & 1) ? "reversed normals" : "standard normals", data & ~1);
		else
			snprintf(buffer, bufsize, "Patch Facing: %s", (data & 1) ? "reversed normals" : "standard normals");
		break;

	case GE_CMD_REVERSENORMAL:
		if (data & ~1)
			snprintf(buffer, bufsize, "Reverse normal: %s (extra %06x)", (data & 1) ? "reversed" : "standard", data & ~1);
		else
			snprintf(buffer, bufsize, "Reverse normal: %s", (data & 1) ? "reversed" : "standard");
		break;

	case GE_CMD_MATERIALUPDATE:
		{
			static const char *materialTypes[] = {
				"none",
				"ambient",
				"diffuse",
				"ambient, diffuse",
				"specular",
				"ambient, specular",
				"diffuse, specular",
				"ambient, diffuse, specular",
			};
			if (data & ~7)
				snprintf(buffer, bufsize, "Material update: %s (extra %06x)", materialTypes[data & 7], data & ~7);
			else
				snprintf(buffer, bufsize, "Material update: %s", materialTypes[data & 7]);
		}
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
			if (data & ~0x0701)
				snprintf(buffer, bufsize, "Clear mode: %s (extra %06x)", mode, data & ~0x0701);
			else
				snprintf(buffer, bufsize, "Clear mode: %s", mode);
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
			if (data & ~0xFFFF07)
				snprintf(buffer, bufsize, "Alpha test: (src.a & %02x)%s%02x (extra %06x)", (data >> 16) & 0xFF, alphaTestFuncs[data & 7], (data >> 8) & 0xFF, data & ~0xFFFF07);
			else
				snprintf(buffer, bufsize, "Alpha test: (src.a & %02x)%s%02x", (data >> 16) & 0xFF, alphaTestFuncs[data & 7], (data >> 8) & 0xFF);
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
			const char * const texfuncs[] = {
				"modulate",
				"decal",
				"blend",
				"replace",
				"add",
				"add2",
				"add3",
				"add4",
			};
			if (data & ~0x10107)
				snprintf(buffer, bufsize, "TexFunc %d %s %s%s (extra %x)", data & 7, data & 0x100 ? "RGBA" : "RGB", texfuncs[data & 7], data & 0x10000 ? ", color double" : "", data & ~0x10107);
			else
				snprintf(buffer, bufsize, "TexFunc %d %s %s%s", data & 7, data & 0x100 ? "RGBA" : "RGB", texfuncs[data & 7], data & 0x10000 ? ", color double" : "");
		}
		break;

	case GE_CMD_TEXFILTER:
		{
			static const char * const textureFilters[] = {
				"nearest",
				"linear",
				"nearest, invalid",
				"linear, invalid",
				"nearest, mipmap nearest",
				"linear, mipmap nearest",
				"nearest, mipmap linear",
				"linear, mipmap linear",
			};
			int min = data & 7;
			int mag = (data >> 8) & 1;
			if (data & ~0x107)
				snprintf(buffer, bufsize, "TexFilter min: %s, mag: %s (extra %x)", textureFilters[min], textureFilters[mag], data & ~0x107);
			else
				snprintf(buffer, bufsize, "TexFilter min: %s, mag: %s", textureFilters[min], textureFilters[mag]);
		}
		break;

	case GE_CMD_TEXENVCOLOR:
		snprintf(buffer, bufsize, "TexEnvColor %06x", data);
		break;

	case GE_CMD_TEXMODE:
		if (data & ~0x070101)
			snprintf(buffer, bufsize, "TexMode %s, %d levels, %s (extra %06x)", (data & 1) ? "swizzle" : "no swizzle", (data >> 16) & 7, (data >> 8) & 1 ? "separate cluts" : "shared clut", data & ~0x070101);
		else
			snprintf(buffer, bufsize, "TexMode %s, %d levels, %s", (data & 1) ? "swizzle" : "no swizzle", (data >> 16) & 7, (data >> 8) & 1 ? "separate cluts" : "shared clut");
		break;

	case GE_CMD_TEXFORMAT:
		{
			static const char * const texformats[] = {
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
			if (data & ~0xF)
				snprintf(buffer, bufsize, "TexFormat %s (extra %06x)", texformats[data & 0xF], data & ~0xF);
			else
				snprintf(buffer, bufsize, "TexFormat %s", texformats[data & 0xF]);
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
			snprintf(buffer, bufsize, "TexWrap %s s, %s t (extra %x)", data & 1 ? "clamp" : "wrap", data & 0x100 ? "clamp" : "wrap", data & ~0x0101);
		else
			snprintf(buffer, bufsize, "TexWrap %s s, %s t", data & 1 ? "clamp" : "wrap", data & 0x100 ? "clamp" : "wrap");
		break;

	case GE_CMD_TEXLEVEL:
		{
			static const char * const mipLevelModes[] = {
				"auto + bias",
				"bias",
				"slope + bias",
				"invalid + bias",
			};
			const int biasFixed = (s8)(data >> 16);
			const float bias = (float)biasFixed / 16.0f;
			if (data & ~0xFF0003)
				snprintf(buffer, bufsize, "TexLevel mode: %s Offset: %f / %d (extra %x)", mipLevelModes[data & 3], bias, biasFixed, data & ~0xFF0003);
			else
				snprintf(buffer, bufsize, "TexLevel mode: %s Offset: %f / %d", mipLevelModes[data & 3], bias, biasFixed);
		}
		break;

	case GE_CMD_FOG1:
		snprintf(buffer, bufsize, "Fog end %f", getFloat24(data));
		break;

	case GE_CMD_FOG2:
		snprintf(buffer, bufsize, "Fog slope %f", getFloat24(data));
		break;

	case GE_CMD_FOGCOLOR:
		snprintf(buffer, bufsize, "FogColor %06x", data);
		break;

	case GE_CMD_TEXLODSLOPE:
		snprintf(buffer, bufsize, "TexLodSlope %f", getFloat24(data));
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
			static const char * const stencilOps[] = { "KEEP", "ZERO", "REPLACE", "INVERT", "INCREMENT", "DECREMENT", "unsupported1", "unsupported2" };
			snprintf(buffer, bufsize, "Stencil op: fail=%s, pass/depthfail=%s, pass=%s", stencilOps[data & 7], stencilOps[(data >> 8) & 7], stencilOps[(data >> 16) & 7]);
		}
		break;

	case GE_CMD_STENCILTEST:
		{
			static const char * const zTestFuncs[] = { " NEVER ", " ALWAYS ", " == ", " != ", " < ", " <= ", " > ", " >= " };
			if (data & ~0xFFFF07)
				snprintf(buffer, bufsize, "Stencil test: %02x%s(dst.a & %02x) (extra %06x)", (data >> 8) & 0xFF, zTestFuncs[data & 7], (data >> 16) & 0xFF, data & ~0xFFFF07);
			else
				snprintf(buffer, bufsize, "Stencil test: %02x%s(dst.a & %02x)", (data >> 8) & 0xFF, zTestFuncs[data & 7], (data >> 16) & 0xFF);
		}
		break;

	case GE_CMD_STENCILTESTENABLE:
		snprintf(buffer, bufsize, "Stencil test enable: %d", data);
		break;

	case GE_CMD_ZTEST:
		{
			static const char * const zTestFuncs[] = { "NEVER", "ALWAYS", "==", "!=", "<", "<=", ">", ">=" };
			if (data & ~7)
				snprintf(buffer, bufsize, "Z test mode: %s (extra %06x)", zTestFuncs[data & 7], data & ~7);
			else
				snprintf(buffer, bufsize, "Z test mode: %s", zTestFuncs[data & 7]);
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
			const char * const logicOps[] = {
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
			if (data & ~0xF)
				snprintf(buffer, bufsize, "LogicOp: %s (%06x)", logicOps[data & 0xF], data & ~0xF);
			else
				snprintf(buffer, bufsize, "LogicOp: %s", logicOps[data & 0xF]);
		}
		break;

	case GE_CMD_ZWRITEDISABLE:
		if (data & ~1)
			snprintf(buffer, bufsize, "ZMask: %s (extra %06x)", data & 1 ? "disable write" : "allow write", data & ~1);
		else
			snprintf(buffer, bufsize, "ZMask: %s", data & 1 ? "disable write" : "allow write");
		break;

	case GE_CMD_COLORTEST:
		{
			const char *colorTests[] = {" NEVER ", " ALWAYS ", " == ", " != "};
			if (data & ~3)
				snprintf(buffer, bufsize, "ColorTest: (src.rgb & cmask)%s(dst.rgb & cmask) (extra %06x)", colorTests[data & 3], data & ~3);
			else
				snprintf(buffer, bufsize, "ColorTest: (src.rgb & cmask)%s(dst.rgb & cmask)", colorTests[data & 3]);
		}
		break;

	case GE_CMD_COLORREF:
		snprintf(buffer, bufsize, "ColorRef: %06x", data);
		break;

	case GE_CMD_COLORTESTMASK:
		snprintf(buffer, bufsize, "ColorTestMask: %06x", data);
		break;

	case GE_CMD_MASKRGB:
		snprintf(buffer, bufsize, "MaskRGB: %06x (bits not to write)", data);
		break;

	case GE_CMD_MASKALPHA:
		if (data & ~0xFF)
			snprintf(buffer, bufsize, "MaskAlpha: %02x (bits not to write) (extra %04x)", data & 0xFF, data >> 8);
		else
			snprintf(buffer, bufsize, "MaskAlpha: %02x (bits not to write)", data & 0xFF);
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "World # %d (extra %x)", data & 0xF, data & ~0xF);
		else
			snprintf(buffer, bufsize, "World # %d", data & 0xF);
		break;

	case GE_CMD_WORLDMATRIXDATA:
		snprintf(buffer, bufsize, "World data # %f", getFloat24(data));
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "VIEW # %d (extra %x)", data & 0xF, data & ~0xF);
		else
			snprintf(buffer, bufsize, "VIEW # %d", data & 0xF);
		break;

	case GE_CMD_VIEWMATRIXDATA:
		snprintf(buffer, bufsize, "VIEW data # %f", getFloat24(data));
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "PROJECTION # %d (extra %x)", data & 0xF, data & ~0xF);
		else
			snprintf(buffer, bufsize, "PROJECTION # %d", data & 0xF);
		break;

	case GE_CMD_PROJMATRIXDATA:
		snprintf(buffer, bufsize, "PROJECTION matrix data # %f", getFloat24(data));
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		if (data & ~0xF)
			snprintf(buffer, bufsize, "TGEN # %d (extra %x)", data & 0xF, data & ~0xF);
		else
			snprintf(buffer, bufsize, "TGEN # %d", data & 0xF);
		break;

	case GE_CMD_TGENMATRIXDATA:
		snprintf(buffer, bufsize, "TGEN data # %f", getFloat24(data));
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		if (data & ~0x7F)
			snprintf(buffer, bufsize, "BONE #%d (extra %x)", data & 0x7F, data & ~0x7F);
		else
			snprintf(buffer, bufsize, "BONE #%d", data & 0x7F);
		break;

	case GE_CMD_BONEMATRIXDATA:
		snprintf(buffer, bufsize, "BONE data # %f", getFloat24(data));
		break;

	case GE_CMD_VSCX:
		if (data & ~0xFFFF)
			snprintf(buffer, bufsize, "Vertex screen X: %04x / %d with sub %d (extra %02x)", data & 0xFFFF, data >> 4, data & 0xF, data >> 16);
		else
			snprintf(buffer, bufsize, "Vertex screen X: %04x / %d with sub %d", data & 0xFFFF, data >> 4, data & 0xF);
		break;

	case GE_CMD_VSCY:
		if (data & ~0xFFFF)
			snprintf(buffer, bufsize, "Vertex screen Y: %04x / %d with sub %d (extra %02x)", data & 0xFFFF, data >> 4, data & 0xF, data >> 16);
		else
			snprintf(buffer, bufsize, "Vertex screen Y: %04x / %d with sub %d", data & 0xFFFF, data >> 4, data & 0xF);
		break;

	case GE_CMD_VSCZ:
		if (data & ~0xFFFF)
			snprintf(buffer, bufsize, "Vertex Z: %04x (extra %02x)", data & 0xFFFF, data >> 16);
		else
			snprintf(buffer, bufsize, "Vertex Z: %04x", data & 0xFFFF);
		break;

	case GE_CMD_VTCS:
		snprintf(buffer, bufsize, "Vertex tex S: %f", getFloat24(data));
		break;

	case GE_CMD_VTCT:
		snprintf(buffer, bufsize, "Vertex tex T: %f", getFloat24(data));
		break;

	case GE_CMD_VTCQ:
		snprintf(buffer, bufsize, "Vertex tex Q: %f", getFloat24(data));
		break;

	case GE_CMD_VCV:
		snprintf(buffer, bufsize, "Vertex color: %06x", data);
		break;

	case GE_CMD_VAP:
		{
			bool antialias = (data & GE_IMM_ANTIALIAS) != 0;
			int clip = (data & GE_IMM_CLIPMASK) >> 12;
			bool shading = (data & GE_IMM_SHADING) != 0;
			bool cullEnable = (data & GE_IMM_CULLENABLE) != 0;
			int cullMode = (data & GE_IMM_CULLFACE) != 0 ? 1 : 0;
			bool texturing = (data & GE_IMM_TEXTURE) != 0;
			bool dither = (data & GE_IMM_DITHER) != 0;
			char *p = buffer;
			p += snprintf(p, bufsize - (p - buffer), "Vertex draw: alpha=%02x, prim=%s", data & 0xFF, primTypes[(data >> 8) & 7]);
			if (antialias)
				p += snprintf(p, bufsize - (p - buffer), ", antialias");
			if (clip != 0)
				p += snprintf(p, bufsize - (p - buffer), ", clip=%02x", clip);
			if (shading)
				p += snprintf(p, bufsize - (p - buffer), ", shading");
			if (cullEnable)
				p += snprintf(p, bufsize - (p - buffer), ", cull=%s", cullMode == 1 ? "back (CCW)" : "front (CW)");
			if (texturing)
				p += snprintf(p, bufsize - (p - buffer), ", texturing");
			if (dither)
				p += snprintf(p, bufsize - (p - buffer), ", dither");
		}
		break;

	case GE_CMD_VFC:
		if (data & ~0xFF)
			snprintf(buffer, bufsize, "Vertex fog: %02x / %f (extra %04x)", data & 0xFF, (data & 0xFF) / 255.0f, data >> 8);
		else
			snprintf(buffer, bufsize, "Vertex fog: %02x / %f", data & 0xFF, (data & 0xFF) / 255.0f);
		break;

	case GE_CMD_VSCV:
		snprintf(buffer, bufsize, "Vertex secondary color: %06x", data);
		break;

	default:
		snprintf(buffer, bufsize, "Unknown: %08x", op);
		break;
	}
}

