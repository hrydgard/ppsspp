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
#include "Core/MemMap.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"
#include "Core/Reporting.h"
#include "gfx/gl_common.h"

#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/Colors.h"

static GLuint temp_texture = 0;

static GLint attr_pos = -1, attr_tex = -1;
static GLint uni_tex = -1;

static GLuint program;

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;
u8* fb = NULL;
u8* depthbuf = NULL;
u32 clut[4096];

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
	if(Result && stringBufferUsage) {
		// not nice
	} else if(!Result) {
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
	if(Result && stringBufferUsage) {
		// not nice
	} else if(!Result) {
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
	if(Result && stringBufferUsage) {
		// not nice
	} else if(!Result && !shader_errors) {
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
		"varying vec2 TexCoordOut;\n"
		"uniform sampler2D Texture;\n"
		"void main() {\n"
		"   vec4 tmpcolor;\n"
		"   tmpcolor = texture2D(Texture, TexCoordOut);\n"
		"   gl_FragColor = tmpcolor;\n"
		"}\n";
	static const char *vertShaderText =
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

	fb = Memory::GetPointer(0x44000000); // TODO: correct default address?
	depthbuf = Memory::GetPointer(0x44000000); // TODO: correct default address?
}

SoftGPU::~SoftGPU()
{
	glDeleteProgram(program);
	glDeleteTextures(1, &temp_texture);
}

// Copies RGBA8 data from RAM to the currently bound render target.
void CopyToCurrentFboFromRam(u8* data, int srcwidth, int srcheight, int dstwidth, int dstheight)
{
    glDisable(GL_BLEND);
	glViewport(0, 0, dstwidth, dstheight);
	glScissor(0, 0, dstwidth, dstheight);

	glBindTexture(GL_TEXTURE_2D, temp_texture);

	GLfloat texvert_u;
	if (gstate.FrameBufFormat() == GE_FORMAT_8888) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)gstate.FrameBufStride(), (GLsizei)srcheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		texvert_u = (float)srcwidth / gstate.FrameBufStride();
	} else {
		// TODO: This should probably be converted in a shader instead..
		// TODO: Do something less brain damaged to manage this buffer...
		u32 *buf = new u32[srcwidth * srcheight];
		u16 *fb16 = (u16 *)fb;
		for (int y = 0; y < srcheight; ++y) {
			u32 *buf_line = &buf[y * srcwidth];
			u16 *fb_line = &fb16[y * gstate.FrameBufStride()];

			switch (gstate.FrameBufFormat()) {
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
				ERROR_LOG_REPORT(G3D, "Unexpected framebuffer format: %d", gstate.FrameBufFormat());
			}
		}

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)srcwidth, (GLsizei)srcheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		texvert_u = 1.0f;

		delete[] buf;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glUseProgram(program);

	static const GLfloat verts[4][2] = {
		{ -1, -1}, // Left top
		{ -1,  1}, // left bottom
		{  1,  1}, // right bottom
		{  1, -1}  // right top
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
	CopyToCurrentFboFromRam(fb, FB_WIDTH, FB_HEIGHT, PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
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

void SoftGPU::ExecuteOp(u32 op, u32 diff)
{
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
	case GE_CMD_BASE:
		DEBUG_LOG(G3D,"DL BASE: %06x", data);
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

			if (type != GE_PRIM_TRIANGLES && type != GE_PRIM_TRIANGLE_STRIP && type != GE_PRIM_TRIANGLE_FAN && type != GE_PRIM_RECTANGLES) {
				ERROR_LOG(G3D, "DL DrawPrim type: %s count: %i vaddr= %08x, iaddr= %08x", type<7 ? types[type] : "INVALID", count, gstate_c.vertexAddr, gstate_c.indexAddr);
				break;
			}

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			void *verts = Memory::GetPointer(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG(G3D, "Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				indices = Memory::GetPointer(gstate_c.indexAddr);
			}

			TransformUnit::SubmitPrimitive(verts, indices, type, count, gstate.vertType);
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
				ERROR_LOG(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			void *control_points = Memory::GetPointer(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG(G3D, "Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				indices = Memory::GetPointer(gstate_c.indexAddr);
			}

			if (gstate.getPatchPrimitiveType() != GE_PATCHPRIM_TRIANGLES) {
				ERROR_LOG(G3D, "Unsupported patch primitive %x", gstate.patchprimitive&3);
				break;
			}

			TransformUnit::SubmitSpline(control_points, indices, sp_ucount, sp_vcount, sp_utype, sp_vtype, gstate.getPatchPrimitiveType(), gstate.vertType);
			DEBUG_LOG(G3D,"DL DRAW SPLINE: %i x %i, %i x %i", sp_ucount, sp_vcount, sp_utype, sp_vtype);
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

	case GE_CMD_VERTEXTYPE:
		DEBUG_LOG(G3D,"DL SetVertexType: %06x", data);
		// This sets through-mode or not, as well.
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
		break;

	case GE_CMD_CULLFACEENABLE: 
		DEBUG_LOG(G3D, "DL CullFace Enable: %i   (ignoring)", data);
		break;

	case GE_CMD_TEXTUREMAPENABLE: 
		DEBUG_LOG(G3D, "DL Texture map enable: %i", data);
		break;

	case GE_CMD_LIGHTINGENABLE:
		DEBUG_LOG(G3D, "DL Lighting enable: %i", data);
		break;

	case GE_CMD_FOGENABLE:		
		DEBUG_LOG(G3D, "DL Fog Enable: %i", gstate.fogEnable);
		break;

	case GE_CMD_DITHERENABLE:
		DEBUG_LOG(G3D, "DL Dither Enable: %i", gstate.ditherEnable);
		break;

	case GE_CMD_OFFSETX:		
		DEBUG_LOG(G3D, "DL Offset X: %i", gstate.offsetx);
		break;

	case GE_CMD_OFFSETY:		
		DEBUG_LOG(G3D, "DL Offset Y: %i", gstate.offsety);
		break;

	case GE_CMD_TEXSCALEU: 
		gstate_c.uv.uScale = getFloat24(data); 
		DEBUG_LOG(G3D, "DL Texture U Scale: %f", gstate_c.uv.uScale);
		break;

	case GE_CMD_TEXSCALEV: 
		gstate_c.uv.vScale = getFloat24(data); 
		DEBUG_LOG(G3D, "DL Texture V Scale: %f", gstate_c.uv.vScale);
		break;

	case GE_CMD_TEXOFFSETU: 
		gstate_c.uv.uOff = getFloat24(data);	
		DEBUG_LOG(G3D, "DL Texture U Offset: %f", gstate_c.uv.uOff);
		break;

	case GE_CMD_TEXOFFSETV: 
		gstate_c.uv.vOff = getFloat24(data);	
		DEBUG_LOG(G3D, "DL Texture V Offset: %f", gstate_c.uv.vOff);
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
		DEBUG_LOG(G3D, "DL MinZ: %i", data);
		break;

	case GE_CMD_MAXZ: 
		DEBUG_LOG(G3D, "DL MaxZ: %i", data);
		break;

	case GE_CMD_FRAMEBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			fb = Memory::GetPointer(0x44000000 | (gstate.fbptr & 0xFFE000) | ((gstate.fbwidth & 0xFF0000) << 8));
			DEBUG_LOG(G3D, "DL FramebufPtr: %08x", ptr);
		}
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		{
			u32 w = data & 0xFFFFFF;
			fb = Memory::GetPointer(0x44000000 | (gstate.fbptr & 0xFFE000) | ((gstate.fbwidth & 0xFF0000) << 8));
			DEBUG_LOG(G3D, "DL FramebufWidth: %i", w);
		}
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		break;

	case GE_CMD_TEXADDR0:
		gstate_c.textureChanged=true;
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
		gstate_c.textureChanged=true;
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
		{
			u32 clutAddr = gstate.getClutAddress();
			u32 clutTotalBytes = gstate.getClutLoadBytes();

			if (Memory::IsValidAddress(clutAddr)) {
				Memory::MemcpyUnchecked(clut, clutAddr, clutTotalBytes);
			} else {
				// TODO: Does this make any sense?
				memset(clut, 0xFF, clutTotalBytes);
			}

			if (clutAddr)
			{
				DEBUG_LOG(G3D,"DL Clut load: %08x", clutAddr);
			}
			else
			{
				DEBUG_LOG(G3D,"DL Empty Clut load");
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

			break;
		}

	case GE_CMD_TEXSIZE0:
		gstate_c.textureChanged=true;
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
		DEBUG_LOG(G3D,"DL Texture Size: %06x",	data);
		break;

	case GE_CMD_ZBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			depthbuf = Memory::GetPointer(0x44000000 | (gstate.zbptr & 0xFFE000) | ((gstate.zbwidth & 0xFF0000) << 8));
			DEBUG_LOG(G3D,"Zbuf Ptr: %06x", ptr);
		}
		break;

	case GE_CMD_ZBUFWIDTH:
		{
			u32 w = data & 0xFFFFFF;
			depthbuf = Memory::GetPointer(0x44000000 | (gstate.zbptr & 0xFFE000) | ((gstate.zbwidth & 0xFF0000) << 8));
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
		break;

	case GE_CMD_MATERIALDIFFUSE:
		DEBUG_LOG(G3D,"DL Material Diffuse Color: %06x",	data);
		break;

	case GE_CMD_MATERIALEMISSIVE:
		DEBUG_LOG(G3D,"DL Material Emissive Color: %06x",	data);
		break;

	case GE_CMD_MATERIALSPECULAR:
		DEBUG_LOG(G3D,"DL Material Specular Color: %06x",	data);
		break;

	case GE_CMD_MATERIALALPHA:
		DEBUG_LOG(G3D,"DL Material Alpha Color: %06x",	data);
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		DEBUG_LOG(G3D,"DL Material specular coef: %f", getFloat24(data));
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
		}
		break;


	case GE_CMD_LAC0:case GE_CMD_LAC1:case GE_CMD_LAC2:case GE_CMD_LAC3:
	case GE_CMD_LDC0:case GE_CMD_LDC1:case GE_CMD_LDC2:case GE_CMD_LDC3:
	case GE_CMD_LSC0:case GE_CMD_LSC1:case GE_CMD_LSC2:case GE_CMD_LSC3:
		{
			float r = (float)(data>>16)/255.0f;
			float g = (float)((data>>8) & 0xff)/255.0f;
			float b = (float)(data & 0xff)/255.0f;

			int l = (cmd - GE_CMD_LAC0) / 3;
			int t = (cmd - GE_CMD_LAC0) % 3;
			gstate_c.lightColor[t][l][0] = r;
			gstate_c.lightColor[t][l][1] = g;
			gstate_c.lightColor[t][l][2] = b;
		}
		break;

	case GE_CMD_VIEWPORTX1:
	case GE_CMD_VIEWPORTY1:
	case GE_CMD_VIEWPORTZ1:
	case GE_CMD_VIEWPORTX2:
	case GE_CMD_VIEWPORTY2:
	case GE_CMD_VIEWPORTZ2:
		DEBUG_LOG(G3D,"DL Viewport param %i: %f", cmd-GE_CMD_VIEWPORTX1, getFloat24(data));
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

	case GE_CMD_LIGHTMODE:
		DEBUG_LOG(G3D,"DL Shade mode: %06x", data);
		break;

	case GE_CMD_PATCHDIVISION:
		break;

	case GE_CMD_MATERIALUPDATE:
		DEBUG_LOG(G3D,"DL Material Update: %d", data);
		break;


	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
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
		break;

	case GE_CMD_TEXFUNC:
		DEBUG_LOG(G3D,"DL TexFunc %i", data&7);
		break;
	case GE_CMD_TEXFILTER:
		{
			int min = data & 7;
			int mag = (data >> 8) & 1;
			DEBUG_LOG(G3D,"DL TexFilter min: %i mag: %i", min, mag);
		}

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
		DEBUG_LOG(G3D,"DL Z test mode: %i", data);
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
		DEBUG_LOG(G3D,"DL World matrix # %i", data);
		gstate.worldmtxnum = data&0xF;
		break;

	case GE_CMD_WORLDMATRIXDATA:
		DEBUG_LOG(G3D,"DL World matrix data # %f", getFloat24(data));
		gstate.worldMatrix[gstate.worldmtxnum++] = getFloat24(data);
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL VIEW matrix # %i", data);
		gstate.viewmtxnum = data&0xF;
		break;

	case GE_CMD_VIEWMATRIXDATA:
		DEBUG_LOG(G3D,"DL VIEW matrix data # %f", getFloat24(data));
		gstate.viewMatrix[gstate.viewmtxnum++] = getFloat24(data);
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL PROJECTION matrix # %i", data);
		gstate.projmtxnum = data&0xF;
		break;

	case GE_CMD_PROJMATRIXDATA:
		DEBUG_LOG(G3D,"DL PROJECTION matrix data # %f", getFloat24(data));
		gstate.projMatrix[gstate.projmtxnum++] = getFloat24(data);
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL TGEN matrix # %i", data);
		gstate.texmtxnum = data&0xF;
		break;

	case GE_CMD_TGENMATRIXDATA:
		DEBUG_LOG(G3D,"DL TGEN matrix data # %f", getFloat24(data));
		gstate.tgenMatrix[gstate.texmtxnum++] = getFloat24(data);
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		DEBUG_LOG(G3D,"DL BONE matrix #%i", data);
		gstate.boneMatrixNumber = data;
		break;

	case GE_CMD_BONEMATRIXDATA:
		DEBUG_LOG(G3D,"DL BONE matrix data #%i %f", gstate.boneMatrixNumber, getFloat24(data));
		gstate.boneMatrix[gstate.boneMatrixNumber++] = getFloat24(data);
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

void SoftGPU::UpdateMemory(u32 dest, u32 src, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
}
