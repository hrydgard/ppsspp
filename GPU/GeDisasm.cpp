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

void GeDisassembleOp(u32 pc, u32 op, u32 prev, char *buffer) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
	case GE_CMD_BASE:
		sprintf(buffer, "BASE: %06x", data & 0xFFFFFF);
		break;

	case GE_CMD_VADDR:		/// <<8????
		sprintf(buffer, "VADDR: %06x", gstate_c.vertexAddr);
		break;

	case GE_CMD_IADDR:
		sprintf(buffer, "IADDR: %06x", gstate_c.indexAddr);
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
			sprintf(buffer, "DrawPrim type: %s count: %i vaddr= %08x, iaddr= %08x", type < 7 ? types[type] : "INVALID", count, gstate_c.vertexAddr, gstate_c.indexAddr);
		}
		break;

	// The arrow and other rotary items in Puzbob are bezier patches, strangely enough.
	case GE_CMD_BEZIER:
		{
			int bz_ucount = data & 0xFF;
			int bz_vcount = (data >> 8) & 0xFF;
			sprintf(buffer, "DRAW BEZIER: %i x %i", bz_ucount, bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;
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
		sprintf(buffer, "CMD RET");
		break;

	case GE_CMD_SIGNAL:
		sprintf(buffer, "GE_CMD_SIGNAL %06x", data);
		break;

	case GE_CMD_FINISH:
		sprintf(buffer, "CMD FINISH %06x", data);
		break;

	case GE_CMD_END:
		sprintf(buffer, "CMD END");
		switch (prev >> 24)
		{
		case GE_CMD_SIGNAL:
			{
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				int behaviour = (prev >> 16) & 0xFF;
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFF;
				// We should probably defer to sceGe here, no sense in implementing this stuff in every GPU
				switch (behaviour) {
				case 1:  // Signal with Wait
					sprintf(buffer, "Signal with Wait UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 2:
					sprintf(buffer, "Signal without wait. signal/end: %04x %04x", signal, enddata);
					break;
				case 3:
					sprintf(buffer, "Signal with Pause UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x10:
					sprintf(buffer, "Signal with Jump UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x11:
					sprintf(buffer, "Signal with Call UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x12:
					sprintf(buffer, "Signal with Return UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				default:
					sprintf(buffer, "UNKNOWN Signal UNIMPLEMENTED %i ! signal/end: %04x %04x", behaviour, signal, enddata);
					break;
				}
			}
			break;
		case GE_CMD_FINISH:
			break;
		default:
			sprintf(buffer, "Ah, not finished: %06x", prev & 0xFFFFFF);
			break;
		}
		break;

	case GE_CMD_BJUMP:
		// bounding box jump. Let's just not jump, for now.
		sprintf(buffer, "BBOX JUMP - unimplemented");
		break;

	case GE_CMD_BOUNDINGBOX:
		// bounding box test. Let's do nothing.
		sprintf(buffer, "BBOX TEST - unimplemented");
		break;

	case GE_CMD_ORIGIN:
		sprintf(buffer, "Origin: %06x", data);
		break;

	case GE_CMD_VERTEXTYPE:
		sprintf(buffer, "SetVertexType: %06x", data);
		break;

	case GE_CMD_OFFSETADDR:
		sprintf(buffer, "OffsetAddr: %06x", data);
		break;

	case GE_CMD_REGION1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			//topleft
			sprintf(buffer, "Region TL: %d %d", x1, y1);
		}
		break;

	case GE_CMD_REGION2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			sprintf(buffer, "Region BR: %d %d", x2, y2);
		}
		break;

	case GE_CMD_CLIPENABLE:
		sprintf(buffer, "Clip Enable: %i", data);
		break;

	case GE_CMD_CULLFACEENABLE:
		sprintf(buffer, "CullFace Enable: %i", data);
		break;

	case GE_CMD_TEXTUREMAPENABLE:
		sprintf(buffer, "Texture map enable: %i", data);
		break;

	case GE_CMD_LIGHTINGENABLE:
		sprintf(buffer, "Lighting enable: %i", data);
		break;

	case GE_CMD_FOGENABLE:
		sprintf(buffer, "Fog Enable: %i", data);
		break;

	case GE_CMD_DITHERENABLE:
		sprintf(buffer, "Dither Enable: %i", data);
		break;

	case GE_CMD_OFFSETX:
		sprintf(buffer, "Offset X: %i", data);
		break;

	case GE_CMD_OFFSETY:
		sprintf(buffer, "Offset Y: %i", data);
		break;

	case GE_CMD_TEXSCALEU:
		sprintf(buffer, "Texture U Scale: %f", gstate_c.uScale);
		break;

	case GE_CMD_TEXSCALEV:
		sprintf(buffer, "Texture V Scale: %f", gstate_c.vScale);
		break;

	case GE_CMD_TEXOFFSETU:
		sprintf(buffer, "Texture U Offset: %f", gstate_c.uOff);
		break;

	case GE_CMD_TEXOFFSETV:
		sprintf(buffer, "Texture V Offset: %f", gstate_c.vOff);
		break;

	case GE_CMD_SCISSOR1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			sprintf(buffer, "Scissor TL: %i, %i", x1,y1);
		}
		break;
	case GE_CMD_SCISSOR2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
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
			sprintf(buffer, "FramebufPtr: %08x", ptr);
		}
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		{
			sprintf(buffer, "FramebufWidth: %i", data);
		}
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		sprintf(buffer, "FramebufPixeFormat: %i", data);
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
			sprintf(buffer, "Clut format: %06x", data);
		}
		break;

	case GE_CMD_TRANSFERSRC:
		{
			sprintf(buffer, "Block Transfer Src: %06x", data);
			// Nothing to do, the next one prints
		}
		break;

	case GE_CMD_TRANSFERSRCW:
		{
			u32 xferSrc = gstate.transfersrc | ((data&0xFF0000)<<8);
			u32 xferSrcW = gstate.transfersrcw & 1023;
			sprintf(buffer, "Block Transfer Src: %08x	W: %i", xferSrc, xferSrcW);
			break;
		}

	case GE_CMD_TRANSFERDST:
		{
			// Nothing to do, the next one prints
			sprintf(buffer, "Block Transfer Dst: %06x", data);
		}
		break;

	case GE_CMD_TRANSFERDSTW:
		{
			u32 xferDst= gstate.transferdst | ((data&0xFF0000)<<8);
			u32 xferDstW = gstate.transferdstw & 1023;
			sprintf(buffer, "Block Transfer Dest: %08x	W: %i", xferDst, xferDstW);
			break;
		}

	case GE_CMD_TRANSFERSRCPOS:
		{
			u32 x = (data & 1023)+1;
			u32 y = ((data>>10) & 1023)+1;
			sprintf(buffer, "Block Transfer Src Rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERDSTPOS:
		{
			u32 x = (data & 1023)+1;
			u32 y = ((data>>10) & 1023)+1;
			sprintf(buffer, "Block Transfer Dest Rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERSIZE:
		{
			u32 w = (data & 1023)+1;
			u32 h = ((data>>10) & 1023)+1;
			sprintf(buffer, "Block Transfer Rect Size: %i x %i", w, h);
			break;
		}

	case GE_CMD_TRANSFERSTART:  // Orphis calls this TRXKICK
		{
			sprintf(buffer, "Block Transfer Start");
			break;
		}

	case GE_CMD_TEXSIZE0:
	case GE_CMD_TEXSIZE1:
	case GE_CMD_TEXSIZE2:
	case GE_CMD_TEXSIZE3:
	case GE_CMD_TEXSIZE4:
	case GE_CMD_TEXSIZE5:
	case GE_CMD_TEXSIZE6:
	case GE_CMD_TEXSIZE7:
		sprintf(buffer, "Texture Size %i: %06x", cmd - GE_CMD_TEXSIZE0, data);
		break;

	case GE_CMD_ZBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			sprintf(buffer, "Zbuf Ptr: %06x", ptr);
		}
		break;

	case GE_CMD_ZBUFWIDTH:
		{
			sprintf(buffer, "Zbuf Width: %06x", data);
		}
		break;

	case GE_CMD_AMBIENTCOLOR:
		sprintf(buffer, "Ambient Color: %06x",	data);
		break;

	case GE_CMD_AMBIENTALPHA:
		sprintf(buffer, "Ambient Alpha: %06x",	data);
		break;

	case GE_CMD_MATERIALAMBIENT:
		sprintf(buffer, "Material Ambient Color: %06x",	data);
		break;

	case GE_CMD_MATERIALDIFFUSE:
		sprintf(buffer, "Material Diffuse Color: %06x",	data);
		break;

	case GE_CMD_MATERIALEMISSIVE:
		sprintf(buffer, "Material Emissive Color: %06x",	data);
		break;

	case GE_CMD_MATERIALSPECULAR:
		sprintf(buffer, "Material Specular Color: %06x",	data);
		break;

	case GE_CMD_MATERIALALPHA:
		sprintf(buffer, "Material Alpha Color: %06x",	data);
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		sprintf(buffer, "Material specular coef: %f", getFloat24(data));
		break;

	case GE_CMD_SHADEMODE:
		sprintf(buffer, "Shade: %06x (%s)", data, data ? "gouraud" : "flat");
		break;

	case GE_CMD_LIGHTMODE:
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
			sprintf(buffer, "Z scale: %f", zScale);
		}
		break;
	case GE_CMD_VIEWPORTZ2:
		{
			float zOff = getFloat24(data) / 65535.f;
			sprintf(buffer, "Z pos: %f", zOff);
		}
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		sprintf(buffer, "Light %i enable: %d", cmd-GE_CMD_LIGHTENABLE0, data);
		break;

	case GE_CMD_CULL:
		sprintf(buffer, "cull: %06x", data);
		break;

	case GE_CMD_PATCHDIVISION:
		{
			int patch_div_s = data & 0xFF;
			int patch_div_t = (data >> 8) & 0xFF;
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
		sprintf(buffer, "Material Update: %d", data);
		break;


	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
		// If it becomes a performance problem, check diff&1
		sprintf(buffer, "Clear mode: %06x", data);
		break;


	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
		sprintf(buffer, "Alpha blend enable: %d", data);
		break;

	case GE_CMD_BLENDMODE:
		sprintf(buffer, "Blend mode: %06x", data);
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
		sprintf(buffer, "Alpha test settings");
		break;

	case GE_CMD_ANTIALIASENABLE:
		sprintf(buffer, "Antialias enable: %d", data);
		break;

	case GE_CMD_PATCHCULLENABLE:
		sprintf(buffer, "Antialias enable: %d", data);
		break;

	case GE_CMD_COLORTESTENABLE:
		sprintf(buffer, "Color Test enable: %d", data);
		break;

	case GE_CMD_LOGICOPENABLE:
		sprintf(buffer, "Logic op enable: %d", data);
		break;

	case GE_CMD_TEXFUNC:
		sprintf(buffer, "TexFunc %i", data&7);
		break;

	case GE_CMD_TEXFILTER:
		{
			int min = data & 7;
			int mag = (data >> 8) & 1;
			sprintf(buffer, "TexFilter min: %i mag: %i", min, mag);
		}
		break;

	case GE_CMD_TEXENVCOLOR:
		sprintf(buffer, "TexEnvColor %06x", data);
		break;

	case GE_CMD_TEXMODE:
		sprintf(buffer, "TexMode %08x", data);
		break;

	case GE_CMD_TEXFORMAT:
		sprintf(buffer, "TexFormat %08x", data);
		break;

	case GE_CMD_TEXFLUSH:
		sprintf(buffer, "TexFlush");
		break;

	case GE_CMD_TEXSYNC:
		sprintf(buffer, "TexSync");
		break;

	case GE_CMD_TEXWRAP:
		sprintf(buffer, "TexWrap %08x", data);
		break;

	case GE_CMD_TEXLEVEL:
		sprintf(buffer, "TexWrap Mode: %i Offset: %i", data&3, data >> 16);
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
		sprintf(buffer, "Z test enable: %d", data & 1);
		break;

	case GE_CMD_STENCILOP:
		sprintf(buffer, "Stencil op: %06x", data);
		break;

	case GE_CMD_STENCILTEST:
		sprintf(buffer, "Stencil test: %06x", data);
		break;

	case GE_CMD_STENCILTESTENABLE:
		sprintf(buffer, "Stencil test enable: %d", data);
		break;

	case GE_CMD_ZTEST:
		sprintf(buffer, "Z test mode: %i", data);
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
		sprintf(buffer, "LogicOp: %06x", data);
		break;

	case GE_CMD_ZWRITEDISABLE:
		sprintf(buffer, "ZMask: %06x", data);
		break;

	case GE_CMD_COLORTEST:
		sprintf(buffer, "ColorTest: %06x", data);
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
		sprintf(buffer, "World # %i", data & 0xF);
		break;

	case GE_CMD_WORLDMATRIXDATA:
		sprintf(buffer, "World data # %f", getFloat24(data));
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		sprintf(buffer, "VIEW # %i", data & 0xF);
		break;

	case GE_CMD_VIEWMATRIXDATA:
		sprintf(buffer, "VIEW data # %f", getFloat24(data));
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		sprintf(buffer, "PROJECTION # %i", data & 0xF);
		break;

	case GE_CMD_PROJMATRIXDATA:
		sprintf(buffer, "PROJECTION matrix data # %f", getFloat24(data));
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		sprintf(buffer, "TGEN # %i", data & 0xF);
		break;

	case GE_CMD_TGENMATRIXDATA:
		sprintf(buffer, "TGEN data # %f", getFloat24(data));
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		sprintf(buffer, "BONE #%i", data);
		break;

	case GE_CMD_BONEMATRIXDATA:
		sprintf(buffer, "BONE data #%i %f", gstate.boneMatrixNumber & 0x7f, getFloat24(data));
		break;

	default:
		sprintf(buffer, "Unknown: %08x", op);
		break;
	}
}

