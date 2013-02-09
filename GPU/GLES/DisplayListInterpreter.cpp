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
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "gfx_es2/gl_state.h"

#include "../GPUState.h"
#include "../ge_constants.h"
#include "../GeDisasm.h"

#include "ShaderManager.h"
#include "DisplayListInterpreter.h"
#include "Framebuffer.h"
#include "TransformPipeline.h"
#include "TextureCache.h"

#include "../../Core/HLE/sceKernelThread.h"
#include "../../Core/HLE/sceKernelInterrupt.h"
#include "../../Core/HLE/sceGe.h"

extern u32 curTextureWidth;
extern u32 curTextureHeight;

static const int flushOnChangedBeforeCommandList[] = {
	GE_CMD_VERTEXTYPE,
	GE_CMD_BLENDMODE,
	GE_CMD_BLENDFIXEDA,
	GE_CMD_BLENDFIXEDB,
	GE_CMD_TEXOFFSETU,
	GE_CMD_TEXOFFSETV,
	GE_CMD_TEXSCALEU,
	GE_CMD_TEXSCALEV,
	GE_CMD_CULLFACEENABLE,
	GE_CMD_TEXTUREMAPENABLE,
	GE_CMD_LIGHTINGENABLE,
	GE_CMD_FOGENABLE,
	GE_CMD_ALPHABLENDENABLE,
	GE_CMD_ALPHATESTENABLE,
	GE_CMD_ALPHATEST,
	GE_CMD_COLORTESTENABLE,
	GE_CMD_COLORTEST,
	GE_CMD_COLORTESTMASK,
	GE_CMD_COLORREF,
	GE_CMD_MINZ,GE_CMD_MAXZ,
	GE_CMD_FOG1,
	GE_CMD_FOG2,
	GE_CMD_FOGCOLOR,
	GE_CMD_SHADEMODE,
	GE_CMD_REVERSENORMAL,
	GE_CMD_MATERIALUPDATE,
	GE_CMD_MATERIALEMISSIVE,
	GE_CMD_MATERIALAMBIENT,
	GE_CMD_MATERIALDIFFUSE,
	GE_CMD_MATERIALSPECULAR,
	GE_CMD_MATERIALALPHA,
	GE_CMD_MATERIALSPECULARCOEF,
	GE_CMD_AMBIENTCOLOR,
	GE_CMD_AMBIENTALPHA,
	GE_CMD_LIGHTMODE,
	GE_CMD_LIGHTTYPE0, GE_CMD_LIGHTTYPE1, GE_CMD_LIGHTTYPE2, GE_CMD_LIGHTTYPE3,
	GE_CMD_LX0,GE_CMD_LY0,GE_CMD_LZ0,
	GE_CMD_LX1,GE_CMD_LY1,GE_CMD_LZ1,
	GE_CMD_LX2,GE_CMD_LY2,GE_CMD_LZ2,
	GE_CMD_LX3,GE_CMD_LY3,GE_CMD_LZ3,
	GE_CMD_LDX0,GE_CMD_LDY0,GE_CMD_LDZ0,
	GE_CMD_LDX1,GE_CMD_LDY1,GE_CMD_LDZ1,
	GE_CMD_LDX2,GE_CMD_LDY2,GE_CMD_LDZ2,
	GE_CMD_LDX3,GE_CMD_LDY3,GE_CMD_LDZ3,
	GE_CMD_LKA0,GE_CMD_LKB0,GE_CMD_LKC0,
	GE_CMD_LKA1,GE_CMD_LKB1,GE_CMD_LKC1,
	GE_CMD_LKA2,GE_CMD_LKB2,GE_CMD_LKC2,
	GE_CMD_LKA3,GE_CMD_LKB3,GE_CMD_LKC3,
	GE_CMD_LKS0,GE_CMD_LKS1,GE_CMD_LKS2,GE_CMD_LKS3,
	GE_CMD_LKO0,GE_CMD_LKO1,GE_CMD_LKO2,GE_CMD_LKO3,
	GE_CMD_LAC0,GE_CMD_LDC0,GE_CMD_LSC0,
	GE_CMD_LAC1,GE_CMD_LDC1,GE_CMD_LSC1,
	GE_CMD_LAC2,GE_CMD_LDC2,GE_CMD_LSC2,
	GE_CMD_LAC3,GE_CMD_LDC3,GE_CMD_LSC3,
	GE_CMD_VIEWPORTX1,GE_CMD_VIEWPORTY1,
	GE_CMD_VIEWPORTX2,GE_CMD_VIEWPORTY2,
	GE_CMD_VIEWPORTZ1,GE_CMD_VIEWPORTZ2,
	GE_CMD_LIGHTENABLE0,GE_CMD_LIGHTENABLE1,GE_CMD_LIGHTENABLE2,GE_CMD_LIGHTENABLE3,
	GE_CMD_CULL,
	GE_CMD_PATCHDIVISION,
	GE_CMD_MATERIALUPDATE,
	GE_CMD_CLEARMODE,
	GE_CMD_TEXMAPMODE,
	GE_CMD_TEXSHADELS,
	GE_CMD_TEXFUNC,
	GE_CMD_TEXFILTER,
	GE_CMD_TEXENVCOLOR,
	GE_CMD_TEXMODE,
	GE_CMD_TEXFORMAT,
	GE_CMD_TEXWRAP,
	GE_CMD_ZTESTENABLE,
	GE_CMD_ZWRITEDISABLE,
	GE_CMD_STENCILTESTENABLE,
	GE_CMD_STENCILOP,
	GE_CMD_STENCILTEST,
	GE_CMD_ZTEST,
	GE_CMD_MASKRGB,
	GE_CMD_MASKALPHA,
};

static const int flushBeforeCommandList[] = {
	GE_CMD_BEZIER,
	GE_CMD_SPLINE,
	GE_CMD_SIGNAL,
	GE_CMD_FINISH,
	GE_CMD_BJUMP,
	GE_CMD_REGION1,GE_CMD_REGION2,
	GE_CMD_FRAMEBUFPTR,
	GE_CMD_FRAMEBUFWIDTH,
	GE_CMD_FRAMEBUFPIXFORMAT,
	GE_CMD_TEXADDR0,
	GE_CMD_CLUTADDR,
	GE_CMD_LOADCLUT,
	GE_CMD_CLUTFORMAT,
	GE_CMD_TRANSFERSTART,
	GE_CMD_TEXBUFWIDTH0,
	GE_CMD_TEXSIZE0,GE_CMD_TEXSIZE1,GE_CMD_TEXSIZE2,GE_CMD_TEXSIZE3,
	GE_CMD_TEXSIZE4,GE_CMD_TEXSIZE5,GE_CMD_TEXSIZE6,GE_CMD_TEXSIZE7,
	GE_CMD_ZBUFPTR,
	GE_CMD_ZBUFWIDTH,
	GE_CMD_OFFSETY,
	GE_CMD_OFFSETX,
	GE_CMD_OFFSETY,
	GE_CMD_TEXFLUSH,
	GE_CMD_MORPHWEIGHT0,GE_CMD_MORPHWEIGHT1,GE_CMD_MORPHWEIGHT2,GE_CMD_MORPHWEIGHT3,
	GE_CMD_MORPHWEIGHT4,GE_CMD_MORPHWEIGHT5,GE_CMD_MORPHWEIGHT6,GE_CMD_MORPHWEIGHT7,
	// These handle their own flushing.
	/*
	GE_CMD_WORLDMATRIXNUMBER,
	GE_CMD_WORLDMATRIXDATA,
	GE_CMD_VIEWMATRIXNUMBER,
	GE_CMD_VIEWMATRIXDATA,
	GE_CMD_PROJMATRIXNUMBER,
	GE_CMD_PROJMATRIXDATA,
	GE_CMD_TGENMATRIXNUMBER,
	GE_CMD_TGENMATRIXDATA,
	GE_CMD_BONEMATRIXNUMBER,
	GE_CMD_BONEMATRIXDATA,
	*/
};

GLES_GPU::GLES_GPU()
:		interruptsEnabled_(true),
		resized_(false)
{
	shaderManager_ = new ShaderManager();
	transformDraw_.SetShaderManager(shaderManager_);
	transformDraw_.SetTextureCache(&textureCache_);
	transformDraw_.SetFramebufferManager(&framebufferManager_);
	framebufferManager_.SetTextureCache(&textureCache_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	flushBeforeCommand_ = new u8[256];
	memset(flushBeforeCommand_, 0, 256 * sizeof(bool));
	for (size_t i = 0; i < ARRAY_SIZE(flushOnChangedBeforeCommandList); i++) {
		flushBeforeCommand_[flushOnChangedBeforeCommandList[i]] = 2;
	}
	for (size_t i = 0; i < ARRAY_SIZE(flushBeforeCommandList); i++) {
		flushBeforeCommand_[flushBeforeCommandList[i]] = 1;
	}
	flushBeforeCommand_[1] = 0;
}

GLES_GPU::~GLES_GPU() {
	framebufferManager_.DestroyAllFBOs();
	shaderManager_->ClearCache(true);
	delete shaderManager_;
	delete [] flushBeforeCommand_;
}

void GLES_GPU::DeviceLost() {
	// Simply drop all caches and textures.
	// FBO:s appear to survive? Or no?
	shaderManager_->ClearCache(false);
	textureCache_.Clear(false);
}

void GLES_GPU::InitClear() {
	if (!g_Config.bBufferedRendering) {
		glClearColor(0,0,0,1);
		//	glClearColor(1,0,1,1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
}

void GLES_GPU::DumpNextFrame() {
	dumpNextFrame_ = true;
}

void GLES_GPU::BeginDebugDraw() {
	if (g_Config.bDrawWireframe) {
#ifndef USING_GLES2
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
	}
}
void GLES_GPU::EndDebugDraw() {
#ifndef USING_GLES2
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}

void GLES_GPU::BeginFrame() {
	textureCache_.StartFrame();
	transformDraw_.DecimateTrackedVertexArrays();

	if (dumpNextFrame_) {
		NOTICE_LOG(G3D, "DUMPING THIS FRAME");
		dumpThisFrame_ = true;
		dumpNextFrame_ = false;
	} else if (dumpThisFrame_) {
		dumpThisFrame_ = false;
	}
	shaderManager_->DirtyShader();

	// Not sure if this is really needed.
	shaderManager_->DirtyUniform(DIRTY_ALL);

	framebufferManager_.BeginFrame();
}

void GLES_GPU::SetDisplayFramebuffer(u32 framebuf, u32 stride, int format) {
	framebufferManager_.SetDisplayFramebuffer(framebuf, stride, format);
}

void GLES_GPU::CopyDisplayToOutput() {
	transformDraw_.Flush();
	if (!g_Config.bBufferedRendering)
		return;

	EndDebugDraw();

	framebufferManager_.CopyDisplayToOutput();

	shaderManager_->DirtyShader();
	shaderManager_->DirtyUniform(DIRTY_ALL);
	gstate_c.textureChanged = true;

	BeginDebugDraw();
}

// Render queue

void GLES_GPU::DrawSync(int mode)
{
	transformDraw_.Flush();
}

void GLES_GPU::Continue() {

}

void GLES_GPU::Break() {

}

static void EnterClearMode(u32 data) {
	bool colMask = (data >> 8) & 1;
	bool alphaMask = (data >> 9) & 1;
	bool updateZ = (data >> 10) & 1;
	glstate.colorMask.set(colMask, colMask, colMask, alphaMask);
	glstate.depthWrite.set(updateZ ? GL_TRUE : GL_FALSE);
}

static void LeaveClearMode() {
	// We have to reset the following state as per the state of the command registers:
	// Back face culling
	// Texture map enable	(meh)
	// Fogging
	// Antialiasing
	// Alpha test
	glstate.colorMask.set(1,1,1,1);
	glstate.depthWrite.set(!(gstate.zmsk & 1) ? GL_TRUE : GL_FALSE);
	// dirtyshader?
}

void GLES_GPU::PreExecuteOp(u32 op, u32 diff) {
	u32 cmd = op >> 24;
	if (flushBeforeCommand_[cmd] == 1 || (diff && flushBeforeCommand_[cmd] == 2))
	{
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		transformDraw_.Flush();
	}
}

void GLES_GPU::ExecuteOp(u32 op, u32 diff) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_BASE:
		break;

	case GE_CMD_VADDR:
		gstate_c.vertexAddr = gstate_c.getRelativeAddress(data);
		break;

	case GE_CMD_IADDR:
		gstate_c.indexAddr	= gstate_c.getRelativeAddress(data);
		break;

	case GE_CMD_PRIM:
		{
			framebufferManager_.SetRenderFrameBuffer();

			u32 count = data & 0xFFFF;
			u32 type = data >> 16;

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			// TODO: Split this so that we can collect sequences of primitives, can greatly speed things up
			// on platforms where draw calls are expensive like mobile and D3D
			void *verts = Memory::GetPointer(gstate_c.vertexAddr);
			void *inds = 0;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG(G3D, "Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				inds = Memory::GetPointer(gstate_c.indexAddr);
			}

			int bytesRead;
			transformDraw_.SubmitPrim(verts, inds, type, count, gstate.vertType, -1, &bytesRead);
			// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
			// Some games rely on this, they don't bother reloading VADDR and IADDR.
			// Q: Are these changed reflected in the real registers? Needs testing.
			if (inds) {
				int indexSize = 1;
				if ((gstate.vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT)
					indexSize = 2;
				gstate_c.indexAddr += count * indexSize;
			} else {
				gstate_c.vertexAddr += bytesRead;
			}
		}
		break;

	// The arrow and other rotary items in Puzbob are bezier patches, strangely enough.
	case GE_CMD_BEZIER:
		{
			int bz_ucount = data & 0xFF;
			int bz_vcount = (data >> 8) & 0xFF;
			transformDraw_.DrawBezier(bz_ucount, bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;
			transformDraw_.DrawSpline(sp_ucount, sp_vcount, sp_utype, sp_vtype);
		}
		break;

	case GE_CMD_JUMP:
		{
			u32 target = gstate_c.getRelativeAddress(data);
			if (Memory::IsValidAddress(target)) {
				currentList->pc = target - 4; // pc will be increased after we return, counteract that
			} else {
				ERROR_LOG(G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, data);
			}
		}
		break;

	case GE_CMD_CALL:
		{
			// Saint Seiya needs correct support for relative calls.
			u32 retval = currentList->pc + 4;
			u32 target = gstate_c.getRelativeAddress(data);
			if (stackptr == ARRAY_SIZE(stack)) {
				ERROR_LOG(G3D, "CALL: Stack full!");
			} else if (!Memory::IsValidAddress(target)) {
				ERROR_LOG(G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, data);
			} else {
				stack[stackptr++] = retval;
				currentList->pc = target - 4;	// pc will be increased after we return, counteract that
			}
		}
		break;

	case GE_CMD_RET:
		{
			if (stackptr == 0) {
				ERROR_LOG(G3D, "RET: Stack empty!");
			} else {
				u32 target = (currentList->pc & 0xF0000000) | (stack[--stackptr] & 0x0FFFFFFF);
				//target = (target + gstate_c.originAddr) & 0xFFFFFFF;
				currentList->pc = target - 4;
				if (!Memory::IsValidAddress(currentList->pc)) {
					ERROR_LOG(G3D, "Invalid DL PC %08x on return", currentList->pc);
					finished = true;
				}
			}
		}
		break;

	case GE_CMD_OFFSETADDR:
		gstate_c.offsetAddr = data << 8;
		// ???
		break;

	case GE_CMD_ORIGIN:
		gstate_c.offsetAddr = currentList->pc;
		break;


	case GE_CMD_SIGNAL:
		{
			// Processed in GE_END. Has data.
			currentList->subIntrToken = data & 0xFFFF;
		}
		break;

	case GE_CMD_FINISH:
		currentList->subIntrToken = data & 0xFFFF;
		// TODO: Should this run while interrupts are suspended?
		if (interruptsEnabled_)
			__GeTriggerInterrupt(currentList->id, currentList->pc);
		break;

	case GE_CMD_END:
		switch (prev >> 24) {
		case GE_CMD_SIGNAL:
			{
				currentList->status = PSP_GE_LIST_END_REACHED;
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				int behaviour = (prev >> 16) & 0xFF;
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFF;
				// We should probably defer to sceGe here, no sense in implementing this stuff in every GPU
				switch (behaviour) {
				case 1:  // Signal with Wait
					ERROR_LOG(G3D, "Signal with Wait UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 2:
					ERROR_LOG(G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
					break;
				case 3:
					ERROR_LOG(G3D, "Signal with Pause UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x10:
					ERROR_LOG(G3D, "Signal with Jump UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x11:
					ERROR_LOG(G3D, "Signal with Call UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x12:
					ERROR_LOG(G3D, "Signal with Return UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				default:
					ERROR_LOG(G3D, "UNKNOWN Signal UNIMPLEMENTED %i ! signal/end: %04x %04x", behaviour, signal, enddata);
					break;
				}
				// TODO: Should this run while interrupts are suspended?
				if (interruptsEnabled_)
					__GeTriggerInterrupt(currentList->id, currentList->pc);
			}
			break;
		case GE_CMD_FINISH:
			currentList->status = PSP_GE_LIST_DONE;
			finished = true;
			break;
		default:
			DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
			break;
		}
		break;

	case GE_CMD_BJUMP:
		// bounding box jump. Let's just not jump, for now.
		break;

	case GE_CMD_BOUNDINGBOX:
		// bounding box test. Let's do nothing.
		break;

	case GE_CMD_VERTEXTYPE:
		if (diff & GE_VTYPE_THROUGH) {
			// Throughmode changed, let's make the proj matrix dirty.
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
		}
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		// This sets through-mode or not, as well.
		break;

	case GE_CMD_REGION1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			//topleft
		}
		break;

	case GE_CMD_REGION2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
		}
		break;

	case GE_CMD_CLIPENABLE:
		//we always clip, this is opengl
		break;

	case GE_CMD_CULLFACEENABLE:
		break;

	case GE_CMD_TEXTUREMAPENABLE:
		gstate_c.textureChanged = true;
		break;

	case GE_CMD_LIGHTINGENABLE:
		break;

	case GE_CMD_FOGCOLOR:
		shaderManager_->DirtyUniform(DIRTY_FOGCOLOR);
		break;

	case GE_CMD_FOG1:
		shaderManager_->DirtyUniform(DIRTY_FOGCOEF);
		break;

	case GE_CMD_FOG2:
		shaderManager_->DirtyUniform(DIRTY_FOGCOEF);
		break;

	case GE_CMD_FOGENABLE:
		break;

	case GE_CMD_DITHERENABLE:
		break;

	case GE_CMD_OFFSETX:
		break;

	case GE_CMD_OFFSETY:
		break;

	case GE_CMD_TEXSCALEU:
		gstate_c.uScale = getFloat24(data);
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_TEXSCALEV:
		gstate_c.vScale = getFloat24(data);
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_TEXOFFSETU:
		gstate_c.uOff = getFloat24(data);
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_TEXOFFSETV:
		gstate_c.vOff = getFloat24(data);
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_SCISSOR1:
	case GE_CMD_SCISSOR2:
		break;

	case GE_CMD_MINZ:
		gstate_c.zMin = getFloat24(data) / 65535.f;
		break;

	case GE_CMD_MAXZ:
		gstate_c.zMax = getFloat24(data) / 65535.f;
		break;

	case GE_CMD_FRAMEBUFPTR:
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		break;

	case GE_CMD_TEXADDR0:
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		gstate_c.textureChanged = true;
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		gstate_c.textureChanged = true;
		break;

	case GE_CMD_CLUTADDR:
		gstate_c.textureChanged = true;
		break;

	case GE_CMD_CLUTADDRUPPER:
		gstate_c.textureChanged = true;
		break;

	case GE_CMD_LOADCLUT:
		gstate_c.textureChanged = true;
		// This could be used to "dirty" textures with clut.
		break;

	case GE_CMD_TEXMAPMODE:
		break;

	case GE_CMD_TEXSHADELS:
		break;

	case GE_CMD_CLUTFORMAT:
		gstate_c.textureChanged = true;
		break;

	case GE_CMD_TRANSFERSRC:
	case GE_CMD_TRANSFERSRCW:
	case GE_CMD_TRANSFERDST:
	case GE_CMD_TRANSFERDSTW:
	case GE_CMD_TRANSFERSRCPOS:
	case GE_CMD_TRANSFERDSTPOS:
		break;

	case GE_CMD_TRANSFERSIZE:
		break;

	case GE_CMD_TRANSFERSTART:  // Orphis calls this TRXKICK
		{
			// TODO: Here we should check if the transfer overlaps a framebuffer or any textures,
			// and take appropriate action. This is a block transfer between RAM and VRAM, or vice versa.
			DoBlockTransfer();
			break;
		}

	case GE_CMD_TEXSIZE0:
		gstate_c.curTextureWidth = 1 << (gstate.texsize[0] & 0xf);
		gstate_c.curTextureHeight = 1 << ((gstate.texsize[0]>>8) & 0xf);
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		//fall thru - ignoring the mipmap sizes for now
	case GE_CMD_TEXSIZE1:
	case GE_CMD_TEXSIZE2:
	case GE_CMD_TEXSIZE3:
	case GE_CMD_TEXSIZE4:
	case GE_CMD_TEXSIZE5:
	case GE_CMD_TEXSIZE6:
	case GE_CMD_TEXSIZE7:
		gstate_c.textureChanged = true;
		break;

	case GE_CMD_ZBUFPTR:
	case GE_CMD_ZBUFWIDTH:
		break;

	case GE_CMD_AMBIENTCOLOR:
	case GE_CMD_AMBIENTALPHA:
		break;

	case GE_CMD_MATERIALAMBIENT:
		if (diff)
			shaderManager_->DirtyUniform(DIRTY_MATAMBIENTALPHA);
		break;

	case GE_CMD_MATERIALDIFFUSE:
		if (diff)
			shaderManager_->DirtyUniform(DIRTY_MATDIFFUSE);
		break;

	case GE_CMD_MATERIALEMISSIVE:
		if (diff)
			shaderManager_->DirtyUniform(DIRTY_MATEMISSIVE);
		break;

	case GE_CMD_MATERIALSPECULAR:
		if (diff)
			shaderManager_->DirtyUniform(DIRTY_MATSPECULAR);
		break;

	case GE_CMD_MATERIALALPHA:
		if (diff)
			shaderManager_->DirtyUniform(DIRTY_MATAMBIENTALPHA);
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		if (diff)
			shaderManager_->DirtyUniform(DIRTY_MATSPECULAR);
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		break;

	case GE_CMD_LX0:case GE_CMD_LY0:case GE_CMD_LZ0:
	case GE_CMD_LX1:case GE_CMD_LY1:case GE_CMD_LZ1:
	case GE_CMD_LX2:case GE_CMD_LY2:case GE_CMD_LZ2:
	case GE_CMD_LX3:case GE_CMD_LY3:case GE_CMD_LZ3:
		{
			int n = cmd - GE_CMD_LX0;
			int l = n / 3;
			int c = n % 3;
			gstate_c.lightpos[l][c] = getFloat24(data);
			if (diff)
				shaderManager_->DirtyUniform(DIRTY_LIGHT0 << l);
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
			gstate_c.lightdir[l][c] = getFloat24(data);
			if (diff)
				shaderManager_->DirtyUniform(DIRTY_LIGHT0 << l);
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
			gstate_c.lightatt[l][c] = getFloat24(data);
			if (diff)
				shaderManager_->DirtyUniform(DIRTY_LIGHT0 << l);
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
			gstate_c.lightColor[t][l][0] = r;
			gstate_c.lightColor[t][l][1] = g;
			gstate_c.lightColor[t][l][2] = b;
			if (diff)
				shaderManager_->DirtyUniform(DIRTY_LIGHT0 << l);
		}
		break;

	case GE_CMD_VIEWPORTX1:
	case GE_CMD_VIEWPORTY1:
	case GE_CMD_VIEWPORTX2:
	case GE_CMD_VIEWPORTY2:
	case GE_CMD_VIEWPORTZ1:
	case GE_CMD_VIEWPORTZ2:
	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		break;

	case GE_CMD_CULL:
		break;

	case GE_CMD_SHADEMODE:
		break;

	case GE_CMD_PATCHDIVISION:
	case GE_CMD_PATCHPRIMITIVE:
	case GE_CMD_PATCHFACING:
		break;


	case GE_CMD_MATERIALUPDATE:
		break;

	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
		// If it becomes a performance problem, check diff&1
		if (data & 1)
			EnterClearMode(data);
		else
			LeaveClearMode();
		break;


	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
	case GE_CMD_BLENDMODE:
	case GE_CMD_BLENDFIXEDA:
	case GE_CMD_BLENDFIXEDB:
		break;

	case GE_CMD_ALPHATESTENABLE:
	case GE_CMD_COLORTESTENABLE:
		// This is done in the shader.
		break;

	case GE_CMD_COLORREF:
	case GE_CMD_ALPHATEST:
		shaderManager_->DirtyUniform(DIRTY_ALPHACOLORREF);
		break;

	case GE_CMD_TEXENVCOLOR:
		if (diff)
			shaderManager_->DirtyUniform(DIRTY_TEXENV);
		break;

	case GE_CMD_TEXFUNC:
	case GE_CMD_TEXFILTER:
	case GE_CMD_TEXMODE:
	case GE_CMD_TEXFORMAT:
	case GE_CMD_TEXFLUSH:
	case GE_CMD_TEXWRAP:
		break;

	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_STENCILTESTENABLE:
	case GE_CMD_ZTESTENABLE:
	case GE_CMD_ZTEST:
		break;

	case GE_CMD_MORPHWEIGHT0:
	case GE_CMD_MORPHWEIGHT1:
	case GE_CMD_MORPHWEIGHT2:
	case GE_CMD_MORPHWEIGHT3:
	case GE_CMD_MORPHWEIGHT4:
	case GE_CMD_MORPHWEIGHT5:
	case GE_CMD_MORPHWEIGHT6:
	case GE_CMD_MORPHWEIGHT7:
		gstate_c.morphWeights[cmd - GE_CMD_MORPHWEIGHT0] = getFloat24(data);
		break;

	case GE_CMD_DITH0:
	case GE_CMD_DITH1:
	case GE_CMD_DITH2:
	case GE_CMD_DITH3:
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		gstate.worldmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_WORLDMATRIXDATA:
		{
			int num = gstate.worldmtxnum & 0xF;
			float newVal = getFloat24(data);
			if (num < 12 && newVal != gstate.worldMatrix[num]) {
				Flush();
				gstate.worldMatrix[num] = getFloat24(data);
				shaderManager_->DirtyUniform(DIRTY_WORLDMATRIX);
			}
			num++;
			gstate.worldmtxnum = (gstate.worldmtxnum & 0xFF000000) | (num & 0xF);
		}
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		gstate.viewmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_VIEWMATRIXDATA:
		{
			int num = gstate.viewmtxnum & 0xF;
			float newVal = getFloat24(data);
			if (num < 12 && newVal != gstate.viewMatrix[num]) {
				Flush();
				gstate.viewMatrix[num] = newVal;
				shaderManager_->DirtyUniform(DIRTY_VIEWMATRIX);
			}
			num++;
			gstate.viewmtxnum = (gstate.viewmtxnum & 0xFF000000) | (num & 0xF);
		}
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		gstate.projmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_PROJMATRIXDATA:
		{
			int num = gstate.projmtxnum & 0xF;
			float newVal = getFloat24(data);
			if (newVal != gstate.projMatrix[num]) {
				Flush();
				gstate.projMatrix[num] = newVal;
				shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
			}
			num++;
			gstate.projmtxnum = (gstate.projmtxnum & 0xFF000000) | (num & 0xF);
		}
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		gstate.texmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_TGENMATRIXDATA:
		{
			int num = gstate.texmtxnum & 0xF;
			float newVal = getFloat24(data);
			if (num < 12 && newVal != gstate.tgenMatrix[num]) {
				Flush();
				gstate.tgenMatrix[num] = newVal;
				shaderManager_->DirtyUniform(DIRTY_TEXMATRIX);
			}
			num++;
			gstate.texmtxnum = (gstate.texmtxnum & 0xFF000000) | (num & 0xF);
		}
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		gstate.boneMatrixNumber &= 0xFF00007F;
		break;

	case GE_CMD_BONEMATRIXDATA:
		{
			int num = gstate.boneMatrixNumber & 0x7F;
			float newVal = getFloat24(data);
			if (num < 96 && newVal != gstate.boneMatrix[num]) {
				Flush();
				gstate.boneMatrix[num] = newVal;
				shaderManager_->DirtyUniform(DIRTY_BONEMATRIX0 << (num / 12));
			}
			num++;
			gstate.boneMatrixNumber = (gstate.boneMatrixNumber & 0xFF000000) | (num & 0x7F);
		}
		break;

	default:
		DEBUG_LOG(G3D,"DL Unknown: %08x @ %08x", op, currentList == NULL ? 0 : currentList->pc);
		break;
	}
}

void GLES_GPU::UpdateStats() {
	gpuStats.numVertexShaders = shaderManager_->NumVertexShaders();
	gpuStats.numFragmentShaders = shaderManager_->NumFragmentShaders();
	gpuStats.numShaders = shaderManager_->NumPrograms();
	gpuStats.numTextures = textureCache_.NumLoadedTextures();
	gpuStats.numFBOs = framebufferManager_.NumVFBs();
}

void GLES_GPU::DoBlockTransfer() {
	// TODO: This is used a lot to copy data around between render targets and textures,
	// and also to quickly load textures from RAM to VRAM. So we should do checks like the following:
	//  * Does dstBasePtr point to an existing texture? If so maybe reload it immediately.
	//
	//  * Does srcBasePtr point to a render target, and dstBasePtr to a texture? If so
	//    either copy between rt and texture or reassign the texture to point to the render target
	//
	// etc....

	u32 srcBasePtr = (gstate.transfersrc & 0xFFFFFF) | ((gstate.transfersrcw & 0xFF0000) << 8);
	u32 srcStride = gstate.transfersrcw & 0x3FF;

	u32 dstBasePtr = (gstate.transferdst & 0xFFFFFF) | ((gstate.transferdstw & 0xFF0000) << 8);
	u32 dstStride = gstate.transferdstw & 0x3FF;

	int srcX = gstate.transfersrcpos & 0x3FF;
	int srcY = (gstate.transfersrcpos >> 10) & 0x3FF;

	int dstX = gstate.transferdstpos & 0x3FF;
	int dstY = (gstate.transferdstpos >> 10) & 0x3FF;

	int width = (gstate.transfersize & 0x3FF) + 1;
	int height = ((gstate.transfersize >> 10) & 0x3FF) + 1;

	int bpp = (gstate.transferstart & 1) ? 4 : 2;

	DEBUG_LOG(G3D, "Block transfer: %08x to %08x, %i x %i , ...", srcBasePtr, dstBasePtr, width, height);

	// Do the copy!
	for (int y = 0; y < height; y++) {
		const u8 *src = Memory::GetPointer(srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp);
		u8 *dst = Memory::GetPointer(dstBasePtr + ((y + dstY) * srcStride + dstX) * bpp);
		memcpy(dst, src, width * bpp);
	}

	// TODO: Notify all overlapping FBOs that they need to reload.

	textureCache_.Invalidate(dstBasePtr + dstY * dstStride + dstX, height * dstStride + width * bpp, true);
}

void GLES_GPU::InvalidateCache(u32 addr, int size) {
	if (size > 0)
		textureCache_.Invalidate(addr, size, true);
	else
		textureCache_.InvalidateAll(true);
}

void GLES_GPU::InvalidateCacheHint(u32 addr, int size) {
	if (size > 0)
		textureCache_.Invalidate(addr, size, false);
	else
		textureCache_.InvalidateAll(false);
}

void GLES_GPU::Flush() {
	transformDraw_.Flush();
}

void GLES_GPU::Resized() {
	framebufferManager_.Resized();
}

void GLES_GPU::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	textureCache_.Clear(true);

	gstate_c.textureChanged = true;
	framebufferManager_.DestroyAllFBOs();
	shaderManager_->ClearCache(true);
}
