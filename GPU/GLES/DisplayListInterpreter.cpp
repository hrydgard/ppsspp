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

#include "../../Core/MemMap.h"
#include "../../Core/Host.h"
#include "../../Core/Config.h"
#include "../../Core/System.h"
#include "../../native/gfx_es2/gl_state.h"

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

extern u32 curTextureWidth;
extern u32 curTextureHeight;

// Aggressively delete unused FBO:s to save gpu memory.
enum {
	FBO_OLD_AGE = 4
};

const int flushOnChangedBeforeCommandList[] = {
	GE_CMD_VERTEXTYPE,
	GE_CMD_BLENDMODE,
	GE_CMD_BLENDFIXEDA,
	GE_CMD_BLENDFIXEDB,
};

const int flushBeforeCommandList[] = {
	GE_CMD_BEZIER,
	GE_CMD_SPLINE,
	GE_CMD_SIGNAL,
	GE_CMD_FINISH,
	GE_CMD_BJUMP,
	GE_CMD_OFFSETADDR,
	GE_CMD_REGION1,GE_CMD_REGION2,
	GE_CMD_CULLFACEENABLE,
	GE_CMD_TEXTUREMAPENABLE,
	GE_CMD_LIGHTINGENABLE,
	GE_CMD_FOGENABLE,
	GE_CMD_TEXSCALEU,GE_CMD_TEXSCALEV,
	GE_CMD_TEXOFFSETU,GE_CMD_TEXOFFSETV,
	GE_CMD_MINZ,GE_CMD_MAXZ,
	GE_CMD_FRAMEBUFPTR,
	GE_CMD_FRAMEBUFWIDTH,
	GE_CMD_FRAMEBUFPIXFORMAT,
	GE_CMD_TEXADDR0,
	GE_CMD_CLUTADDR,
	GE_CMD_LOADCLUT,
	GE_CMD_TEXMAPMODE,
	GE_CMD_TEXSHADELS,
	GE_CMD_CLUTFORMAT,
	GE_CMD_TRANSFERSTART,
	GE_CMD_TEXBUFWIDTH0,
	GE_CMD_TEXSIZE0,GE_CMD_TEXSIZE1,GE_CMD_TEXSIZE2,GE_CMD_TEXSIZE3,
	GE_CMD_TEXSIZE4,GE_CMD_TEXSIZE5,GE_CMD_TEXSIZE6,GE_CMD_TEXSIZE7,
	GE_CMD_ZBUFPTR,
	GE_CMD_ZBUFWIDTH,
	GE_CMD_AMBIENTCOLOR,
	GE_CMD_AMBIENTALPHA,
	GE_CMD_MATERIALAMBIENT,
	GE_CMD_MATERIALDIFFUSE,
	GE_CMD_MATERIALEMISSIVE,
	GE_CMD_MATERIALSPECULAR,
	GE_CMD_MATERIALALPHA,
	GE_CMD_MATERIALSPECULARCOEF,
	GE_CMD_COLORMODEL,
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
	GE_CMD_LMODE,
	GE_CMD_REVERSENORMAL,
	GE_CMD_PATCHDIVISION,
	GE_CMD_MATERIALUPDATE,
	GE_CMD_CLEARMODE,
	GE_CMD_ALPHABLENDENABLE,
	GE_CMD_ALPHATESTENABLE,
	GE_CMD_ALPHATEST,
	GE_CMD_TEXFUNC,
	GE_CMD_TEXFILTER,
	GE_CMD_TEXENVCOLOR,
	GE_CMD_TEXMODE,
	GE_CMD_TEXFORMAT,
	GE_CMD_TEXFLUSH,
	GE_CMD_TEXWRAP,
	GE_CMD_ZTESTENABLE,
	GE_CMD_STENCILTESTENABLE,
	GE_CMD_STENCILOP,
	GE_CMD_ZTEST,
	GE_CMD_FOG1,
	GE_CMD_FOG2,
	GE_CMD_FOGCOLOR,
	GE_CMD_MORPHWEIGHT0,GE_CMD_MORPHWEIGHT1,GE_CMD_MORPHWEIGHT2,GE_CMD_MORPHWEIGHT3,
	GE_CMD_MORPHWEIGHT4,GE_CMD_MORPHWEIGHT5,GE_CMD_MORPHWEIGHT6,GE_CMD_MORPHWEIGHT7,
	GE_CMD_WORLDMATRIXNUMBER,
	GE_CMD_VIEWMATRIXNUMBER,
	GE_CMD_PROJMATRIXNUMBER,
	GE_CMD_PROJMATRIXDATA,
	GE_CMD_TGENMATRIXNUMBER,
	GE_CMD_BONEMATRIXNUMBER,
	GE_CMD_MASKRGB,
	GE_CMD_MASKALPHA,
};

GLES_GPU::GLES_GPU(int renderWidth, int renderHeight)
:		interruptsEnabled_(true),
		displayFramebufPtr_(0),
		prevDisplayFramebufPtr_(0),
		prevPrevDisplayFramebufPtr_(0),
		renderWidth_(renderWidth),
		renderHeight_(renderHeight)
{
	renderWidthFactor_ = (float)renderWidth / 480.0f;
	renderHeightFactor_ = (float)renderHeight / 272.0f;
	shaderManager_ = new ShaderManager();
	transformDraw_.SetShaderManager(shaderManager_);
	TextureCache_Init();
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
	TextureCache_Shutdown();
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		fbo_destroy((*iter)->fbo);
		delete (*iter);
	}
	vfbs_.clear();
	shaderManager_->ClearCache(true);
	delete shaderManager_;
	delete [] flushBeforeCommand_;
}

void GLES_GPU::DeviceLost() {
	// Simply drop all caches and textures.
	// FBO:s appear to survive? Or no?
	shaderManager_->ClearCache(false);
	TextureCache_Clear(false);
}

void GLES_GPU::InitClear() {
	if (!g_Config.bBufferedRendering) {
		glClearColor(0,0,0,1);
		//	glClearColor(1,0,1,1);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	}
	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
}

void GLES_GPU::DumpNextFrame() {
	dumpNextFrame_ = true;
}

void GLES_GPU::BeginFrame() {
	TextureCache_StartFrame();
	DecimateFBOs();

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

	// NOTE - this is all wrong. At the beginning of the frame is a TERRIBLE time to draw the fb.
	if (g_Config.bDisplayFramebuffer && displayFramebufPtr_) {
		INFO_LOG(HLE, "Drawing the framebuffer");
		const u8 *pspframebuf = Memory::GetPointer((0x44000000) | (displayFramebufPtr_ & 0x1FFFFF));	// TODO - check
		glstate.cullFace.disable();
		glstate.depthTest.disable();
		glstate.blend.disable();
		framebufferManager.DrawPixels(pspframebuf, displayFormat_, displayStride_);
		// TODO: restore state?
	}
	currentRenderVfb_ = 0;
}

void GLES_GPU::SetDisplayFramebuffer(u32 framebuf, u32 stride, int format) {
	if (framebuf & 0x04000000) {
		//DEBUG_LOG(G3D, "Switch display framebuffer %08x", framebuf);
		prevPrevDisplayFramebufPtr_ = prevDisplayFramebufPtr_;
		prevDisplayFramebufPtr_ = displayFramebufPtr_;
		displayFramebufPtr_ = framebuf;
		displayStride_ = stride;
		displayFormat_ = format;
	} else {
		ERROR_LOG(HLE, "Bogus framebuffer address: %08x", framebuf);
	}
}

void GLES_GPU::CopyDisplayToOutput() {
	transformDraw_.Flush();
	if (!g_Config.bBufferedRendering)
		return;

	EndDebugDraw();

	VirtualFramebuffer *vfb = GetDisplayFBO();
	fbo_unbind();

	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);

	currentRenderVfb_ = 0;

	if (!vfb) {
		DEBUG_LOG(HLE, "Found no FBO! displayFBPtr = %08x", displayFramebufPtr_);
		// No framebuffer to display! Clear to black.
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // Let's not add STENCIL_BUFFER_BIT until we have a stencil buffer (GL ES)
		return;
	}

	DEBUG_LOG(HLE, "Displaying FBO %08x", vfb->fb_address);
	glstate.blend.disable();
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	glstate.scissorTest.disable();

	fbo_bind_color_as_texture(vfb->fbo, 0);

	// These are in the output display coordinates
	framebufferManager.DrawActiveTexture(480, 272, true);

	shaderManager_->DirtyShader();
	shaderManager_->DirtyUniform(DIRTY_ALL);
	gstate_c.textureChanged = true;

	BeginDebugDraw();
}

static bool MaskedEqual(u32 addr1, u32 addr2) {
	return (addr1 & 0x3FFFFFF) == (addr2 & 0x3FFFFFF);
}


void GLES_GPU::DecimateFBOs() {
	for (auto iter = vfbs_.begin(); iter != vfbs_.end();) {
		VirtualFramebuffer *v = *iter;
		if (MaskedEqual(v->fb_address, displayFramebufPtr_) ||
				MaskedEqual(v->fb_address, prevDisplayFramebufPtr_) ||
				MaskedEqual(v->fb_address, prevPrevDisplayFramebufPtr_)) {
			++iter;
			continue;
		}
		if ((*iter)->last_frame_used + FBO_OLD_AGE < gpuStats.numFrames) {
			fbo_destroy((*iter)->fbo);
			vfbs_.erase(iter++);
		}
		else
			++iter;
	}
}

GLES_GPU::VirtualFramebuffer *GLES_GPU::GetDisplayFBO() {
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		if (MaskedEqual((*iter)->fb_address, displayFramebufPtr_)) {
			// Could check w to but whatever
			return *iter;
		}
	}
	DEBUG_LOG(HLE, "Finding no FBO matching address %08x", displayFramebufPtr_);
#ifdef _DEBUG
	std::string debug = "FBOs: ";
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		char temp[256];
		sprintf(temp, "%08x %i %i", (*iter)->fb_address, (*iter)->width, (*iter)->height);
		debug += std::string(temp);
	}
	ERROR_LOG(HLE, "FBOs: %s", debug.c_str());
#endif
	return 0;
}

void GLES_GPU::SetRenderFrameBuffer() {
	if (!g_Config.bBufferedRendering)
		return;
	// Get parameters
	u32 fb_address = (gstate.fbptr & 0xFFE000) | ((gstate.fbwidth & 0xFF0000) << 8);
	int fb_stride = gstate.fbwidth & 0x3C0;

	u32 z_address = (gstate.zbptr & 0xFFE000) | ((gstate.zbwidth & 0xFF0000) << 8);
	int z_stride = gstate.zbwidth & 0x3C0;

	// Yeah this is not completely right. but it'll do for now.
	int drawing_width = ((gstate.region2) & 0x3FF) + 1;
	int drawing_height = ((gstate.region2 >> 10) & 0x3FF) + 1;

	// HACK for first frame where some games don't init things right
	if (drawing_width == 1 && drawing_height == 1) {
		drawing_width = 480;
		drawing_height = 272;
	}

	int fmt = gstate.framebufpixformat & 3;

	// Find a matching framebuffer
	VirtualFramebuffer *vfb = 0;
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		VirtualFramebuffer *v = *iter;
		if (v->fb_address == fb_address && v->width == drawing_width && v->height == drawing_height) {
			// Let's not be so picky for now. Let's say this is the one.
			vfb = v;
			// Update fb stride in case it changed
			vfb->fb_stride = fb_stride;
			break;
		}
	}

	// None found? Create one.
	if (!vfb) {
		transformDraw_.Flush();
		gstate_c.textureChanged = true;
		vfb = new VirtualFramebuffer;
		vfb->fb_address = fb_address;
		vfb->fb_stride = fb_stride;
		vfb->z_address = z_address;
		vfb->z_stride = z_stride;
		vfb->width = drawing_width;
		vfb->height = drawing_height;
		vfb->format = fmt;
		vfb->fbo = fbo_create(vfb->width * renderWidthFactor_, vfb->height * renderHeightFactor_, 1, true);
		vfb->last_frame_used = gpuStats.numFrames;
		vfbs_.push_back(vfb);
		fbo_bind_as_render_target(vfb->fbo);
		glstate.viewport.set(0, 0, renderWidth_, renderHeight_);
		currentRenderVfb_ = vfb;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		INFO_LOG(HLE, "Creating FBO for %08x : %i x %i", vfb->fb_address, vfb->width, vfb->height);
		return;
	}

	if (vfb != currentRenderVfb_) {
		transformDraw_.Flush();
		// Use it as a render target.
		DEBUG_LOG(HLE, "Switching render target to FBO for %08x", vfb->fb_address);
		gstate_c.textureChanged = true;
		fbo_bind_as_render_target(vfb->fbo);
		glstate.viewport.set(0, 0, renderWidth_, renderHeight_);
		currentRenderVfb_ = vfb;
		vfb->last_frame_used = gpuStats.numFrames;
	}
}

void GLES_GPU::BeginDebugDraw() {
	if (g_Config.bDrawWireframe) {
#ifndef USING_GLES2
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
		// glClear(GL_COLOR_BUFFER_BIT);
	}
}
void GLES_GPU::EndDebugDraw() {
#ifndef USING_GLES2
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
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
		transformDraw_.Flush();
}

void GLES_GPU::ExecuteOp(u32 op, u32 diff) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_BASE:
		//DEBUG_LOG(G3D,"DL BASE: %06x", data & 0xFFFFFF);
		break;

	case GE_CMD_VADDR:		/// <<8????
		gstate_c.vertexAddr = ((gstate.base & 0x00FF0000) << 8)|data;
		//DEBUG_LOG(G3D,"DL VADDR: %06x", gstate_c.vertexAddr);
		break;

	case GE_CMD_IADDR:
		gstate_c.indexAddr	= ((gstate.base & 0x00FF0000) << 8)|data;
		//DEBUG_LOG(G3D,"DL IADDR: %06x", gstate_c.indexAddr);
		break;

	case GE_CMD_PRIM:
		{
			SetRenderFrameBuffer();

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
			u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
			if (Memory::IsValidAddress(target)) {
				currentList->pc = target - 4; // pc will be increased after we return, counteract that
			} else {
				ERROR_LOG(G3D, "JUMP to illegal address %08x - ignoring??", target);
			}
		}
		break;

	case GE_CMD_CALL:
		{
			u32 retval = currentList->pc + 4;
			if (stackptr == ARRAY_SIZE(stack)) {
				ERROR_LOG(G3D, "CALL: Stack full!");
			} else {
				stack[stackptr++] = retval;
				u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0xFFFFFFF;
				currentList->pc = target - 4;	// pc will be increased after we return, counteract that
			}
		}
		break;

	case GE_CMD_RET:
		{
			u32 target = (currentList->pc & 0xF0000000) | (stack[--stackptr] & 0x0FFFFFFF);
			currentList->pc = target - 4;
		}
		break;

	case GE_CMD_SIGNAL:
		{
			// Processed in GE_END. Has data.
		}
		break;

	case GE_CMD_FINISH:
		// TODO: Should this run while interrupts are suspended?
		if (interruptsEnabled_)
			__TriggerInterruptWithArg(PSP_INTR_HLE, PSP_GE_INTR, currentList->subIntrBase | PSP_GE_SUBINTR_FINISH, 0);
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
					__TriggerInterruptWithArg(PSP_INTR_HLE, PSP_GE_INTR, currentList->subIntrBase | PSP_GE_SUBINTR_SIGNAL, signal);
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

	case GE_CMD_ORIGIN:
		gstate.offsetAddr = currentList->pc & 0xFFFFFF;
		break;

	case GE_CMD_VERTEXTYPE:
		if (diff & GE_VTYPE_THROUGH) {
			// Throughmode changed, let's make the proj matrix dirty.
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
		}
		// This sets through-mode or not, as well.
		break;

	case GE_CMD_OFFSETADDR:
		//			offsetAddr = data<<8;
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
		break;

	case GE_CMD_VIEWPORTZ1:
		gstate_c.zScale = getFloat24(data) / 65535.f;
		break;

	case GE_CMD_VIEWPORTZ2:
		gstate_c.zOff = getFloat24(data) / 65535.f;
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		break;

	case GE_CMD_CULL:
		break;

	case GE_CMD_LMODE:
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
		// This is done in the shader.
		break;

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
			if (num < 12)
				gstate.worldMatrix[num++] = getFloat24(data);
			gstate.worldmtxnum = (gstate.worldmtxnum & 0xFF000000) | (num & 0xF);
			shaderManager_->DirtyUniform(DIRTY_WORLDMATRIX);
		}
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		gstate.viewmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_VIEWMATRIXDATA:
		{
			int num = gstate.viewmtxnum & 0xF;
			if (num < 12)
				gstate.viewMatrix[num++] = getFloat24(data);
			gstate.viewmtxnum = (gstate.viewmtxnum & 0xFF000000) | (num & 0xF);
			shaderManager_->DirtyUniform(DIRTY_VIEWMATRIX);
		}
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		gstate.projmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_PROJMATRIXDATA:
		{
			int num = gstate.projmtxnum & 0xF;
			gstate.projMatrix[num++] = getFloat24(data);
			gstate.projmtxnum = (gstate.projmtxnum & 0xFF000000) | (num & 0xF);
		}
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		gstate.texmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_TGENMATRIXDATA:
		{
			int num = gstate.texmtxnum & 0xF;
			if (num < 12)
				gstate.tgenMatrix[num++] = getFloat24(data);
			gstate.texmtxnum = (gstate.texmtxnum & 0xFF000000) | (num & 0xF);
		}
		shaderManager_->DirtyUniform(DIRTY_TEXMATRIX);
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		gstate.boneMatrixNumber &= 0xFF00007F;
		break;

	case GE_CMD_BONEMATRIXDATA:
		{
			int num = gstate.boneMatrixNumber & 0x7F;
			shaderManager_->DirtyUniform(DIRTY_BONEMATRIX0 << (num / 12));
			if (num < 96) {
				gstate.boneMatrix[num++] = getFloat24(data);
			}
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
	gpuStats.numTextures = TextureCache_NumLoadedTextures();
	gpuStats.numFBOs = vfbs_.size();
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

	TextureCache_Invalidate(dstBasePtr + dstY * dstStride + dstX, height * dstStride + width * bpp, true);
}

void GLES_GPU::InvalidateCache(u32 addr, int size) {
	if (size > 0)
		TextureCache_Invalidate(addr, size, true);
	else
		TextureCache_InvalidateAll(true);
}

void GLES_GPU::InvalidateCacheHint(u32 addr, int size) {
	if (size > 0)
		TextureCache_Invalidate(addr, size, false);
	else
		TextureCache_InvalidateAll(false);
}

void GLES_GPU::Flush() {
	transformDraw_.Flush();
}

void GLES_GPU::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	TextureCache_Clear(true);
	gstate_c.textureChanged = true;
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		fbo_destroy((*iter)->fbo);
		delete (*iter);
	}
	vfbs_.clear();
	shaderManager_->ClearCache(true);
}
