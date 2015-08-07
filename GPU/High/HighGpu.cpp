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

#include <algorithm>
#include <cstring>

#include "base/logging.h"
#include "gfx_es2/gl_state.h"
#include "profiler/profiler.h"

#include "Common/ChunkFile.h"

#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"

#include "GPU/Common/MemoryArena.h"
#include "GPU/Common/VertexDecoderCommon.cpp"
#include "GPU/High/Command.h"
#include "GPU/High/HighGpu.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

namespace HighGpu {

// TODO: can we get rid of these and only keep EXECUTE? If so we can merge EXECUTE into the dirtyState field.
enum {
	FLAG_EXECUTE = 1,
};

enum {
	ArenaSize = 4096 * 1024,
};

struct CommandTableEntry {
	u8 cmd;
	u8 flags;
	u32 dirtyState;
	HighGpuFrontend::CmdFunc func;
};

// This table gets crunched into a faster form by init.
// TODO: Share this table between the backends. Will have to make another indirection for the function pointers though..
static const CommandTableEntry commandTable[] = {
	// Changes that dirty the framebuffer
	{GE_CMD_FRAMEBUFPTR, 0, STATE_FRAMEBUF},
	{GE_CMD_FRAMEBUFWIDTH, 0, STATE_FRAMEBUF},
	{GE_CMD_FRAMEBUFPIXFORMAT, 0, STATE_FRAMEBUF},
	{GE_CMD_ZBUFPTR, 0, STATE_FRAMEBUF},
	{GE_CMD_ZBUFWIDTH, 0, STATE_FRAMEBUF},

	// Changes that dirty uniforms
	{GE_CMD_FOGCOLOR, 0, STATE_FRAGMENT},
	{GE_CMD_FOG1, 0, STATE_FRAGMENT},
	{GE_CMD_FOG2, 0, STATE_FRAGMENT},

	// Should these maybe flush?
	{GE_CMD_MINZ},
	{GE_CMD_MAXZ},

	// Changes that dirty texture scaling.
	{GE_CMD_TEXMAPMODE, 0, STATE_TEXSCALE},
	{GE_CMD_TEXSCALEU, 0, STATE_TEXSCALE},
	{GE_CMD_TEXSCALEV, 0, STATE_TEXSCALE},
	{GE_CMD_TEXOFFSETU, 0, STATE_TEXSCALE},
	{GE_CMD_TEXOFFSETV, 0, STATE_TEXSCALE},

	// Changes that dirty the current texture. Really should be possible to avoid executing these if we compile
	// by adding some more flags.
	{GE_CMD_TEXSIZE0, 0, STATE_TEXTURE},
	{GE_CMD_TEXSIZE1, 0, STATE_TEXTURE},
	{GE_CMD_TEXSIZE2, 0, STATE_TEXTURE},
	{GE_CMD_TEXSIZE3, 0, STATE_TEXTURE},
	{GE_CMD_TEXSIZE4, 0, STATE_TEXTURE},
	{GE_CMD_TEXSIZE5, 0, STATE_TEXTURE},
	{GE_CMD_TEXSIZE6, 0, STATE_TEXTURE},
	{GE_CMD_TEXSIZE7, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR0, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR1, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR2, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR3, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR4, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR5, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR6, 0, STATE_TEXTURE},
	{GE_CMD_TEXADDR7, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH0, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH1, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH2, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH3, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH4, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH5, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH6, 0, STATE_TEXTURE},
	{GE_CMD_TEXBUFWIDTH7, 0, STATE_TEXTURE},
	{GE_CMD_TEXFORMAT, 0, STATE_TEXTURE},
	{GE_CMD_TEXLEVEL, 0, STATE_TEXTURE},
	{GE_CMD_TEXMODE, 0, STATE_TEXTURE},  // swizzle, etc etc

	// These are merged into clutload command directly, no state object needed.
	{GE_CMD_CLUTADDR},
	{GE_CMD_CLUTADDRUPPER},
	{GE_CMD_CLUTFORMAT},

	{GE_CMD_TEXSHADELS,       0, STATE_TEXSCALE},  // The light sources to use for light-UV-gen

	// These affect the fragment shader
	{GE_CMD_CLEARMODE,        0, STATE_RASTERIZER},
	{GE_CMD_TEXTUREMAPENABLE, 0, STATE_ENABLES},
	{GE_CMD_FOGENABLE,        0},
	{GE_CMD_SHADEMODE,        0, STATE_FRAGMENT},
	{GE_CMD_TEXFUNC,          0, STATE_FRAGMENT},
	{GE_CMD_COLORTEST,        0, STATE_BLEND},
	{GE_CMD_ALPHATESTENABLE,  0, STATE_ENABLES},
	{GE_CMD_COLORTESTENABLE,  0, STATE_ENABLES},
	{GE_CMD_COLORTESTMASK,    0, STATE_BLEND},

	// These change the vertex shader
	{GE_CMD_REVERSENORMAL, 0, STATE_LIGHTGLOBAL},
	{GE_CMD_LIGHTINGENABLE, 0, STATE_ENABLES},
	{GE_CMD_LIGHTENABLE0, 0, STATE_ENABLES},
	{GE_CMD_LIGHTENABLE1, 0, STATE_ENABLES},
	{GE_CMD_LIGHTENABLE2, 0, STATE_ENABLES},
	{GE_CMD_LIGHTENABLE3, 0, STATE_ENABLES},
	{GE_CMD_LIGHTTYPE0, 0, STATE_LIGHT0},
	{GE_CMD_LIGHTTYPE1, 0, STATE_LIGHT1},
	{GE_CMD_LIGHTTYPE2, 0, STATE_LIGHT2},
	{GE_CMD_LIGHTTYPE3, 0, STATE_LIGHT3},
	{GE_CMD_MATERIALUPDATE, 0, STATE_LIGHTGLOBAL},

	// This changes both shaders so need flushing.
	{GE_CMD_LIGHTMODE, 0, STATE_LIGHTGLOBAL},
	{GE_CMD_TEXFILTER, 0, STATE_SAMPLER},
	{GE_CMD_TEXWRAP, 0, STATE_SAMPLER},

	// Uniform changes
	{GE_CMD_ALPHATEST, 0, STATE_BLEND},
	{GE_CMD_COLORREF, 0, STATE_BLEND},
	{GE_CMD_TEXENVCOLOR, 0, STATE_FRAGMENT},

	// Simple render state changes. Handled in StateMapping.cpp.
	{GE_CMD_OFFSETX, 0, STATE_RASTERIZER},
	{GE_CMD_OFFSETY, 0, STATE_RASTERIZER},
	{GE_CMD_CULL, 0, STATE_RASTERIZER},
	{GE_CMD_CULLFACEENABLE, 0, STATE_RASTERIZER},
	{GE_CMD_DITHERENABLE, 0, 0},
	{GE_CMD_STENCILOP, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_STENCILTEST, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_STENCILTESTENABLE, 0, STATE_ENABLES},
	{GE_CMD_ALPHABLENDENABLE, 0, STATE_ENABLES},
	{GE_CMD_BLENDMODE, 0, STATE_BLEND},
	{GE_CMD_BLENDFIXEDA, 0, STATE_BLEND},
	{GE_CMD_BLENDFIXEDB, 0, STATE_BLEND},
	{GE_CMD_MASKRGB, 0, STATE_BLEND},
	{GE_CMD_MASKALPHA, 0, STATE_BLEND},
	{GE_CMD_ZTEST, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_ZTESTENABLE, 0, STATE_ENABLES},
	{GE_CMD_ZWRITEDISABLE, 0, STATE_DEPTHSTENCIL},
#ifndef USING_GLES2
	{GE_CMD_LOGICOP, 0, STATE_BLEND},
	{GE_CMD_LOGICOPENABLE, 0, STATE_ENABLES},
#else
	{GE_CMD_LOGICOP, 0},
	{GE_CMD_LOGICOPENABLE, 0},
#endif

	// Can probably ignore this one as we don't support AA lines.
	{GE_CMD_ANTIALIASENABLE, 0},

	// Morph weights.
	{GE_CMD_MORPHWEIGHT0, 0, STATE_MORPH},
	{GE_CMD_MORPHWEIGHT1, 0, STATE_MORPH},
	{GE_CMD_MORPHWEIGHT2, 0, STATE_MORPH},
	{GE_CMD_MORPHWEIGHT3, 0, STATE_MORPH},
	{GE_CMD_MORPHWEIGHT4, 0, STATE_MORPH},
	{GE_CMD_MORPHWEIGHT5, 0, STATE_MORPH},
	{GE_CMD_MORPHWEIGHT6, 0, STATE_MORPH},
	{GE_CMD_MORPHWEIGHT7, 0, STATE_MORPH},

	// Control spline/bezier patches. Don't really require flushing as such, but meh.
	// We read these and bake into the draw commands so no need to put them in any state.
	{GE_CMD_PATCHDIVISION, 0},
	{GE_CMD_PATCHPRIMITIVE, 0},
	{GE_CMD_PATCHFACING, 0},
	{GE_CMD_PATCHCULLENABLE, 0},

	// Viewport.
	{GE_CMD_VIEWPORTX1, 0, STATE_VIEWPORT},
	{GE_CMD_VIEWPORTY1, 0, STATE_VIEWPORT},
	{GE_CMD_VIEWPORTX2, 0, STATE_VIEWPORT},
	{GE_CMD_VIEWPORTY2, 0, STATE_VIEWPORT},
	{GE_CMD_VIEWPORTZ1, 0, STATE_VIEWPORT},
	{GE_CMD_VIEWPORTZ2, 0, STATE_VIEWPORT},

	// Region. Only used as heuristic for framebuffer sizing. Its real meaning is for bezier patch culling.
	{GE_CMD_REGION1, 0},
	{GE_CMD_REGION2, 0},

	// Scissor
	{GE_CMD_SCISSOR1, 0, STATE_RASTERIZER},
	{GE_CMD_SCISSOR2, 0, STATE_RASTERIZER},

	{GE_CMD_AMBIENTCOLOR, 0, STATE_LIGHTGLOBAL},
	{GE_CMD_AMBIENTALPHA, 0, STATE_LIGHTGLOBAL},
	{GE_CMD_MATERIALDIFFUSE,      0, STATE_LIGHTGLOBAL},
	{GE_CMD_MATERIALEMISSIVE,     0, STATE_LIGHTGLOBAL},
	{GE_CMD_MATERIALAMBIENT,      0, STATE_LIGHTGLOBAL},
	{GE_CMD_MATERIALALPHA,        0, STATE_LIGHTGLOBAL},
	{GE_CMD_MATERIALSPECULAR,     0, STATE_LIGHTGLOBAL},
	{GE_CMD_MATERIALSPECULARCOEF, 0, STATE_LIGHTGLOBAL},

	{GE_CMD_LX0, 0, STATE_LIGHT0},
	{GE_CMD_LY0, 0, STATE_LIGHT0},
	{GE_CMD_LZ0, 0, STATE_LIGHT0},
	{GE_CMD_LX1, 0, STATE_LIGHT1},
	{GE_CMD_LY1, 0, STATE_LIGHT1},
	{GE_CMD_LZ1, 0, STATE_LIGHT1},
	{GE_CMD_LX2, 0, STATE_LIGHT2},
	{GE_CMD_LY2, 0, STATE_LIGHT2},
	{GE_CMD_LZ2, 0, STATE_LIGHT2},
	{GE_CMD_LX3, 0, STATE_LIGHT3},
	{GE_CMD_LY3, 0, STATE_LIGHT3},
	{GE_CMD_LZ3, 0, STATE_LIGHT3},

	{GE_CMD_LDX0, 0, STATE_LIGHT0},
	{GE_CMD_LDY0, 0, STATE_LIGHT0},
	{GE_CMD_LDZ0, 0, STATE_LIGHT0},
	{GE_CMD_LDX1, 0, STATE_LIGHT1},
	{GE_CMD_LDY1, 0, STATE_LIGHT1},
	{GE_CMD_LDZ1, 0, STATE_LIGHT1},
	{GE_CMD_LDX2, 0, STATE_LIGHT2},
	{GE_CMD_LDY2, 0, STATE_LIGHT2},
	{GE_CMD_LDZ2, 0, STATE_LIGHT2},
	{GE_CMD_LDX3, 0, STATE_LIGHT3},
	{GE_CMD_LDY3, 0, STATE_LIGHT3},
	{GE_CMD_LDZ3, 0, STATE_LIGHT3},

	{GE_CMD_LKA0, 0, STATE_LIGHT0},
	{GE_CMD_LKB0, 0, STATE_LIGHT0},
	{GE_CMD_LKC0, 0, STATE_LIGHT0},
	{GE_CMD_LKA1, 0, STATE_LIGHT1},
	{GE_CMD_LKB1, 0, STATE_LIGHT1},
	{GE_CMD_LKC1, 0, STATE_LIGHT1},
	{GE_CMD_LKA2, 0, STATE_LIGHT2},
	{GE_CMD_LKB2, 0, STATE_LIGHT2},
	{GE_CMD_LKC2, 0, STATE_LIGHT2},
	{GE_CMD_LKA3, 0, STATE_LIGHT3},
	{GE_CMD_LKB3, 0, STATE_LIGHT3},
	{GE_CMD_LKC3, 0, STATE_LIGHT3},

	{GE_CMD_LKS0, 0, STATE_LIGHT0},
	{GE_CMD_LKS1, 0, STATE_LIGHT1},
	{GE_CMD_LKS2, 0, STATE_LIGHT2},
	{GE_CMD_LKS3, 0, STATE_LIGHT3},

	{GE_CMD_LKO0, 0, STATE_LIGHT0},
	{GE_CMD_LKO1, 0, STATE_LIGHT1},
	{GE_CMD_LKO2, 0, STATE_LIGHT2},
	{GE_CMD_LKO3, 0, STATE_LIGHT3},

	{GE_CMD_LAC0, 0, STATE_LIGHT0},
	{GE_CMD_LDC0, 0, STATE_LIGHT0},
	{GE_CMD_LSC0, 0, STATE_LIGHT0},
	{GE_CMD_LAC1, 0, STATE_LIGHT1},
	{GE_CMD_LDC1, 0, STATE_LIGHT1},
	{GE_CMD_LSC1, 0, STATE_LIGHT1},
	{GE_CMD_LAC2, 0, STATE_LIGHT2},
	{GE_CMD_LDC2, 0, STATE_LIGHT2},
	{GE_CMD_LSC2, 0, STATE_LIGHT2},
	{GE_CMD_LAC3, 0, STATE_LIGHT3},
	{GE_CMD_LDC3, 0, STATE_LIGHT3},
	{GE_CMD_LSC3, 0, STATE_LIGHT3},

	// Ignored commands
	{GE_CMD_CLIPENABLE},
	{GE_CMD_TEXFLUSH},
	{GE_CMD_TEXLODSLOPE},
	{GE_CMD_TEXSYNC},

	// These are just nop or part of other later commands.
	{GE_CMD_NOP},
	{GE_CMD_BASE},
	{GE_CMD_TRANSFERSRC},
	{GE_CMD_TRANSFERSRCW},
	{GE_CMD_TRANSFERDST},
	{GE_CMD_TRANSFERDSTW},
	{GE_CMD_TRANSFERSRCPOS},
	{GE_CMD_TRANSFERDSTPOS},
	{GE_CMD_TRANSFERSIZE},

	// From Common. No flushing but definitely need execute. These don't affect drawing, only the
	// command processor's operation.
	{GE_CMD_OFFSETADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_OffsetAddr},
	{GE_CMD_ORIGIN, FLAG_EXECUTE , 0, &GPUCommon::Execute_Origin},  // Really?
	{GE_CMD_PRIM, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Prim},
	{GE_CMD_JUMP, FLAG_EXECUTE, 0, &GPUCommon::Execute_Jump},
	{GE_CMD_CALL, FLAG_EXECUTE, 0, &GPUCommon::Execute_Call},
	{GE_CMD_RET, FLAG_EXECUTE, 0, &GPUCommon::Execute_Ret},
	{GE_CMD_END, FLAG_EXECUTE, 0, &GPUCommon::Execute_End},  // Flush?
	{GE_CMD_VADDR, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Vaddr},
	{GE_CMD_IADDR, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Iaddr},
	{GE_CMD_BJUMP, FLAG_EXECUTE, 0, &GPUCommon::Execute_BJump},  // EXECUTE
	{GE_CMD_BOUNDINGBOX, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_BoundingBox},

	{GE_CMD_VERTEXTYPE},  // Baked into draw calls

	{GE_CMD_BEZIER, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Bezier},
	{GE_CMD_SPLINE, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Spline},

	// These two are actually processed in CMD_END.
	{GE_CMD_SIGNAL},
	{GE_CMD_FINISH},

	// Changes that trigger data copies. Only flushing on change for LOADCLUT must be a bit of a hack...
	{GE_CMD_LOADCLUT, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_LoadClut},
	{GE_CMD_TRANSFERSTART, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_BlockTransferStart},

	// We don't use the dither table.
	{GE_CMD_DITH0},
	{GE_CMD_DITH1},
	{GE_CMD_DITH2},
	{GE_CMD_DITH3},

	// These handle their own flushing.
	{GE_CMD_WORLDMATRIXNUMBER, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_WorldMtxNum},
	{GE_CMD_WORLDMATRIXDATA,   FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_WorldMtxData},
	{GE_CMD_VIEWMATRIXNUMBER,  FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_ViewMtxNum},
	{GE_CMD_VIEWMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_ViewMtxData},
	{GE_CMD_PROJMATRIXNUMBER,  FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_ProjMtxNum},
	{GE_CMD_PROJMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_ProjMtxData},
	{GE_CMD_TGENMATRIXNUMBER,  FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_TgenMtxNum},
	{GE_CMD_TGENMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_TgenMtxData},
	{GE_CMD_BONEMATRIXNUMBER,  FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_BoneMtxNum},
	{GE_CMD_BONEMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_BoneMtxData},

	// Vertex Screen/Texture/Color
	// These are unimplemented and do not seem to be used.
	{GE_CMD_VSCX},
	{GE_CMD_VSCY},
	{GE_CMD_VSCZ},
	{GE_CMD_VTCS},
	{GE_CMD_VTCT},
	{GE_CMD_VTCQ},
	{GE_CMD_VCV},
	{GE_CMD_VAP},
	{GE_CMD_VFC},
	{GE_CMD_VSCV},

	// "Missing" commands (gaps in the sequence)
	{GE_CMD_UNKNOWN_03},
	{GE_CMD_UNKNOWN_0D},
	{GE_CMD_UNKNOWN_11},
	{GE_CMD_UNKNOWN_29},
	{GE_CMD_UNKNOWN_34},
	{GE_CMD_UNKNOWN_35},
	{GE_CMD_UNKNOWN_39},
	{GE_CMD_UNKNOWN_4E},
	{GE_CMD_UNKNOWN_4F},
	{GE_CMD_UNKNOWN_52},
	{GE_CMD_UNKNOWN_59},
	{GE_CMD_UNKNOWN_5A},
	{GE_CMD_UNKNOWN_B6},
	{GE_CMD_UNKNOWN_B7},
	{GE_CMD_UNKNOWN_D1},
	{GE_CMD_UNKNOWN_ED},
	{GE_CMD_UNKNOWN_EF},
	{GE_CMD_UNKNOWN_FA},
	{GE_CMD_UNKNOWN_FB},
	{GE_CMD_UNKNOWN_FC},
	{GE_CMD_UNKNOWN_FD},
	{GE_CMD_UNKNOWN_FE},
	// Appears to be debugging related or something?  Hit a lot in GoW.
	{GE_CMD_UNKNOWN_FF},
};

HighGpuFrontend::CommandInfo HighGpuFrontend::cmdInfo_[256];

HighGpuFrontend::HighGpuFrontend(HighGpuBackend *backend)
: resized_(false), dirty_(0), backend_(backend), arena_(ArenaSize) {
	clutData_ = new u8[1024];
	// Sanity check cmdInfo_ table - no dupes please
	std::set<u8> dupeCheck;
	memset(cmdInfo_, 0, sizeof(cmdInfo_));
	for (size_t i = 0; i < ARRAY_SIZE(commandTable); i++) {
		const u8 cmd = commandTable[i].cmd;
		if (dupeCheck.find(cmd) != dupeCheck.end()) {
			ERROR_LOG(G3D, "Command table Dupe: %02x (%i)", (int)cmd, (int)cmd);
		} else {
			dupeCheck.insert(cmd);
		}
		cmdInfo_[cmd].flags = commandTable[i].flags;
		cmdInfo_[cmd].dirtyState = commandTable[i].dirtyState;
		cmdInfo_[cmd].func = commandTable[i].func;
	}

	// Find commands missing from the table.
	for (int i = 0; i < 0xEF; i++) {
		if (dupeCheck.find((u8)i) == dupeCheck.end()) {
			ERROR_LOG(G3D, "Command missing from table: %02x (%i)", i, i);
		}
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.

	// Update after init to be sure of any silly driver problems.
	backend_->UpdateVsyncInterval(true);

	CommandInitDummyDraw(&dummyDraw_);

	cmdPacket_ = new CommandPacket();
	CommandPacketInit(cmdPacket_, 256, clutData_);
	CommandPacketReset(cmdPacket_, &dummyDraw_);

	// Some of our defaults are different from hw defaults, let's assert them.
	// We restore each frame anyway, but here is convenient for tests.
	glstate.Restore();
}

HighGpuFrontend::~HighGpuFrontend() {
	CommandPacketDeinit(cmdPacket_);
	delete cmdPacket_;
	delete backend_;
	delete [] clutData_;
}

void HighGpuFrontend::GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) {
	backend_->GetReportingInfo(primaryInfo, fullInfo);
}

// Should only be executed on the GPU thread.
void HighGpuFrontend::DeviceLost() {
	ILOG("HighGpuFrontend: DeviceLost");
	backend_->DeviceLost();
}

void HighGpuFrontend::InitClear() {
	ScheduleEvent(GPU_EVENT_INIT_CLEAR);
}

void HighGpuFrontend::Reinitialize() {
	GPUCommon::Reinitialize();
	ScheduleEvent(GPU_EVENT_REINITIALIZE);
}


void HighGpuFrontend::DumpNextFrame() {
	dumpNextFrame_ = true;
}

void HighGpuFrontend::BeginFrame() {
	ScheduleEvent(GPU_EVENT_BEGIN_FRAME);
}

void HighGpuFrontend::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	host->GPUNotifyDisplay(framebuf, stride, format);
	// framebufferManager_.SetDisplayFramebuffer(framebuf, stride, format);
}

bool HighGpuFrontend::FramebufferDirty() {
	// TODO
	return true;
}

bool HighGpuFrontend::FramebufferReallyDirty() {
	// TODO
	return true;
}

void HighGpuFrontend::CopyDisplayToOutput() {
	ScheduleEvent(GPU_EVENT_COPY_DISPLAY_TO_OUTPUT);
}

// This is probably worth writing a highly optimized ASM version of.
void HighGpuFrontend::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("gpuhighloop");

	const CommandInfo *cmdInfo = cmdInfo_;
	int dc = downcount;
	// TODO: Move dirty_ to a local variable.
	for (; dc > 0; --dc) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer even on 32-bit
		const u32 op = *(const u32 *)(Memory::base + list.pc);
		const u32 cmd = op >> 24;
		const CommandInfo info = cmdInfo[cmd];
		const u8 cmdFlags = info.flags;
		const u32 diff = op ^ gstate.cmdmem[cmd];
		if (diff) {
			gstate.cmdmem[cmd] = op;
			dirty_ |= info.dirtyState;
		}
		if (cmdFlags & FLAG_EXECUTE) {
			downcount = dc;
			(this->*info.func)(op, diff);
			dc = downcount;
		}
		list.pc += 4;
	}
	downcount = 0;
}

void HighGpuFrontend::ProcessEvent(GPUEvent ev) {
	if (!backend_->ProcessEvent(ev)) {
		GPUCommon::ProcessEvent(ev);
	}
}

void HighGpuFrontend::PreExecuteOp(u32 op, u32 diff) { }

void HighGpuFrontend::ExecuteOp(u32 op, u32 diff) {
	const u8 cmd = op >> 24;
	const CommandInfo info = cmdInfo_[cmd];
	const u8 cmdFlags = info.flags;
	if (cmdFlags & FLAG_EXECUTE) {
		(this->*info.func)(op, diff);
	}
}

void HighGpuFrontend::Execute_Vaddr(u32 op, u32 diff) {
	gstate_c.vertexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void HighGpuFrontend::Execute_Iaddr(u32 op, u32 diff) {
	gstate_c.indexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void HighGpuFrontend::Execute_Prim(u32 op, u32 diff) {
	u32 data = op & 0xFFFFFF;
	u32 count = data & 0xFFFF;
	GEPrimitiveType prim = static_cast<GEPrimitiveType>(data >> 16);
	if (count == 0)
		return;

	// Discard AA lines as we can't do anything that makes sense with these anyway. The SW plugin might, though.
	if (gstate.isAntiAliasEnabled()) {
		// Discard AA lines in DOA Paradise
		if (prim == GE_PRIM_LINE_STRIP)
			return;
		// Discard AA lines in Summon Night 5
		if ((prim == GE_PRIM_LINES) && gstate.isSkinningEnabled())
			return;
	}

	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB))	{
		// TODO: Figure out a way to do cycle estimates here, without disturbing the draw engine
		// which might be running on another thread.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	bool indexed = false;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indexed = true;
	}

	dirty_ = CommandSubmitDraw(cmdPacket_, &arena_, &gstate, dirty_, data, gstate_c.vertexAddr, indexed ? gstate_c.indexAddr : 0);

	// After submitting the drawcall, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	if (indexed) {
		if ((gstate.vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT) {
			count *= sizeof(u16);
		}
		gstate_c.indexAddr += count;
	} else {
		// TODO: Add a very small (4-entry?) LRU cache for these. Cannot use the vertexdecodercache here
		// as it belongs to the drawing thread, and it's not worth adding locking.
		int vertexSize = ComputePSPVertexSize(gstate.vertType);
		gstate_c.vertexAddr += count * vertexSize;
	}

	if (cmdPacket_->full)
		FlushCommandPacket();
}

void HighGpuFrontend::Execute_Bezier(u32 op, u32 diff) {
	// TODO
}

void HighGpuFrontend::Execute_Spline(u32 op, u32 diff) {
	// TODO
}

void HighGpuFrontend::Execute_BlockTransferStart(u32 op, u32 diff) {
	CommandSubmitTransfer(cmdPacket_, &gstate);
}

void HighGpuFrontend::Execute_BoundingBox(u32 op, u32 diff) {
	// TODO: Need to extract TestBoundingBox from transformDraw
}

void HighGpuFrontend::Execute_LoadClut(u32 op, u32 diff) {
	u32 clutAddr = gstate.getClutAddress();
	u32 loadBytes = gstate.getClutLoadBytes();
	clutTotalBytes_ = loadBytes;

	void *clutBufRaw_ = (void *)clutData_;
	if (Memory::IsValidAddress(clutAddr)) {
		// It's possible for a game to (successfully) access outside valid memory.
		u32 bytes = Memory::ValidSize(clutAddr, loadBytes);
#ifdef _M_SSE
		int numBlocks = bytes / 16;
		if (bytes == loadBytes) {
			const __m128i *source = (const __m128i *)Memory::GetPointerUnchecked(clutAddr);
			__m128i *dest = (__m128i *)clutBufRaw_;
			// Is the source really allowed to be misaligned?
			for (int i = 0; i < numBlocks; i++, source += 2, dest += 2) {
				__m128i data1 = _mm_loadu_si128(source);
				__m128i data2 = _mm_loadu_si128(source + 1);
				_mm_store_si128(dest, data1);
				_mm_store_si128(dest + 1, data2);
			}
		} else {
			Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, bytes);
			if (bytes < loadBytes) {
				memset((u8 *)clutBufRaw_ + bytes, 0x00, loadBytes - bytes);
			}
		}
#else
		Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, bytes);
		if (bytes < clutTotalBytes_) {
			memset((u8 *)clutBufRaw_ + bytes, 0x00, clutTotalBytes_ - bytes);
		}
#endif
	} else {
		memset(clutBufRaw_, 0x00, loadBytes);
	}

	// Reload the clut next time.
	clutMaxBytes_ = std::max(clutMaxBytes_, loadBytes);
	// TODO: Check during the loops above if the CLUT contents actually changed?
	dirty_ |= STATE_CLUT;
}

void HighGpuFrontend::Execute_WorldMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_WORLDMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.worldMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;
	bool dirty = false;
	while ((src[i] >> 24) == GE_CMD_WORLDMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			dst[i] = newVal;
			dirty = true;
		}
		if (++i >= end) {
			break;
		}
	}
	if (dirty) {
		dirty_ |= STATE_WORLDMATRIX;
	}
	const int count = i;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_WorldMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.worldmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.worldMatrix)[num]) {
		((u32 *)gstate.worldMatrix)[num] = newVal;
		dirty_ |= STATE_WORLDMATRIX;
	}
	num++;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | (num & 0xF);
}

void HighGpuFrontend::Execute_ViewMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_VIEWMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.viewMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	bool dirty = false;
	while ((src[i] >> 24) == GE_CMD_VIEWMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			dst[i] = newVal;
			dirty = true;
		}
		if (++i >= end) {
			break;
		}
	}

	if (dirty)
		dirty_ |= STATE_VIEWMATRIX;

	const int count = i;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_ViewMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.viewmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.viewMatrix)[num]) {
		((u32 *)gstate.viewMatrix)[num] = newVal;
		dirty_ |= STATE_VIEWMATRIX;
	}
	num++;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | (num & 0xF);
}

void HighGpuFrontend::Execute_ProjMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_PROJMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.projMatrix + (op & 0xF));
	const int end = 16 - (op & 0xF);
	int i = 0;

	bool dirty = false;
	while ((src[i] >> 24) == GE_CMD_PROJMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			dirty = true;
			dst[i] = newVal;
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | ((op + count) & 0xF);

	if (dirty) {
		dirty_ |= STATE_PROJMATRIX;
	}

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_ProjMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.projmtxnum & 0xF;
	u32 newVal = op << 8;
	if (newVal != ((const u32 *)gstate.projMatrix)[num]) {
		((u32 *)gstate.projMatrix)[num] = newVal;
		dirty_ |= STATE_PROJMATRIX;
	}
	num++;
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | (num & 0xF);
}

void HighGpuFrontend::Execute_TgenMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_TGENMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.tgenMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	bool dirty = false;
	while ((src[i] >> 24) == GE_CMD_TGENMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			dst[i] = newVal;
			dirty = true;
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | ((op + count) & 0xF);

	if (dirty) {
		dirty_ |= STATE_TEXMATRIX;
	}

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_TgenMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.texmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.tgenMatrix)[num]) {
		((u32 *)gstate.tgenMatrix)[num] = newVal;
		dirty_ |= STATE_TEXMATRIX;
	}
	num++;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | (num & 0xF);
}

void HighGpuFrontend::Execute_BoneMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_BONEMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.boneMatrix + (op & 0x7F));
	const int end = 12 * 8 - (op & 0x7F);
	int i = 0;

	while ((src[i] >> 24) == GE_CMD_BONEMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			dst[i] = newVal;
		}
		if (++i >= end) {
			break;
		}
	}

	const int numPlusCount = (op & 0x7F) + i;

	const int count = i;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | ((op + count) & 0x7F);

	u32 toDirty = 0;
	for (int num = op & 0x7F; num < numPlusCount; num += 12) {
		toDirty |= STATE_BONE0 << (num / 12);
	}
	dirty_ |= toDirty;

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_BoneMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.boneMatrixNumber & 0x7F;
	u32 newVal = op << 8;
	u32 toDirty = 0;
	if (num < 96 && newVal != ((const u32 *)gstate.boneMatrix)[num]) {
		toDirty |= STATE_BONE0 << (num / 12);
		((u32 *)gstate.boneMatrix)[num] = newVal;
	}
	num++;
	dirty_ |= toDirty;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x7F);
}

void HighGpuFrontend::FastLoadBoneMatrix(u32 target) {
	const int num = gstate.boneMatrixNumber & 0x7F;
	const int mtxNum = num / 12;
	uint32_t stateToDirty = STATE_BONE0 << mtxNum;
	if ((num - 12 * mtxNum) != 0) {
		stateToDirty |= STATE_BONE0 << ((mtxNum + 1) & 7);
	}
	gstate.FastLoadBoneMatrix(target);
	dirty_ |= stateToDirty;
}

void HighGpuFrontend::UpdateStats() {
	backend_->UpdateStats();
}

void HighGpuFrontend::InvalidateCache(u32 addr, int size, GPUInvalidationType type) {
	GPUEvent ev(GPU_EVENT_INVALIDATE_CACHE);
	ev.invalidate_cache.addr = addr;
	ev.invalidate_cache.size = size;
	ev.invalidate_cache.type = type;
	ScheduleEvent(ev);
}

bool HighGpuFrontend::PerformMemoryCopy(u32 dest, u32 src, int size) {
	// Track stray copies of a framebuffer in RAM. MotoGP does this.
	// if (framebufferManager_.MayIntersectFramebuffer(src) || framebufferManager_.MayIntersectFramebuffer(dest)) {
		GPUEvent ev(GPU_EVENT_FB_MEMCPY);
		ev.fb_memcpy.dst = dest;
		ev.fb_memcpy.src = src;
		ev.fb_memcpy.size = size;
		if (IsOnSeparateCPUThread()) {
			ScheduleEvent(ev);

			// This is a memcpy, so we need to wait for it to complete.
			SyncThread();
		} else {
			backend_->ProcessEvent(ev);
		}
		//return true;
	// }

	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	// return false;
	return true;
}

bool HighGpuFrontend::PerformMemorySet(u32 dest, u8 v, int size) {
	/*
	// This may indicate a memset, usually to 0, of a framebuffer.
	if (framebufferManager_.MayIntersectFramebuffer(dest)) {
		Memory::Memset(dest, v, size);

		GPUEvent ev(GPU_EVENT_FB_MEMSET);
		ev.fb_memset.dst = dest;
		ev.fb_memset.v = v;
		ev.fb_memset.size = size;
		if (IsOnSeparateCPUThread()) {
			ScheduleEvent(ev);

			// We don't need to wait for the framebuffer to be updated.
		} else {
			backend_->ProcessEvent(ev);
		}
		return true;
	}

	// Or perhaps a texture, let's invalidate.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	*/
	return false;
}

bool HighGpuFrontend::PerformMemoryDownload(u32 dest, int size) {
	// Cheat a bit to force a download of the framebuffer.
	// VRAM + 0x00400000 is simply a VRAM mirror.
	if (Memory::IsVRAMAddress(dest)) {
		return PerformMemoryCopy(dest ^ 0x00400000, dest, size);
	}
	return false;
}

bool HighGpuFrontend::PerformMemoryUpload(u32 dest, int size) {
	// Cheat a bit to force an upload of the framebuffer.
	// VRAM + 0x00400000 is simply a VRAM mirror.
	if (Memory::IsVRAMAddress(dest)) {
		return PerformMemoryCopy(dest, dest ^ 0x00400000, size);
	}
	return false;
}

bool HighGpuFrontend::PerformStencilUpload(u32 dest, int size) {
	GPUEvent ev(GPU_EVENT_FB_STENCIL_UPLOAD);
	ev.fb_stencil_upload.dst = dest;
	ev.fb_stencil_upload.size = size;
	if (IsOnSeparateCPUThread()) {
		ScheduleEvent(ev);
	} else {
		backend_->ProcessEvent(ev);
	}
	return true;
}

void HighGpuFrontend::ClearCacheNextFrame() {
	// textureCache_.ClearNextFrame();
}

void HighGpuFrontend::Resized() {
	resized_ = true;
	// framebufferManager_.Resized();
}

void HighGpuFrontend::ClearShaderCache() {
	// shaderManager_->ClearCache(true);
}

void HighGpuFrontend::CleanupBeforeUI() {
	// Clear any enabled vertex arrays.
	// shaderManager_->DirtyLastShader();
	// glstate.arrayBuffer.bind(0);
	// glstate.elementArrayBuffer.bind(0);
}

std::vector<FramebufferInfo> HighGpuFrontend::GetFramebufferList() {
	return std::vector<FramebufferInfo>();  //framebufferManager_.GetFramebufferList();
}

void HighGpuFrontend::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	backend_->DoState(p);
}

bool HighGpuFrontend::GetCurrentFramebuffer(GPUDebugBuffer &buffer) {
	return false; //framebufferManager_.GetCurrentFramebuffer(buffer);
}

bool HighGpuFrontend::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	return false; //framebufferManager_.GetCurrentDepthbuffer(buffer);
}

bool HighGpuFrontend::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	return false; //framebufferManager_.GetCurrentStencilbuffer(buffer);
}

bool HighGpuFrontend::GetCurrentTexture(GPUDebugBuffer &buffer, int level) {
	return false;
	// TODO
}

bool HighGpuFrontend::GetDisplayFramebuffer(GPUDebugBuffer &buffer) {
	return false; // FramebufferManager::GetDisplayFramebuffer(buffer);
}

bool HighGpuFrontend::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	return false;  // transformDraw_.GetCurrentSimpleVertices(count, vertices, indices);
}

bool HighGpuFrontend::DescribeCodePtr(const u8 *ptr, std::string &name) {
	return false;
}

void HighGpuFrontend::FlushCommandPacket() {
	// TODO: Might need a queue of these packets for parallelism later.
	backend_->Execute(cmdPacket_);

	CommandPacketReset(cmdPacket_, &dummyDraw_);
	// Wait for the GPU thread to be done.
	arena_.Clear();
	dirty_ = STATE_ALL;
}

// This is probably a pretty good point to flush the command packet. Needs more investigation.
void HighGpuFrontend::SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) {
	GPUCommon::SyncEnd(waitType, listid, wokeThreads);
	FlushCommandPacket();
}

}  // namespace HighGpu
