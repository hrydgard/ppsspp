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


#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gl_state.h"

#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/Colors.h"
#include "GPU/Software/Rasterizer.h"

static GLuint temp_texture = 0;

static GLint attr_pos = -1, attr_tex = -1;
static GLint uni_tex = -1;

static GLuint program;

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;
FormatBuffer fb;
FormatBuffer depthbuf;
u32 clut[4096];

// TODO: This one lives in GPU/GLES/Framebuffer.cpp, move it to somewhere common.
void CenterRect(float *x, float *y, float *w, float *h,
								float origW, float origH, float frameW, float frameH);

GLuint OpenGL_CompileProgram(const char* vertexShader, const char* fragmentShader)
{
	// generate objects
	GLuint vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	GLuint programID = glCreateProgram();

	// compile vertex shader
	glShaderSource(vertexShaderID, 1, &vertexShader, NULL);
	glCompileShader(vertexShaderID);

#if defined(_DEBUG) || defined(DEBUGFAST) || defined(DEBUG_GLSL)
	GLint Result = GL_FALSE;
	char stringBuffer[1024];
	GLsizei stringBufferUsage = 0;
	glGetShaderiv(vertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderInfoLog(vertexShaderID, 1024, &stringBufferUsage, stringBuffer);
	if (Result && stringBufferUsage) {
		// not nice
	} else if (!Result) {
		// not nice
	} else {
		// not nice
	}
	bool shader_errors = !Result;
#endif

	// compile fragment shader
	glShaderSource(fragmentShaderID, 1, &fragmentShader, NULL);
	glCompileShader(fragmentShaderID);

#if defined(_DEBUG) || defined(DEBUGFAST) || defined(DEBUG_GLSL)
	glGetShaderiv(fragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderInfoLog(fragmentShaderID, 1024, &stringBufferUsage, stringBuffer);
	if (Result && stringBufferUsage) {
		// not nice
	} else if (!Result) {
		// not nice
	} else {
		// not nice
	}
	shader_errors |= !Result;
#endif

	// link them
	glAttachShader(programID, vertexShaderID);
	glAttachShader(programID, fragmentShaderID);
	glLinkProgram(programID);

#if defined(_DEBUG) || defined(DEBUGFAST) || defined(DEBUG_GLSL)
	glGetProgramiv(programID, GL_LINK_STATUS, &Result);
	glGetProgramInfoLog(programID, 1024, &stringBufferUsage, stringBuffer);
	if (Result && stringBufferUsage) {
		// not nice
	} else if (!Result && !shader_errors) {
		// not nice
	}
#endif

	// cleanup
	glDeleteShader(vertexShaderID);
	glDeleteShader(fragmentShaderID);

	return programID;
}

SoftGPU::SoftGPU()
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // 4-byte pixel alignment
	glGenTextures(1, &temp_texture);

	// TODO: Use highp for GLES
	static const char *fragShaderText =
#ifdef USING_GLES2
		"#version 100\n"
#endif
		"varying vec2 TexCoordOut;\n"
		"uniform sampler2D Texture;\n"
		"void main() {\n"
		"   vec4 tmpcolor;\n"
		"   tmpcolor = texture2D(Texture, TexCoordOut);\n"
		"   gl_FragColor = tmpcolor;\n"
		"}\n";
	static const char *vertShaderText =
#ifdef USING_GLES2
		"#version 100\n"
#endif
		"attribute vec4 pos;\n"
		"attribute vec2 TexCoordIn;\n "
		"varying vec2 TexCoordOut;\n "
		"void main() {\n"
		"   gl_Position = pos;\n"
		"   TexCoordOut = TexCoordIn;\n"
		"}\n";

	program = OpenGL_CompileProgram(vertShaderText, fragShaderText);

	glUseProgram(program);

	uni_tex = glGetUniformLocation(program, "Texture");
	attr_pos = glGetAttribLocation(program, "pos");
	attr_tex = glGetAttribLocation(program, "TexCoordIn");

	fb.data = Memory::GetPointer(0x44000000); // TODO: correct default address?
	depthbuf.data = Memory::GetPointer(0x44000000); // TODO: correct default address?

	framebufferDirty_ = true;
	// TODO: Is there a default?
	displayFramebuf_ = 0;
	displayStride_ = 512;
	displayFormat_ = GE_FORMAT_8888;
}

SoftGPU::~SoftGPU()
{
	glDeleteProgram(program);
	glDeleteTextures(1, &temp_texture);
}

void SoftGPU::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	// Seems like this can point into RAM, but should be VRAM if not in RAM.
	displayFramebuf_ = (framebuf & 0xFF000000) == 0 ? 0x44000000 | framebuf : framebuf;
	displayStride_ = stride;
	displayFormat_ = format;
	host->GPUNotifyDisplay(framebuf, stride, format);
}

// Copies RGBA8 data from RAM to the currently bound render target.
void SoftGPU::CopyToCurrentFboFromDisplayRam(int srcwidth, int srcheight)
{
	float dstwidth = (float)PSP_CoreParameter().pixelWidth;
	float dstheight = (float)PSP_CoreParameter().pixelHeight;

	glstate.blend.disable();
	glstate.viewport.set(0, 0, dstwidth, dstheight);
	glstate.scissorTest.disable();

	glBindTexture(GL_TEXTURE_2D, temp_texture);

	GLfloat texvert_u;
	if (displayFramebuf_ == 0) {
		u32 data[] = {0};
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		texvert_u = 1.0f;
	} else if (displayFormat_ == GE_FORMAT_8888) {
		u8 *data = Memory::GetPointer(displayFramebuf_);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)displayStride_, (GLsizei)srcheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		texvert_u = (float)srcwidth / displayStride_;
	} else {
		// TODO: This should probably be converted in a shader instead..
		// TODO: Do something less brain damaged to manage this buffer...
		u32 *buf = new u32[srcwidth * srcheight];
		FormatBuffer displayBuffer;
		displayBuffer.data = Memory::GetPointer(displayFramebuf_);
		for (int y = 0; y < srcheight; ++y) {
			u32 *buf_line = &buf[y * srcwidth];
			const u16 *fb_line = &displayBuffer.as16[y * displayStride_];

			switch (displayFormat_) {
			case GE_FORMAT_565:
				for (int x = 0; x < srcwidth; ++x) {
					buf_line[x] = DecodeRGB565(fb_line[x]);
				}
				break;

			case GE_FORMAT_5551:
				for (int x = 0; x < srcwidth; ++x) {
					buf_line[x] = DecodeRGBA5551(fb_line[x]);
				}
				break;

			case GE_FORMAT_4444:
				for (int x = 0; x < srcwidth; ++x) {
					buf_line[x] = DecodeRGBA4444(fb_line[x]);
				}
				break;

			default:
				ERROR_LOG_REPORT(G3D, "Software: Unexpected framebuffer format: %d", displayFormat_);
			}
		}

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)srcwidth, (GLsizei)srcheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		texvert_u = 1.0f;

		delete[] buf;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glUseProgram(program);

	float x, y, w, h;
	CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, dstwidth, dstheight);

	x /= 0.5f * dstwidth;
	y /= 0.5f * dstheight;
	w /= 0.5f * dstwidth;
	h /= 0.5f * dstheight;
	float x2 = x + w;
	float y2 = y + h;
	x -= 1.0f;
	y -= 1.0f;
	x2 -= 1.0f;
	y2 -= 1.0f;

	const GLfloat verts[4][2] = {
		{ x, y }, // Left top
		{ x, y2}, // left bottom
		{ x2, y2}, // right bottom
		{ x2, y}  // right top
	};

	const GLfloat texverts[4][2] = {
		{0, 1},
		{0, 0},
		{texvert_u, 0},
		{texvert_u, 1}
	};

	glVertexAttribPointer(attr_pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(attr_tex, 2, GL_FLOAT, GL_FALSE, 0, texverts);
	glEnableVertexAttribArray(attr_pos);
	glEnableVertexAttribArray(attr_tex);
	glUniform1i(uni_tex, 0);
	glActiveTexture(GL_TEXTURE0);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableVertexAttribArray(attr_pos);
	glDisableVertexAttribArray(attr_tex);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void SoftGPU::CopyDisplayToOutput()
{
	ScheduleEvent(GPU_EVENT_COPY_DISPLAY_TO_OUTPUT);
}

void SoftGPU::CopyDisplayToOutputInternal()
{
	// The display always shows 480x272.
	CopyToCurrentFboFromDisplayRam(FB_WIDTH, FB_HEIGHT);
	framebufferDirty_ = false;
}

void SoftGPU::ProcessEvent(GPUEvent ev) {
	switch (ev.type) {
	case GPU_EVENT_COPY_DISPLAY_TO_OUTPUT:
		CopyDisplayToOutputInternal();
		break;

	default:
		GPUCommon::ProcessEvent(ev);
	}
}

void SoftGPU::FastRunLoop(DisplayList &list) {
	for (; downcount > 0; --downcount) {
		u32 op = Memory::ReadUnchecked_U32(list.pc);
		u32 cmd = op >> 24;

		u32 diff = op ^ gstate.cmdmem[cmd];
		gstate.cmdmem[cmd] = op;
		ExecuteOp(op, diff);

		list.pc += 4;
	}
}

int EstimatePerVertexCost() {
	// TODO: This is transform cost, also account for rasterization cost somehow... although it probably
	// runs in parallel with transform.

	// Also, this is all pure guesswork. If we can find a way to do measurements, that would be great.

	// GTA wants a low value to run smooth, GoW wants a high value (otherwise it thinks things
	// went too fast and starts doing all the work over again).

	int cost = 20;
	if (gstate.isLightingEnabled()) {
		cost += 10;
	}

	for (int i = 0; i < 4; i++) {
		if (gstate.isLightChanEnabled(i))
			cost += 10;
	}
	if (gstate.getUVGenMode() != GE_TEXMAP_TEXTURE_COORDS) {
		cost += 20;
	}
	// TODO: morphcount

	return cost;
}

void SoftGPU::ExecuteOp(u32 op, u32 diff)
{
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
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

			/*
			if (type == GE_PRIM_POINTS || type == GE_PRIM_LINES || type == GE_PRIM_LINE_STRIP) {
				ERROR_LOG_REPORT(G3D, "Software: DL DrawPrim type: %s count: %i vaddr= %08x, iaddr= %08x", type<7 ? types[type] : "INVALID", count, gstate_c.vertexAddr, gstate_c.indexAddr);
				cyclesExecuted += EstimatePerVertexCost() * count;
				break;
			}
			*/

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG_REPORT(G3D, "Software: Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			void *verts = Memory::GetPointer(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG_REPORT(G3D, "Software: Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				indices = Memory::GetPointer(gstate_c.indexAddr);
			}

			cyclesExecuted += EstimatePerVertexCost() * count;
			int bytesRead;
			TransformUnit::SubmitPrimitive(verts, indices, type, count, gstate.vertType, &bytesRead);
			framebufferDirty_ = true;

			// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
			// Some games rely on this, they don't bother reloading VADDR and IADDR.
			// The VADDR/IADDR registers are NOT updated.
			if (indices) {
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
			DEBUG_LOG(G3D,"DL DRAW BEZIER: %i x %i", bz_ucount, bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG_REPORT(G3D, "Software: Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			void *control_points = Memory::GetPointer(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG_REPORT(G3D, "Software: Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				indices = Memory::GetPointer(gstate_c.indexAddr);
			}

			if (gstate.getPatchPrimitiveType() != GE_PATCHPRIM_TRIANGLES) {
				ERROR_LOG_REPORT(G3D, "Software: Unsupported patch primitive %x", gstate.patchprimitive&3);
				break;
			}

			if (!(gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME)) {
				TransformUnit::SubmitSpline(control_points, indices, sp_ucount, sp_vcount, sp_utype, sp_vtype, gstate.getPatchPrimitiveType(), gstate.vertType);
			}
			framebufferDirty_ = true;
			DEBUG_LOG(G3D,"DL DRAW SPLINE: %i x %i, %i x %i", sp_ucount, sp_vcount, sp_utype, sp_vtype);
		}
		break;

	case GE_CMD_BOUNDINGBOX:
		if (data != 0)
			DEBUG_LOG(G3D, "Unsupported bounding box: %06x", data);
		// bounding box test. Let's assume the box was within the drawing region.
		currentList->bboxResult = true;
		break;

	case GE_CMD_VERTEXTYPE:
		break;

	case GE_CMD_REGION1:
	case GE_CMD_REGION2:
		break;

	case GE_CMD_CLIPENABLE:
		break;

	case GE_CMD_CULLFACEENABLE:
	case GE_CMD_CULL:
		break;

	case GE_CMD_TEXTUREMAPENABLE: 
		break;

	case GE_CMD_LIGHTINGENABLE:
		break;

	case GE_CMD_FOGCOLOR:
	case GE_CMD_FOG1:
	case GE_CMD_FOG2:
	case GE_CMD_FOGENABLE:
		break;

	case GE_CMD_DITHERENABLE:
		break;

	case GE_CMD_OFFSETX:
		break;

	case GE_CMD_OFFSETY:
		break;

	case GE_CMD_TEXSCALEU:
		gstate_c.uv.uScale = getFloat24(data);
		break;

	case GE_CMD_TEXSCALEV:
		gstate_c.uv.vScale = getFloat24(data);
		break;

	case GE_CMD_TEXOFFSETU:
		gstate_c.uv.uOff = getFloat24(data);
		break;

	case GE_CMD_TEXOFFSETV:
		gstate_c.uv.vOff = getFloat24(data);
		break;

	case GE_CMD_SCISSOR1:
	case GE_CMD_SCISSOR2:
		break;

	case GE_CMD_MINZ:
		break;

	case GE_CMD_FRAMEBUFPTR:
		fb.data = Memory::GetPointer(gstate.getFrameBufAddress());
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		fb.data = Memory::GetPointer(gstate.getFrameBufAddress());
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
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		break;

	case GE_CMD_CLUTADDR:
	case GE_CMD_CLUTADDRUPPER:
		break;

	case GE_CMD_LOADCLUT:
		{
			u32 clutAddr = gstate.getClutAddress();
			u32 clutTotalBytes = gstate.getClutLoadBytes();

			if (Memory::IsValidAddress(clutAddr)) {
				Memory::MemcpyUnchecked(clut, clutAddr, clutTotalBytes);
			// TODO: Do something to the CLUT with 0?
			} else if (clutAddr != 0) {
				// TODO: Does this make any sense?
				ERROR_LOG_REPORT_ONCE(badClut, G3D, "Software: Invalid CLUT address, filling with garbage instead of crashing");
				memset(clut, 0xFF, clutTotalBytes);
			}
		}
		break;

	// Don't need to do anything, just state for transferstart.
	case GE_CMD_TRANSFERSRC:
	case GE_CMD_TRANSFERSRCW:
	case GE_CMD_TRANSFERDST:
	case GE_CMD_TRANSFERDSTW:
	case GE_CMD_TRANSFERSRCPOS:
	case GE_CMD_TRANSFERDSTPOS:
	case GE_CMD_TRANSFERSIZE:
		break;

	case GE_CMD_TRANSFERSTART:
		{
			u32 srcBasePtr = gstate.getTransferSrcAddress();
			u32 srcStride = gstate.getTransferSrcStride();

			u32 dstBasePtr = gstate.getTransferDstAddress();
			u32 dstStride = gstate.getTransferDstStride();

			int srcX = gstate.getTransferSrcX();
			int srcY = gstate.getTransferSrcY();

			int dstX = gstate.getTransferDstX();
			int dstY = gstate.getTransferDstY();

			int width = gstate.getTransferWidth();
			int height = gstate.getTransferHeight();

			int bpp = gstate.getTransferBpp();

			DEBUG_LOG(G3D, "Block transfer: %08x/%x -> %08x/%x, %ix%ix%i (%i,%i)->(%i,%i)", srcBasePtr, srcStride, dstBasePtr, dstStride, width, height, bpp, srcX, srcY, dstX, dstY);

			for (int y = 0; y < height; y++) {
				const u8 *src = Memory::GetPointer(srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp);
				u8 *dst = Memory::GetPointer(dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp);
				memcpy(dst, src, width * bpp);
			}

#ifndef MOBILE_DEVICE
			CBreakPoints::ExecMemCheck(srcBasePtr + (srcY * srcStride + srcX) * bpp, false, height * srcStride * bpp, currentMIPS->pc);
			CBreakPoints::ExecMemCheck(dstBasePtr + (srcY * dstStride + srcX) * bpp, true, height * dstStride * bpp, currentMIPS->pc);
#endif

			// Could theoretically dirty the framebuffer.
			framebufferDirty_ = true;
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
		break;

	case GE_CMD_ZBUFPTR:
		depthbuf.data = Memory::GetPointer(gstate.getDepthBufAddress());
		break;

	case GE_CMD_ZBUFWIDTH:
		depthbuf.data = Memory::GetPointer(gstate.getDepthBufAddress());
		break;

	case GE_CMD_AMBIENTCOLOR:
	case GE_CMD_AMBIENTALPHA:
	case GE_CMD_MATERIALAMBIENT:
	case GE_CMD_MATERIALDIFFUSE:
	case GE_CMD_MATERIALEMISSIVE:
	case GE_CMD_MATERIALSPECULAR:
	case GE_CMD_MATERIALALPHA:
	case GE_CMD_MATERIALSPECULARCOEF:
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
		break;

	case GE_CMD_LDX0:case GE_CMD_LDY0:case GE_CMD_LDZ0:
	case GE_CMD_LDX1:case GE_CMD_LDY1:case GE_CMD_LDZ1:
	case GE_CMD_LDX2:case GE_CMD_LDY2:case GE_CMD_LDZ2:
	case GE_CMD_LDX3:case GE_CMD_LDY3:case GE_CMD_LDZ3:
		break;

	case GE_CMD_LKA0:case GE_CMD_LKB0:case GE_CMD_LKC0:
	case GE_CMD_LKA1:case GE_CMD_LKB1:case GE_CMD_LKC1:
	case GE_CMD_LKA2:case GE_CMD_LKB2:case GE_CMD_LKC2:
	case GE_CMD_LKA3:case GE_CMD_LKB3:case GE_CMD_LKC3:
		break;

	case GE_CMD_LAC0:case GE_CMD_LAC1:case GE_CMD_LAC2:case GE_CMD_LAC3:
	case GE_CMD_LDC0:case GE_CMD_LDC1:case GE_CMD_LDC2:case GE_CMD_LDC3:
	case GE_CMD_LSC0:case GE_CMD_LSC1:case GE_CMD_LSC2:case GE_CMD_LSC3:
		break;

	case GE_CMD_VIEWPORTX1:
	case GE_CMD_VIEWPORTY1:
	case GE_CMD_VIEWPORTZ1:
	case GE_CMD_VIEWPORTX2:
	case GE_CMD_VIEWPORTY2:
	case GE_CMD_VIEWPORTZ2:
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		break;

	case GE_CMD_LIGHTMODE:
		break;

	case GE_CMD_PATCHDIVISION:
		break;

	case GE_CMD_MATERIALUPDATE:
		break;

	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
		break;

	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
		break;

	case GE_CMD_BLENDMODE:
		break;

	case GE_CMD_BLENDFIXEDA:
	case GE_CMD_BLENDFIXEDB:
		break;

	case GE_CMD_ALPHATESTENABLE:
	case GE_CMD_ALPHATEST:
		break;

	case GE_CMD_TEXFUNC:
	case GE_CMD_TEXFILTER:
		break;

	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
	case GE_CMD_STENCILTESTENABLE:
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
		gstate.worldmtxnum = data&0xF;
		break;

	case GE_CMD_WORLDMATRIXDATA:
		{
			int num = gstate.worldmtxnum & 0xF;
			if (num < 12) {
				gstate.worldMatrix[num] = getFloat24(data);
			}
			gstate.worldmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		gstate.viewmtxnum = data&0xF;
		break;

	case GE_CMD_VIEWMATRIXDATA:
		{
			int num = gstate.viewmtxnum & 0xF;
			if (num < 12) {
				gstate.viewMatrix[num] = getFloat24(data);
			}
			gstate.viewmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		gstate.projmtxnum = data&0xF;
		break;

	case GE_CMD_PROJMATRIXDATA:
		{
			int num = gstate.projmtxnum & 0xF;
			gstate.projMatrix[num] = getFloat24(data);
			gstate.projmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		gstate.texmtxnum = data&0xF;
		break;

	case GE_CMD_TGENMATRIXDATA:
		{
			int num = gstate.texmtxnum & 0xF;
			if (num < 12) {
				gstate.tgenMatrix[num] = getFloat24(data);
			}
			gstate.texmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		gstate.boneMatrixNumber = data & 0x7F;
		break;

	case GE_CMD_BONEMATRIXDATA:
		{
			int num = gstate.boneMatrixNumber & 0x7F;
			if (num < 96) {
				gstate.boneMatrix[num] = getFloat24(data);
			}
			gstate.boneMatrixNumber = (++num) & 0x7F;
		}
		break;

	default:
		GPUCommon::ExecuteOp(op, diff);
		break;
	}
}

void SoftGPU::UpdateStats()
{
	gpuStats.numVertexShaders = 0;
	gpuStats.numFragmentShaders = 0;
	gpuStats.numShaders = 0;
	gpuStats.numTextures = 0;
}

void SoftGPU::InvalidateCache(u32 addr, int size, GPUInvalidationType type)
{
	// Nothing to invalidate.
}

bool SoftGPU::PerformMemoryCopy(u32 dest, u32 src, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	// Let's just be safe.
	framebufferDirty_ = true;
	return false;
}

bool SoftGPU::PerformMemorySet(u32 dest, u8 v, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	// Let's just be safe.
	framebufferDirty_ = true;
	return false;
}

bool SoftGPU::PerformMemoryDownload(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	return false;
}

bool SoftGPU::PerformMemoryUpload(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	return false;
}

bool SoftGPU::PerformStencilUpload(u32 dest, int size)
{
	return false;
}

bool SoftGPU::FramebufferDirty() {
	if (g_Config.bSeparateCPUThread) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	if (g_Config.iFrameSkip != 0) {
		bool dirty = framebufferDirty_;
		framebufferDirty_ = false;
		return dirty;
	}
	return true;
}

bool SoftGPU::GetCurrentFramebuffer(GPUDebugBuffer &buffer)
{
	const int w = gstate.getRegionX2() - gstate.getRegionX1() + 1;
	const int h = gstate.getRegionY2() - gstate.getRegionY1() + 1;
	buffer.Allocate(w, h, gstate.FrameBufFormat());

	const int depth = gstate.FrameBufFormat() == GE_FORMAT_8888 ? 4 : 2;
	const u8 *src = fb.data + gstate.FrameBufStride() * depth * gstate.getRegionY1();
	u8 *dst = buffer.GetData();
	for (int y = gstate.getRegionY1(); y <= gstate.getRegionY2(); ++y) {
		memcpy(dst, src + gstate.getRegionX1(), (gstate.getRegionX2() + 1) * depth);
		dst += w * depth;
		src += gstate.FrameBufStride() * depth;
	}
	return true;
}

bool SoftGPU::GetCurrentDepthbuffer(GPUDebugBuffer &buffer)
{
	const int w = gstate.getRegionX2() - gstate.getRegionX1() + 1;
	const int h = gstate.getRegionY2() - gstate.getRegionY1() + 1;
	buffer.Allocate(w, h, GPU_DBG_FORMAT_16BIT);

	const int depth = 2;
	const u8 *src = depthbuf.data + gstate.DepthBufStride() * depth * gstate.getRegionY1();
	u8 *dst = buffer.GetData();
	for (int y = gstate.getRegionY1(); y <= gstate.getRegionY2(); ++y) {
		memcpy(dst, src + gstate.getRegionX1(), (gstate.getRegionX2() + 1) * depth);
		dst += w * depth;
		src += gstate.DepthBufStride() * depth;
	}
	return true;
}

bool SoftGPU::GetCurrentStencilbuffer(GPUDebugBuffer &buffer)
{
	return Rasterizer::GetCurrentStencilbuffer(buffer);
}

bool SoftGPU::GetCurrentTexture(GPUDebugBuffer &buffer, int level)
{
	return Rasterizer::GetCurrentTexture(buffer, level);
}

bool SoftGPU::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices)
{
	return TransformUnit::GetCurrentSimpleVertices(count, vertices, indices);
}
