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

#include "ShaderManager.h"
#include "DisplayListInterpreter.h"
#include "Framebuffer.h"
#include "TransformPipeline.h"
#include "TextureCache.h"

#include "../../Core/HLE/sceKernelThread.h"
#include "../../Core/HLE/sceKernelInterrupt.h"

inline void glEnDis(GLuint cmd, int value)
{
	(value ? glEnable : glDisable)(cmd);
}

ShaderManager shaderManager;

extern u32 curTextureWidth;
extern u32 curTextureHeight;

GLES_GPU::GLES_GPU(int renderWidth, int renderHeight)
	: interruptsEnabled_(true),
		renderWidth_(renderWidth),
		renderHeight_(renderHeight),
		dlIdGenerator(1),
		displayFramebufPtr_(0)
{
	renderWidthFactor_ = (float)renderWidth / 480.0f;
	renderHeightFactor_ = (float)renderHeight / 272.0f;
	shaderManager_ = &shaderManager;
	TextureCache_Init();
	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}
}

GLES_GPU::~GLES_GPU()
{
	TextureCache_Shutdown();
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter)
	{
		fbo_destroy((*iter)->fbo);
		delete (*iter);
	}
	vfbs_.clear();
}

void GLES_GPU::InitClear()
{
	if (!g_Config.bBufferedRendering)
	{
		glClearColor(0,0,0,1);
		//	glClearColor(1,0,1,1);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	}
	glViewport(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
}

void GLES_GPU::BeginFrame()
{
	TextureCache_Decimate();

	// NOTE - this is all wrong. At the beginning of the frame is a TERRIBLE time to draw the fb.
	if (g_Config.bDisplayFramebuffer && displayFramebufPtr_)
	{
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

void GLES_GPU::SetDisplayFramebuffer(u32 framebuf, u32 stride, int format)
{
	if (framebuf & 0x04000000) {
		displayFramebufPtr_ = framebuf;
		displayStride_ = stride;
		displayFormat_ = format;
	} else {
		ERROR_LOG(HLE, "Bogus framebuffer address: %08x", framebuf);
	}
}

void GLES_GPU::CopyDisplayToOutput()
{
	if (!g_Config.bBufferedRendering)
		return;

	EndDebugDraw();

	VirtualFramebuffer *vfb = GetDisplayFBO();
	fbo_unbind();

	glViewport(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);

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

	shaderManager.DirtyShader();
	shaderManager.DirtyUniform(DIRTY_ALL);
	gstate_c.textureChanged = true;

	BeginDebugDraw();
}

GLES_GPU::VirtualFramebuffer *GLES_GPU::GetDisplayFBO()
{
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter)
	{
		if (((*iter)->fb_address & 0x3FFFFFF) == (displayFramebufPtr_ & 0x3FFFFFF)) {
			// Could check w to but whatever
			return *iter;
		}
	}

	return 0;
}

void GLES_GPU::SetRenderFrameBuffer()
{
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

	int fmt = gstate.framebufpixformat & 3;

	// Find a matching framebuffer
	VirtualFramebuffer *vfb = 0;
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter)
	{
		VirtualFramebuffer *v = *iter;
		if (v->fb_address == fb_address) {
			// Let's not be so picky for now. Let's say this is the one.
			vfb = v;
			// Update fb stride in case it changed
			vfb->fb_stride = fb_stride;
			break;
		}
	}

	// None found? Create one.
	if (!vfb) {
		vfb = new VirtualFramebuffer;
		vfb->fb_address = fb_address;
		vfb->fb_stride = fb_stride;
		vfb->z_address = z_address;
		vfb->z_stride = z_stride;
		vfb->width = drawing_width;
		vfb->height = drawing_height;
		vfb->format = fmt;
		vfb->fbo = fbo_create(vfb->width * renderWidthFactor_, vfb->height * renderHeightFactor_, 1, true);
		vfbs_.push_back(vfb);
		fbo_bind_as_render_target(vfb->fbo);
		glViewport(0, 0, renderWidth_, renderHeight_);
		currentRenderVfb_ = vfb;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		DEBUG_LOG(HLE, "Creating FBO for %08x", vfb->fb_address);
		return;
	}

	if (vfb != currentRenderVfb_)
	{
		// Use it as a render target.
		DEBUG_LOG(HLE, "Switching render target to FBO for %08x", vfb->fb_address);
		fbo_bind_as_render_target(vfb->fbo);
		glViewport(0, 0, renderWidth_, renderHeight_);
		currentRenderVfb_ = vfb;
	}
}

void GLES_GPU::BeginDebugDraw()
{
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

bool GLES_GPU::ProcessDLQueue()
{
	std::vector<DisplayList>::iterator iter = dlQueue.begin();
	while (!(iter == dlQueue.end()))
	{
		DisplayList &l = *iter;
		dcontext.pc = l.listpc;
		dcontext.stallAddr = l.stall;
//		DEBUG_LOG(G3D,"Okay, starting DL execution at %08 - stall = %08x", context.pc, stallAddr);
		if (!InterpretList())
		{
			l.listpc = dcontext.pc;
			l.stall = dcontext.stallAddr;
			return false;
		}
		else
		{
			//At the end, we can remove it from the queue and continue
			dlQueue.erase(iter);
			//this invalidated the iterator, let's fix it
			iter = dlQueue.begin();
		}
	}
	return true; //no more lists!
}

u32 GLES_GPU::EnqueueList(u32 listpc, u32 stall)
{
	DisplayList dl;
	dl.id = dlIdGenerator++;
	dl.listpc = listpc & 0xFFFFFFF;
	dl.stall = stall & 0xFFFFFFF;
	dlQueue.push_back(dl);
	if (!ProcessDLQueue())
		return dl.id;
	else
		return 0;
}

void GLES_GPU::UpdateStall(int listid, u32 newstall)
{
	// this needs improvement....
	for (std::vector<DisplayList>::iterator iter = dlQueue.begin(); iter != dlQueue.end(); iter++)
	{
		DisplayList &l = *iter;
		if (l.id == listid)
		{
			l.stall = newstall & 0xFFFFFFF;
		}
	}
	
	ProcessDLQueue();
}

void GLES_GPU::DrawSync(int mode)
{
	
}

void GLES_GPU::Continue()
{

}

void GLES_GPU::Break()
{

}

// Just to get something on the screen, we'll just not subdivide correctly.
void GLES_GPU::DrawBezier(int ucount, int vcount)
{
	u16 indices[3 * 3 * 6];
	float customUV[32];
	int c = 0;
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 3; x++) {
			indices[c++] = y * 4 + x;
			indices[c++] = y * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x;
			indices[c++] = y * 4 + x;
		}
	}

	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			customUV[(y * 4 + x) * 2 + 0] = (float)x/3.0f;
			customUV[(y * 4 + x) * 2 + 1] = (float)y/3.0f;
		}
	}

	TransformAndDrawPrim(Memory::GetPointer(gstate_c.vertexAddr), &indices[0], GE_PRIM_TRIANGLES, 3 * 3 * 6, customUV, GE_VTYPE_IDX_16BIT);
}


void EnterClearMode(u32 data)
{
	bool colMask = (data >> 8) & 1;
	bool alphaMask = (data >> 9) & 1;
	bool updateZ = (data >> 10) & 1;
	glstate.colorMask.set(colMask, colMask, colMask, alphaMask);
	glstate.depthWrite.set(updateZ ? GL_TRUE : GL_FALSE);
}

void LeaveClearMode()
{
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

void GLES_GPU::ExecuteOp(u32 op, u32 diff)
{
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
	case GE_CMD_BASE:
		DEBUG_LOG(G3D,"DL BASE: %06x", data & 0xFFFFFF);
		break;

	case GE_CMD_VADDR:		/// <<8????
		gstate_c.vertexAddr = ((gstate.base & 0x00FF0000) << 8)|data;
		DEBUG_LOG(G3D,"DL VADDR: %06x", gstate_c.vertexAddr);
		break;

	case GE_CMD_IADDR:
		gstate_c.indexAddr	= ((gstate.base & 0x00FF0000) << 8)|data;
		DEBUG_LOG(G3D,"DL IADDR: %06x", gstate_c.indexAddr);
		break;

	case GE_CMD_PRIM:
		{
			SetRenderFrameBuffer();

			u32 count = data & 0xFFFF;
			u32 type = data >> 16;
			static const char* types[7] = {
				"POINTS=0,",
				"LINES=1,",
				"LINE_STRIP=2,",
				"TRIANGLES=3,",
				"TRIANGLE_STRIP=4,",
				"TRIANGLE_FAN=5,",
				"RECTANGLES=6,",
			};
			DEBUG_LOG(G3D, "DL DrawPrim type: %s count: %i vaddr= %08x, iaddr= %08x", type<7 ? types[type] : "INVALID", count, gstate_c.vertexAddr, gstate_c.indexAddr);

			if (!Memory::IsValidAddress(gstate_c.vertexAddr))
			{
				ERROR_LOG(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}
			// TODO: Split this so that we can collect sequences of primitives, can greatly speed things up
			// on platforms where draw calls are expensive like mobile and D3D
			void *verts = Memory::GetPointer(gstate_c.vertexAddr);
			void *inds = 0;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE)
			{
				if (!Memory::IsValidAddress(gstate_c.indexAddr))
				{
					ERROR_LOG(G3D, "Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				inds = Memory::GetPointer(gstate_c.indexAddr);
			}

			// Seems we have to advance the vertex addr, at least in some cases. 
			// Question: Should we also advance the index addr?
			int bytesRead;
			TransformAndDrawPrim(verts, inds, type, count, 0, -1, &bytesRead);
			gstate_c.vertexAddr += bytesRead;
		}
		break;

	// The arrow and other rotary items in Puzbob are bezier patches, strangely enough.
	case GE_CMD_BEZIER:
		{
			int bz_ucount = data & 0xFF;
			int bz_vcount = (data >> 8) & 0xFF;
			DrawBezier(bz_ucount, bz_vcount);
			DEBUG_LOG(G3D,"DL DRAW BEZIER: %i x %i", bz_ucount, bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;
			//drawSpline(sp_ucount, sp_vcount, sp_utype, sp_vtype);
			DEBUG_LOG(G3D,"DL DRAW SPLINE: %i x %i, %i x %i", sp_ucount, sp_vcount, sp_utype, sp_vtype);
		}
		break;

	case GE_CMD_JUMP: 
		{
			u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
			DEBUG_LOG(G3D,"DL CMD JUMP - %08x to %08x", dcontext.pc, target);
			dcontext.pc = target - 4; // pc will be increased after we return, counteract that
		}
		break;

	case GE_CMD_CALL: 
		{
			u32 retval = dcontext.pc + 4;
			if (stackptr == ARRAY_SIZE(stack)) {
				ERROR_LOG(G3D, "CALL: Stack full!");
			} else {
				stack[stackptr++] = retval;
				u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0xFFFFFFF;
				DEBUG_LOG(G3D,"DL CMD CALL - %08x to %08x, ret=%08x", dcontext.pc, target, retval);
				dcontext.pc = target - 4;	// pc will be increased after we return, counteract that
			}
		}
		break;

	case GE_CMD_RET: 
		//TODO : debug!
		{
			u32 target = (dcontext.pc & 0xF0000000) | (stack[--stackptr] & 0x0FFFFFFF); 
			DEBUG_LOG(G3D,"DL CMD RET - from %08x to %08x", dcontext.pc, target);
			dcontext.pc = target - 4;
		}
		break;

	case GE_CMD_SIGNAL:
		{
			DEBUG_LOG(G3D, "DL GE_CMD_SIGNAL %08x", data & 0xFFFFFF);
			// Processed in GE_END.
		}
		break;

	case GE_CMD_FINISH:
		DEBUG_LOG(G3D,"DL CMD FINISH");
		// TODO: Should this run while interrupts are suspended?
		if (interruptsEnabled_)
			__TriggerInterruptWithArg(PSP_INTR_HLE, PSP_GE_INTR, PSP_GE_SUBINTR_FINISH, 0);
		break;

	case GE_CMD_END: 
		DEBUG_LOG(G3D,"DL CMD END");
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
					ERROR_LOG(G3D, "Signal with Wait UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 2:
					DEBUG_LOG(G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
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
					__TriggerInterruptWithArg(PSP_INTR_HLE, PSP_GE_INTR, PSP_GE_SUBINTR_SIGNAL, signal);
			}
			break;
		case GE_CMD_FINISH:
			finished = true;
			break;
		default:
			DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
			break;
		}
		break;

	case GE_CMD_BJUMP:
		// bounding box jump. Let's just not jump, for now.
		DEBUG_LOG(G3D,"DL BBOX JUMP - unimplemented");
		break;

	case GE_CMD_BOUNDINGBOX:
		// bounding box test. Let's do nothing.
		DEBUG_LOG(G3D,"DL BBOX TEST - unimplemented");
		break;

	case GE_CMD_ORIGIN:
		gstate.offsetAddr = dcontext.pc & 0xFFFFFF;
		break;

	case GE_CMD_VERTEXTYPE:
		DEBUG_LOG(G3D,"DL SetVertexType: %06x", data);
		if (diff & GE_VTYPE_THROUGH) {
			// Throughmode changed, let's make the proj matrix dirty.
			shaderManager.DirtyUniform(DIRTY_PROJMATRIX);
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
			DEBUG_LOG(G3D,"DL Region TL: %d %d", x1, y1);
		}
		break;

	case GE_CMD_REGION2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			DEBUG_LOG(G3D,"DL Region BR: %d %d", x2, y2);
		}
		break;

	case GE_CMD_CLIPENABLE:
		DEBUG_LOG(G3D, "DL Clip Enable: %i   (ignoring)", data);
		//we always clip, this is opengl
		break;

	case GE_CMD_CULLFACEENABLE: 
		DEBUG_LOG(G3D, "DL CullFace Enable: %i   (ignoring)", data);
		break;

	case GE_CMD_TEXTUREMAPENABLE: 
		gstate_c.textureChanged = true;
		DEBUG_LOG(G3D, "DL Texture map enable: %i", data);
		break;

	case GE_CMD_LIGHTINGENABLE:
		DEBUG_LOG(G3D, "DL Lighting enable: %i", data);
		data += 1;
		//We don't use OpenGL lighting
		break;

	case GE_CMD_FOGENABLE:		
		DEBUG_LOG(G3D, "DL Fog Enable: %i", data);
		break;

	case GE_CMD_DITHERENABLE:
		DEBUG_LOG(G3D, "DL Dither Enable: %i", data);
		break;

	case GE_CMD_OFFSETX:		
		DEBUG_LOG(G3D, "DL Offset X: %i", data);
		break;

	case GE_CMD_OFFSETY:		
		DEBUG_LOG(G3D, "DL Offset Y: %i", data);
		break;

	case GE_CMD_TEXSCALEU:
		gstate_c.uScale = getFloat24(data);
		DEBUG_LOG(G3D, "DL Texture U Scale: %f", gstate_c.uScale);
		shaderManager.DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_TEXSCALEV:
		gstate_c.vScale = getFloat24(data);
		DEBUG_LOG(G3D, "DL Texture V Scale: %f", gstate_c.vScale);
		shaderManager.DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_TEXOFFSETU:
		gstate_c.uOff = getFloat24(data);
		DEBUG_LOG(G3D, "DL Texture U Offset: %f", gstate_c.uOff);
		shaderManager.DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_TEXOFFSETV:
		gstate_c.vOff = getFloat24(data);
		DEBUG_LOG(G3D, "DL Texture V Offset: %f", gstate_c.vOff);
		shaderManager.DirtyUniform(DIRTY_UVSCALEOFFSET);
		break;

	case GE_CMD_SCISSOR1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			DEBUG_LOG(G3D, "DL Scissor TL: %i, %i", x1,y1);
		}
		break;
	case GE_CMD_SCISSOR2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			DEBUG_LOG(G3D, "DL Scissor BR: %i, %i", x2, y2);
		}
		break;

	case GE_CMD_MINZ:
		gstate_c.zMin = getFloat24(data) / 65535.f;
		DEBUG_LOG(G3D, "DL MinZ: %f", gstate_c.zMin);
		break;

	case GE_CMD_MAXZ:
		gstate_c.zMax = getFloat24(data) / 65535.f;
		DEBUG_LOG(G3D, "DL MaxZ: %f", gstate_c.zMax);
		break;

	case GE_CMD_FRAMEBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			DEBUG_LOG(G3D, "DL FramebufPtr: %08x", ptr);
		}
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		{
			u32 w = data & 0xFFFFFF;
			DEBUG_LOG(G3D, "DL FramebufWidth: %i", w);
		}
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		break;

	case GE_CMD_TEXADDR0:
		gstate_c.textureChanged = true;
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		DEBUG_LOG(G3D,"DL Texture address %i: %06x", cmd-GE_CMD_TEXADDR0, data);
		break;

	case GE_CMD_TEXBUFWIDTH0:
		gstate_c.textureChanged = true;
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		DEBUG_LOG(G3D,"DL Texture BUFWIDTHess %i: %06x", cmd-GE_CMD_TEXBUFWIDTH0, data);
		break;

	case GE_CMD_CLUTADDR:
		//DEBUG_LOG(G3D,"CLUT base addr: %06x", data);
		break;

	case GE_CMD_CLUTADDRUPPER:
		DEBUG_LOG(G3D,"DL CLUT addr: %08x", ((gstate.clutaddrupper & 0xFF0000)<<8) | (gstate.clutaddr & 0xFFFFFF));
		break;

	case GE_CMD_LOADCLUT:
		// This could be used to "dirty" textures with clut.
		{
			u32 clutAddr = ((gstate.clutaddrupper & 0xFF0000)<<8) | (gstate.clutaddr & 0xFFFFFF);
			if (clutAddr)
			{
				DEBUG_LOG(G3D,"DL Clut load: %08x", clutAddr);
			}
			else
			{
				DEBUG_LOG(G3D,"DL Empty Clut load");
			}
			// Should hash and invalidate all paletted textures on use
		}
		break;

	case GE_CMD_TEXMAPMODE:
		DEBUG_LOG(G3D,"Tex map mode: %06x", data);
		break;

	case GE_CMD_TEXSHADELS:
		DEBUG_LOG(G3D,"Tex shade light sources: %06x", data);
		break;

	case GE_CMD_CLUTFORMAT:
		{
			DEBUG_LOG(G3D,"DL Clut format: %06x", data);
		}
		break;

	case GE_CMD_TRANSFERSRC:
		{
			// Nothing to do, the next one prints
		}
		break;

	case GE_CMD_TRANSFERSRCW:
		{
			u32 xferSrc = gstate.transfersrc | ((data&0xFF0000)<<8);
			u32 xferSrcW = gstate.transfersrcw & 1023;
			DEBUG_LOG(G3D,"Block Transfer Src: %08x	W: %i", xferSrc, xferSrcW);
			break;
		}

	case GE_CMD_TRANSFERDST:
		{
			// Nothing to do, the next one prints
		}
		break;

	case GE_CMD_TRANSFERDSTW:
		{
			u32 xferDst= gstate.transferdst | ((data&0xFF0000)<<8);
			u32 xferDstW = gstate.transferdstw & 1023;
			DEBUG_LOG(G3D,"Block Transfer Dest: %08x	W: %i", xferDst, xferDstW);
			break;
		}
		
	case GE_CMD_TRANSFERSRCPOS:
		{
			u32 x = (data & 1023)+1;
			u32 y = ((data>>10) & 1023)+1;
			DEBUG_LOG(G3D, "DL Block Transfer Src Rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERDSTPOS:
		{
			u32 x = (data & 1023)+1;
			u32 y = ((data>>10) & 1023)+1;
			DEBUG_LOG(G3D, "DL Block Transfer Dest Rect TL: %i, %i", x, y);
			break;
		}

	case GE_CMD_TRANSFERSIZE:
		{
			u32 w = (data & 1023)+1;
			u32 h = ((data>>10) & 1023)+1;
			DEBUG_LOG(G3D, "DL Block Transfer Rect Size: %i x %i", w, h);
			break;
		}

	case GE_CMD_TRANSFERSTART:  // Orphis calls this TRXKICK
		{
			// TODO: Here we should check if the transfer overlaps a framebuffer or any textures,
			// and take appropriate action. This is a block transfer between RAM and VRAM, or vice versa.
			DoBlockTransfer();
			break;
		}

	case GE_CMD_TEXSIZE0:
		gstate_c.textureChanged = true;
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
		DEBUG_LOG(G3D,"DL Texture Size %i: %06x", cmd - GE_CMD_TEXSIZE0, data);
		break;

	case GE_CMD_ZBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			DEBUG_LOG(G3D,"Zbuf Ptr: %06x", ptr);
		}
		break;

	case GE_CMD_ZBUFWIDTH:
		{
			u32 w = data & 0xFFFFFF;
			DEBUG_LOG(G3D,"Zbuf Width: %i", w);
		}
		break;

	case GE_CMD_AMBIENTCOLOR:
		DEBUG_LOG(G3D,"DL Ambient Color: %06x",	data);
		break;

	case GE_CMD_AMBIENTALPHA:
		DEBUG_LOG(G3D,"DL Ambient Alpha: %06x",	data);
		break;

	case GE_CMD_MATERIALAMBIENT:
		DEBUG_LOG(G3D,"DL Material Ambient Color: %06x",	data);
		if (diff)
			shaderManager.DirtyUniform(DIRTY_MATAMBIENTALPHA);
		break;

	case GE_CMD_MATERIALDIFFUSE:
		DEBUG_LOG(G3D,"DL Material Diffuse Color: %06x",	data);
		if (diff)
			shaderManager.DirtyUniform(DIRTY_MATDIFFUSE);
		break;

	case GE_CMD_MATERIALEMISSIVE:
		DEBUG_LOG(G3D,"DL Material Emissive Color: %06x",	data);
		if (diff)
			shaderManager.DirtyUniform(DIRTY_MATEMISSIVE);
		break;

	case GE_CMD_MATERIALSPECULAR:
		DEBUG_LOG(G3D,"DL Material Specular Color: %06x",	data);
		if (diff)
			shaderManager.DirtyUniform(DIRTY_MATSPECULAR);
		break;

	case GE_CMD_MATERIALALPHA:
		DEBUG_LOG(G3D,"DL Material Alpha Color: %06x",	data);
		if (diff)
			shaderManager.DirtyUniform(DIRTY_MATAMBIENTALPHA);
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		DEBUG_LOG(G3D,"DL Material specular coef: %f", getFloat24(data));
		if (diff)
			shaderManager.DirtyUniform(DIRTY_MATSPECULAR);
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		DEBUG_LOG(G3D,"DL Light %i type: %06x", cmd-GE_CMD_LIGHTTYPE0, data);
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
			DEBUG_LOG(G3D,"DL Light %i %c pos: %f", l, c+'X', val);
			gstate_c.lightpos[l][c] = val;
			if (diff)
				shaderManager.DirtyUniform(DIRTY_LIGHT0 << l);
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
			DEBUG_LOG(G3D,"DL Light %i %c dir: %f", l, c+'X', val);
			gstate_c.lightdir[l][c] = val;
			if (diff)
				shaderManager.DirtyUniform(DIRTY_LIGHT0 << l);
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
			DEBUG_LOG(G3D,"DL Light %i %c att: %f", l, c+'X', val);
			gstate_c.lightatt[l][c] = val;
			if (diff)
				shaderManager.DirtyUniform(DIRTY_LIGHT0 << l);
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
				shaderManager.DirtyUniform(DIRTY_LIGHT0 << l);
		}
		break;

	case GE_CMD_VIEWPORTX1:
	case GE_CMD_VIEWPORTY1:
	case GE_CMD_VIEWPORTX2:
	case GE_CMD_VIEWPORTY2:
		DEBUG_LOG(G3D,"DL Viewport param %i: %f", cmd-GE_CMD_VIEWPORTX1, getFloat24(data));
		break;
	case GE_CMD_VIEWPORTZ1:
		gstate_c.zScale = getFloat24(data) / 65535.f;
		DEBUG_LOG(G3D,"DL Z scale: %f", gstate_c.zScale);
		break;
	case GE_CMD_VIEWPORTZ2:
		gstate_c.zOff = getFloat24(data) / 65535.f;
		DEBUG_LOG(G3D,"DL Z pos: %f", gstate_c.zOff);
		break;
	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		DEBUG_LOG(G3D,"DL Light %i enable: %d", cmd-GE_CMD_LIGHTENABLE0, data);
		break;
	case GE_CMD_CULL:
		DEBUG_LOG(G3D,"DL cull: %06x", data);
		break;

	case GE_CMD_LMODE:
		DEBUG_LOG(G3D,"DL Shade mode: %06x", data);
		break;

	case GE_CMD_PATCHDIVISION:
		gstate_c.patch_div_s = data & 0xFF;
		gstate_c.patch_div_t = (data >> 8) & 0xFF;
		DEBUG_LOG(G3D, "DL Patch subdivision: %i x %i", gstate_c.patch_div_s, gstate_c.patch_div_t);
		break;

	case GE_CMD_MATERIALUPDATE:
		DEBUG_LOG(G3D,"DL Material Update: %d", data);
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
		DEBUG_LOG(G3D,"DL Clear mode: %06x", data);
		break;


	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
		DEBUG_LOG(G3D,"DL Alpha blend enable: %d", data);
		break;

	case GE_CMD_BLENDMODE:
		DEBUG_LOG(G3D,"DL Blend mode: %06x", data);
		break;

	case GE_CMD_BLENDFIXEDA:
		DEBUG_LOG(G3D,"DL Blend fix A: %06x", data);
		break;

	case GE_CMD_BLENDFIXEDB:
		DEBUG_LOG(G3D,"DL Blend fix B: %06x", data);
		break;

	case GE_CMD_ALPHATESTENABLE:
		DEBUG_LOG(G3D,"DL Alpha test enable: %d", data);
		// This is done in the shader.
		break;

	case GE_CMD_ALPHATEST:
		DEBUG_LOG(G3D,"DL Alpha test settings");
		shaderManager.DirtyUniform(DIRTY_ALPHACOLORREF);
		break;

	case GE_CMD_TEXFUNC:
		{
			DEBUG_LOG(G3D,"DL TexFunc %i", data&7);
			/*
			int m=GL_MODULATE;
			switch (data & 7)
			{
			case 0: m=GL_MODULATE; break;
			case 1: m=GL_DECAL; break;
			case 2: m=GL_BLEND; break;
			case 3: m=GL_REPLACE; break;
			case 4: m=GL_ADD; break;
			}*/

			/*
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB,			GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB,			GL_CONSTANT);
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB,		 GL_SRC_COLOR);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB,			GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB,		 GL_SRC_COLOR);
			glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);

			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, m);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);*/
			break;
		}
	case GE_CMD_TEXFILTER:
		{
			int min = data & 7;
			int mag = (data >> 8) & 1;
			DEBUG_LOG(G3D,"DL TexFilter min: %i mag: %i", min, mag);
		}
		break;
	case GE_CMD_TEXENVCOLOR:
		DEBUG_LOG(G3D,"DL TexEnvColor %06x", data);
		if (diff)
			shaderManager.DirtyUniform(DIRTY_TEXENV);
		break;
	case GE_CMD_TEXMODE:
		DEBUG_LOG(G3D,"DL TexMode %08x", data);
		break;
	case GE_CMD_TEXFORMAT:
		DEBUG_LOG(G3D,"DL TexFormat %08x", data);
		break;
	case GE_CMD_TEXFLUSH:
		DEBUG_LOG(G3D,"DL TexFlush");
		break;
	case GE_CMD_TEXWRAP:
		DEBUG_LOG(G3D,"DL TexWrap %08x", data);
		break;
	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
		DEBUG_LOG(G3D,"DL Z test enable: %d", data & 1);
		break;

	case GE_CMD_STENCILTESTENABLE:
		DEBUG_LOG(G3D,"DL Stencil test enable: %d", data);
		break;

	case GE_CMD_ZTEST:
		{
			DEBUG_LOG(G3D,"DL Z test mode: %i", data);
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
			DEBUG_LOG(G3D,"DL MorphWeight %i = %f", index, weight);
			gstate_c.morphWeights[index] = weight;
		}
		break;

	case GE_CMD_DITH0:
	case GE_CMD_DITH1:
	case GE_CMD_DITH2:
	case GE_CMD_DITH3:
		DEBUG_LOG(G3D,"DL DitherMatrix %i = %06x",cmd-GE_CMD_DITH0,data);
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL World # %i", data & 0xF);
		gstate.worldmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_WORLDMATRIXDATA:
		DEBUG_LOG(G3D,"DL World data # %f", getFloat24(data));
		{
			int num = gstate.worldmtxnum & 0xF;
			if (num < 12)
				gstate.worldMatrix[num++] = getFloat24(data);
			gstate.worldmtxnum = (gstate.worldmtxnum & 0xFF000000) | (num & 0xF);
			shaderManager.DirtyUniform(DIRTY_WORLDMATRIX);
		}
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL VIEW # %i", data & 0xF);
		gstate.viewmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_VIEWMATRIXDATA:
		DEBUG_LOG(G3D,"DL VIEW data # %f", getFloat24(data));
		{
			int num = gstate.viewmtxnum & 0xF;
			if (num < 12)
				gstate.viewMatrix[num++] = getFloat24(data);
			gstate.viewmtxnum = (gstate.viewmtxnum & 0xFF000000) | (num & 0xF);
			shaderManager.DirtyUniform(DIRTY_VIEWMATRIX);
		}
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL PROJECTION # %i", data & 0xF);
		gstate.projmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_PROJMATRIXDATA:
		DEBUG_LOG(G3D,"DL PROJECTION matrix data # %f", getFloat24(data));
		{
			int num = gstate.projmtxnum & 0xF;
			gstate.projMatrix[num++] = getFloat24(data);
			gstate.projmtxnum = (gstate.projmtxnum & 0xFF000000) | (num & 0xF);
		}
		shaderManager.DirtyUniform(DIRTY_PROJMATRIX);
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL TGEN # %i", data & 0xF);
		gstate.texmtxnum &= 0xFF00000F;
		break;

	case GE_CMD_TGENMATRIXDATA:
		DEBUG_LOG(G3D,"DL TGEN data # %f", getFloat24(data));
		{
			int num = gstate.texmtxnum & 0xF;
			if (num < 12)
				gstate.tgenMatrix[num++] = getFloat24(data);
			gstate.texmtxnum = (gstate.texmtxnum & 0xFF000000) | (num & 0xF);
		}
		shaderManager.DirtyUniform(DIRTY_TEXMATRIX);
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL BONE #%i", data);
		gstate.boneMatrixNumber &= 0xFF00007F;
		break;

	case GE_CMD_BONEMATRIXDATA:
		DEBUG_LOG(G3D,"DL BONE data #%i %f", gstate.boneMatrixNumber & 0x7f, getFloat24(data));
		{
			int num = gstate.boneMatrixNumber & 0x7F;
			shaderManager.DirtyUniform(DIRTY_BONEMATRIX0 << (num / 12));
			if (num < 96) {
				gstate.boneMatrix[num++] = getFloat24(data);
			}
			gstate.boneMatrixNumber = (gstate.boneMatrixNumber & 0xFF000000) | (num & 0x7F);
		}
		break;

	default:
		DEBUG_LOG(G3D,"DL Unknown: %08x @ %08x", op, dcontext.pc);
		break;

		//ETC...
	}
}

bool GLES_GPU::InterpretList()
{
	// Reset stackptr for safety
	stackptr = 0;
	u32 op = 0;
	prev = 0;
	finished = false;
	while (!finished)
	{
		if (!Memory::IsValidAddress(dcontext.pc)) {
			ERROR_LOG(G3D, "DL PC = %08x WTF!!!!", dcontext.pc);
			return true;
		}
		if (dcontext.pc == dcontext.stallAddr)
			return false;

		op = Memory::ReadUnchecked_U32(dcontext.pc); //read from memory
		u32 cmd = op >> 24;
		u32 diff = op ^ gstate.cmdmem[cmd];
		gstate.cmdmem[cmd] = op;

		ExecuteOp(op, diff);

		dcontext.pc += 4;
		prev = op;
	}
	return true;
}

void GLES_GPU::UpdateStats()
{
	gpuStats.numVertexShaders = shaderManager.NumVertexShaders();
	gpuStats.numFragmentShaders = shaderManager.NumFragmentShaders();
	gpuStats.numShaders = shaderManager.NumPrograms();
	gpuStats.numTextures = TextureCache_NumLoadedTextures();
}


void GLES_GPU::DoBlockTransfer()
{
	// TODO: This is used a lot to copy data around between render targets and textures,
	// and also to quickly load textures from RAM to VRAM. So we should do checks like the following:
	//  * Does dstBasePtr point to an existing texture? If so invalidate it and reload it immediately.
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

	NOTICE_LOG(HLE, "Block transfer: %08x to %08x, %i x %i , ...", srcBasePtr, dstBasePtr, width, height);

	// Do the copy!
	for (int y = 0; y < height; y++) {
		const u8 *src = Memory::GetPointer(srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp);
		u8 *dst = Memory::GetPointer(dstBasePtr + ((y + dstY) * srcStride + dstX) * bpp);
		memcpy(dst, src, width * bpp);
	}

	// TODO: Notify all overlapping textures that it's time to die/reload.
}
