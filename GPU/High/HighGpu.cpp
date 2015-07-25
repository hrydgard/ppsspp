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

#include "GPU/High/Command.h"
#include "GPU/High/HighGpu.h"

namespace HighGpu {

enum {
	FLAG_EXECUTE = 4,
	FLAG_READS_PC = 16,
	FLAG_WRITES_PC = 32,
	FLAG_DIRTYONCHANGE = 64,
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
	{GE_CMD_TEXMAPMODE, 0, STATE_TEXTURE},
	{GE_CMD_TEXSCALEU, 0, STATE_TEXTURE},
	{GE_CMD_TEXSCALEV, 0, STATE_TEXTURE},
	{GE_CMD_TEXOFFSETU, 0, STATE_TEXTURE},
	{GE_CMD_TEXOFFSETV, 0, STATE_TEXTURE},

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
	{GE_CMD_TEXFORMAT, 0, STATE_TEXTURE},
	{GE_CMD_TEXLEVEL, 0, STATE_TEXTURE},
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

	// These are merged into clutload command directly, no state object needed.
	{GE_CMD_CLUTADDR, 0},
	{GE_CMD_CLUTADDRUPPER, 0},
	{GE_CMD_CLUTFORMAT, 0},

	// These affect the fragment shader
	{GE_CMD_CLEARMODE,        0, STATE_FRAGMENT},
	{GE_CMD_TEXTUREMAPENABLE, 0, STATE_TEXTURE},
	{GE_CMD_FOGENABLE,        0, STATE_FRAGMENT},
	{GE_CMD_TEXMODE,          0, STATE_TEXTURE},
	{GE_CMD_TEXSHADELS,       0, STATE_TEXTURE},
	{GE_CMD_SHADEMODE,        0, STATE_FRAGMENT},
	{GE_CMD_TEXFUNC,          0, STATE_FRAGMENT},
	{GE_CMD_COLORTEST,        0, STATE_BLEND},
	{GE_CMD_ALPHATESTENABLE,  0, STATE_BLEND},
	{GE_CMD_COLORTESTENABLE,  0, STATE_BLEND},
	{GE_CMD_COLORTESTMASK,    0, STATE_BLEND},

	// These change the vertex shader
	{GE_CMD_REVERSENORMAL, 0, STATE_LIGHTGLOBAL},
	{GE_CMD_LIGHTINGENABLE, 0, STATE_LIGHTGLOBAL},
	{GE_CMD_LIGHTENABLE0, 0, STATE_LIGHT0},
	{GE_CMD_LIGHTENABLE1, 0, STATE_LIGHT1},
	{GE_CMD_LIGHTENABLE2, 0, STATE_LIGHT2},
	{GE_CMD_LIGHTENABLE3, 0, STATE_LIGHT3},
	{GE_CMD_LIGHTTYPE0, 0, STATE_LIGHT0},
	{GE_CMD_LIGHTTYPE1, 0, STATE_LIGHT1},
	{GE_CMD_LIGHTTYPE2, 0, STATE_LIGHT2},
	{GE_CMD_LIGHTTYPE3, 0, STATE_LIGHT3},
	{GE_CMD_MATERIALUPDATE, 0, STATE_LIGHTGLOBAL},

	// This changes both shaders so need flushing.
	{GE_CMD_LIGHTMODE, 0, STATE_LIGHTGLOBAL},
	{GE_CMD_TEXFILTER, 0, STATE_TEXTURE},
	{GE_CMD_TEXWRAP, 0, STATE_TEXTURE},

	// Uniform changes
	{GE_CMD_ALPHATEST, 0, STATE_BLEND},
	{GE_CMD_COLORREF, 0, STATE_BLEND},
	{GE_CMD_TEXENVCOLOR, 0, STATE_FRAGMENT},

	// Simple render state changes. Handled in StateMapping.cpp.
	{GE_CMD_OFFSETX, 0},
	{GE_CMD_OFFSETY, 0},
	{GE_CMD_CULL, 0},
	{GE_CMD_CULLFACEENABLE, 0},
	{GE_CMD_DITHERENABLE, 0},
	{GE_CMD_STENCILOP, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_STENCILTEST, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_STENCILTESTENABLE, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_ALPHABLENDENABLE, 0, STATE_BLEND},
	{GE_CMD_BLENDMODE, 0, STATE_BLEND},
	{GE_CMD_BLENDFIXEDA, 0, STATE_BLEND},
	{GE_CMD_BLENDFIXEDB, 0, STATE_BLEND},
	{GE_CMD_MASKRGB, 0},  // TODO
	{GE_CMD_MASKALPHA, 0},  // TODO
	{GE_CMD_ZTEST, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_ZTESTENABLE, 0, STATE_DEPTHSTENCIL},
	{GE_CMD_ZWRITEDISABLE, 0, STATE_DEPTHSTENCIL},
#ifndef USING_GLES2
	{GE_CMD_LOGICOP, 0, STATE_BLEND},
	{GE_CMD_LOGICOPENABLE, 0, STATE_BLEND},
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
	{GE_CMD_SCISSOR1, 0, STATE_FRAGMENT},
	{GE_CMD_SCISSOR2, 0, STATE_FRAGMENT},

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
	{GE_CMD_LX2, 0, STATE_LIGHT3},
	{GE_CMD_LY2, 0, STATE_LIGHT3},
	{GE_CMD_LZ2, 0, STATE_LIGHT3},

	{GE_CMD_LDX0, 0, STATE_LIGHT0},
	{GE_CMD_LDY0, 0, STATE_LIGHT0},
	{GE_CMD_LDZ0, 0, STATE_LIGHT0},
	{GE_CMD_LDX1, 0, STATE_LIGHT1},
	{GE_CMD_LDY1, 0, STATE_LIGHT1},
	{GE_CMD_LDZ1, 0, STATE_LIGHT1},
	{GE_CMD_LDX2, 0, STATE_LIGHT2},
	{GE_CMD_LDY2, 0, STATE_LIGHT2},
	{GE_CMD_LDZ2, 0, STATE_LIGHT2},
	{GE_CMD_LDX2, 0, STATE_LIGHT3},
	{GE_CMD_LDY2, 0, STATE_LIGHT3},
	{GE_CMD_LDZ2, 0, STATE_LIGHT3},

	{GE_CMD_LKA0, 0, STATE_LIGHT0},
	{GE_CMD_LKB0, 0, STATE_LIGHT0},
	{GE_CMD_LKC0, 0, STATE_LIGHT0},
	{GE_CMD_LKA1, 0, STATE_LIGHT1},
	{GE_CMD_LKB1, 0, STATE_LIGHT1},
	{GE_CMD_LKC1, 0, STATE_LIGHT1},
	{GE_CMD_LKA2, 0, STATE_LIGHT2},
	{GE_CMD_LKB2, 0, STATE_LIGHT2},
	{GE_CMD_LKC2, 0, STATE_LIGHT2},
	{GE_CMD_LKA2, 0, STATE_LIGHT3},
	{GE_CMD_LKB2, 0, STATE_LIGHT3},
	{GE_CMD_LKC2, 0, STATE_LIGHT3},

	{GE_CMD_LKS0, 0, STATE_LIGHT0},
	{GE_CMD_LKS1, 0, STATE_LIGHT1},
	{GE_CMD_LKS2, 0, STATE_LIGHT2},
	{GE_CMD_LKS2, 0, STATE_LIGHT3},

	{GE_CMD_LKO0, 0, STATE_LIGHT0},
	{GE_CMD_LKO1, 0, STATE_LIGHT1},
	{GE_CMD_LKO2, 0, STATE_LIGHT2},
	{GE_CMD_LKO2, 0, STATE_LIGHT3},

	{GE_CMD_LAC0, 0, STATE_LIGHT0},
	{GE_CMD_LDC0, 0, STATE_LIGHT0},
	{GE_CMD_LSC0, 0, STATE_LIGHT0},
	{GE_CMD_LAC1, 0, STATE_LIGHT1},
	{GE_CMD_LDC1, 0, STATE_LIGHT1},
	{GE_CMD_LSC1, 0, STATE_LIGHT1},
	{GE_CMD_LAC2, 0, STATE_LIGHT2},
	{GE_CMD_LDC2, 0, STATE_LIGHT2},
	{GE_CMD_LSC2, 0, STATE_LIGHT2},
	{GE_CMD_LAC2, 0, STATE_LIGHT3},
	{GE_CMD_LDC2, 0, STATE_LIGHT3},
	{GE_CMD_LSC2, 0, STATE_LIGHT3},

	// Ignored commands
	{GE_CMD_CLIPENABLE, 0},
	{GE_CMD_TEXFLUSH, 0},
	{GE_CMD_TEXLODSLOPE, 0},
	{GE_CMD_TEXSYNC, 0},

	// These are just nop or part of other later commands.
	{GE_CMD_NOP, 0},
	{GE_CMD_BASE, 0},
	{GE_CMD_TRANSFERSRC, 0},
	{GE_CMD_TRANSFERSRCW, 0},
	{GE_CMD_TRANSFERDST, 0},
	{GE_CMD_TRANSFERDSTW, 0},
	{GE_CMD_TRANSFERSRCPOS, 0},
	{GE_CMD_TRANSFERDSTPOS, 0},
	{GE_CMD_TRANSFERSIZE, 0},

	// From Common. No flushing but definitely need execute.
	{GE_CMD_OFFSETADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_OffsetAddr},
	{GE_CMD_ORIGIN, FLAG_EXECUTE | FLAG_READS_PC, 0, &GPUCommon::Execute_Origin},  // Really?
	{GE_CMD_PRIM, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Prim},
	{GE_CMD_JUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Jump},
	{GE_CMD_CALL, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Call},
	{GE_CMD_RET, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Ret},
	{GE_CMD_END, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_End},  // Flush?
	{GE_CMD_VADDR, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Vaddr},
	{GE_CMD_IADDR, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_Iaddr},
	{GE_CMD_BJUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_BJump},  // EXECUTE
	{GE_CMD_BOUNDINGBOX, FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_BoundingBox},

	{GE_CMD_VERTEXTYPE},

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
	{GE_CMD_WORLDMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &HighGpuFrontend::Execute_WorldMtxNum},
	{GE_CMD_WORLDMATRIXDATA,   FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_WorldMtxData},
	{GE_CMD_VIEWMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &HighGpuFrontend::Execute_ViewMtxNum},
	{GE_CMD_VIEWMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_ViewMtxData},
	{GE_CMD_PROJMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &HighGpuFrontend::Execute_ProjMtxNum},
	{GE_CMD_PROJMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_ProjMtxData},
	{GE_CMD_TGENMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &HighGpuFrontend::Execute_TgenMtxNum},
	{GE_CMD_TGENMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_TgenMtxData},
	{GE_CMD_BONEMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &HighGpuFrontend::Execute_BoneMtxNum},
	{GE_CMD_BONEMATRIXDATA,    FLAG_EXECUTE, 0, &HighGpuFrontend::Execute_BoneMtxData},

	// Vertex Screen/Texture/Color
	// These are unimplemented and do not seem to be used.
	{GE_CMD_VSCX, FLAG_EXECUTE},
	{GE_CMD_VSCY, FLAG_EXECUTE},
	{GE_CMD_VSCZ, FLAG_EXECUTE},
	{GE_CMD_VTCS, FLAG_EXECUTE},
	{GE_CMD_VTCT, FLAG_EXECUTE},
	{GE_CMD_VTCQ, FLAG_EXECUTE},
	{GE_CMD_VCV, FLAG_EXECUTE},
	{GE_CMD_VAP, FLAG_EXECUTE},
	{GE_CMD_VFC, FLAG_EXECUTE},
	{GE_CMD_VSCV, FLAG_EXECUTE},

	// "Missing" commands (gaps in the sequence)
	{GE_CMD_UNKNOWN_03, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_0D, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_11, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_29, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_34, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_35, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_39, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_4E, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_4F, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_52, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_59, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_5A, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_B6, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_B7, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_D1, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_ED, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_EF, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FA, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FB, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FC, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FD, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FE, FLAG_EXECUTE},
	// Appears to be debugging related or something?  Hit a lot in GoW.
	{GE_CMD_UNKNOWN_FF, 0},
};

HighGpuFrontend::CommandInfo HighGpuFrontend::cmdInfo_[256];

HighGpuFrontend::HighGpuFrontend(HighGpuBackend *backend)
: resized_(false), backend_(backend) {
	UpdateVsyncInterval(true);

	shaderManager_ = new ShaderManager();
	transformDraw_.SetShaderManager(shaderManager_);
	transformDraw_.SetTextureCache(&textureCache_);
	transformDraw_.SetFramebufferManager(&framebufferManager_);
	transformDraw_.SetFragmentTestCache(&fragmentTestCache_);
	framebufferManager_.Init();
	framebufferManager_.SetTextureCache(&textureCache_);
	framebufferManager_.SetShaderManager(shaderManager_);
	framebufferManager_.SetTransformDrawEngine(&transformDraw_);
	textureCache_.SetFramebufferManager(&framebufferManager_);
	textureCache_.SetDepalShaderCache(&depalShaderCache_);
	textureCache_.SetShaderManager(shaderManager_);
	fragmentTestCache_.SetTextureCache(&textureCache_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

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

	BuildReportingInfo();
	// Update again after init to be sure of any silly driver problems.
	UpdateVsyncInterval(true);

	// Some of our defaults are different from hw defaults, let's assert them.
	// We restore each frame anyway, but here is convenient for tests.
	glstate.Restore();
}

HighGpuFrontend::~HighGpuFrontend() {
	framebufferManager_.DestroyAllFBOs();
	shaderManager_->ClearCache(true);
	depalShaderCache_.Clear();
	fragmentTestCache_.Clear();
	delete shaderManager_;
	shaderManager_ = nullptr;
	glstate.SetVSyncInterval(0);
}

void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
	backend_->GetReportingInfo(primaryInfo, fullInfo);
}

// Should only be executed on the GPU thread.
void HighGpuFrontend::DeviceLost() {
	ILOG("HighGpuFrontend: DeviceLost");
	backend_->DeviceLost();

	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	// TransformDraw has registered as a GfxResourceHolder.
	shaderManager_->ClearCache(false);
	textureCache_.Clear(false);
	fragmentTestCache_.Clear(false);
	depalShaderCache_.Clear();
	framebufferManager_.DeviceLost();

	UpdateVsyncInterval(true);
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

inline void HighGpuFrontend::UpdateVsyncInterval(bool force) {
#ifdef _WIN32
	int desiredVSyncInterval = g_Config.bVSync ? 1 : 0;
	if (PSP_CoreParameter().unthrottle) {
		desiredVSyncInterval = 0;
	}
	if (PSP_CoreParameter().fpsLimit == 1) {
		// For an alternative speed that is a clean factor of 60, the user probably still wants vsync.
		if (g_Config.iFpsLimit == 0 || (g_Config.iFpsLimit != 15 && g_Config.iFpsLimit != 30 && g_Config.iFpsLimit != 60)) {
			desiredVSyncInterval = 0;
		}
	}

	if (desiredVSyncInterval != lastVsync_ || force) {
		// Disabled EXT_swap_control_tear for now, it never seems to settle at the correct timing
		// so it just keeps tearing. Not what I hoped for...
		//if (gl_extensions.EXT_swap_control_tear) {
		//	// See http://developer.download.nvidia.com/opengl/specs/WGL_EXT_swap_control_tear.txt
		//	glstate.SetVSyncInterval(-desiredVSyncInterval);
		//} else {
			glstate.SetVSyncInterval(desiredVSyncInterval);
		//}
		lastVsync_ = desiredVSyncInterval;
	}
#endif
}

void HighGpuFrontend::BeginFrameInternal() {
	if (resized_) {
		transformDraw_.Resized();
	}
	UpdateVsyncInterval(resized_);
	resized_ = false;

	textureCache_.StartFrame();
	transformDraw_.DecimateTrackedVertexArrays();
	depalShaderCache_.Decimate();
	fragmentTestCache_.Decimate();

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

void HighGpuFrontend::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	host->GPUNotifyDisplay(framebuf, stride, format);
	framebufferManager_.SetDisplayFramebuffer(framebuf, stride, format);
}

bool HighGpuFrontend::FramebufferDirty() {
	if (ThreadEnabled()) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	VirtualFramebuffer *vfb = framebufferManager_.GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->dirtyAfterDisplay;
		vfb->dirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

bool HighGpuFrontend::FramebufferReallyDirty() {
	if (ThreadEnabled()) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	VirtualFramebuffer *vfb = framebufferManager_.GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->reallyDirtyAfterDisplay;
		vfb->reallyDirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

void HighGpuFrontend::CopyDisplayToOutput() {
	ScheduleEvent(GPU_EVENT_COPY_DISPLAY_TO_OUTPUT);
}

// Maybe should write this in ASM...
void HighGpuFrontend::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("gpuloop");
	const CommandInfo *cmdInfo = cmdInfo_;
	int dc = downcount;
	for (; dc > 0; --dc) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer
		const u32 op = *(const u32 *)(Memory::base + list.pc);
		const u32 cmd = op >> 24;
		const CommandInfo info = cmdInfo[cmd];
		const u8 cmdFlags = info.flags;      // If we stashed the cmdFlags in the top bits of the cmdmem, we could get away with one table lookup instead of two
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

void HighGpuFrontend::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	transformDraw_.FinishDeferred();
}

void HighGpuFrontend::ProcessEvent(GPUEvent ev) {
	if (!backend->ProcessEvent(ev)) {
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
	// This drives all drawing. All other state we just buffer up, then we apply it only
	// when it's time to draw. As most PSP games set state redundantly ALL THE TIME, this is a huge optimization.

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

	// This also makes skipping drawing very effective.
	framebufferManager_.SetRenderFrameBuffer();
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB))	{
		transformDraw_.SetupVertexDecoder(gstate.vertType);
		// Rough estimate, not sure what's correct.
		int vertexCost = transformDraw_.EstimatePerVertexCost();
		cyclesExecuted += vertexCost * count;
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *inds = 0;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

#ifndef MOBILE_DEVICE
	if (prim > GE_PRIM_RECTANGLES) {
		ERROR_LOG_REPORT_ONCE(reportPrim, G3D, "Unexpected prim type: %d", prim);
	}
#endif

	int bytesRead = 0;
	transformDraw_.SubmitPrim(verts, inds, prim, count, gstate.vertType, &bytesRead);

	int vertexCost = transformDraw_.EstimatePerVertexCost();
	gpuStats.vertexGPUCycles += vertexCost * count;
	cyclesExecuted += vertexCost * count;

	// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	if (inds) {
		int indexSize = 1;
		if ((gstate.vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT)
			indexSize = 2;
		gstate_c.indexAddr += count * indexSize;
	} else {
		gstate_c.vertexAddr += bytesRead;
	}
}

void HighGpuFrontend::Execute_Bezier(u32 op, u32 diff) {
	// TODO
}

void HighGpuFrontend::Execute_Spline(u32 op, u32 diff) {
	// TODO
}

void HighGpuFrontend::Execute_BoundingBox(u32 op, u32 diff) {
	// TODO: Need to extract TestBoundingBox from transformDraw
	/*
	// Just resetting, nothing to bound.
	const u32 data = op & 0x00FFFFFF;
	if (data == 0) {
		// TODO: Should this set the bboxResult?  Let's set it true for now.
		currentList->bboxResult = true;
		return;
	}
	if (((data & 7) == 0) && data <= 64) {  // Sanity check
		void *control_points = Memory::GetPointer(gstate_c.vertexAddr);
		if (gstate.vertType & GE_VTYPE_IDX_MASK) {
			ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Indexed bounding box data not supported.");
			// Data seems invalid. Let's assume the box test passed.
			currentList->bboxResult = true;
			return;
		}

		// Test if the bounding box is within the drawing region.
		currentList->bboxResult = transformDraw_.TestBoundingBox(control_points, data, gstate.vertType);
	} else {
		ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Bad bounding box data: %06x", data);
		// Data seems invalid. Let's assume the box test passed.
		currentList->bboxResult = true;
	}
	*/
}

void HighGpuFrontend::Execute_LoadClut(u32 op, u32 diff) {
	// This could be used to "dirty" textures with clut.
	CommandSubmitLoadClut(cmdPacket_,
}

void HighGpuFrontend::Execute_ClutFormat(u32 op, u32 diff) {
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	// This could be used to "dirty" textures with clut.
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
			Flush();
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
		Flush();
		((u32 *)gstate.worldMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_WORLDMATRIX);
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

	while ((src[i] >> 24) == GE_CMD_VIEWMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
			shaderManager_->DirtyUniform(DIRTY_VIEWMATRIX);
		}
		if (++i >= end) {
			break;
		}
	}

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
		Flush();
		((u32 *)gstate.viewMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_VIEWMATRIX);
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

	while ((src[i] >> 24) == GE_CMD_PROJMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_ProjMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.projmtxnum & 0xF;
	u32 newVal = op << 8;
	if (newVal != ((const u32 *)gstate.projMatrix)[num]) {
		Flush();
		((u32 *)gstate.projMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
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

	while ((src[i] >> 24) == GE_CMD_TGENMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
			shaderManager_->DirtyUniform(DIRTY_TEXMATRIX);
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_TgenMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.texmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.tgenMatrix)[num]) {
		Flush();
		((u32 *)gstate.tgenMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_TEXMATRIX);
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

	// If we can't use software skinning, we have to flush and dirty.
	if (!g_Config.bSoftwareSkinning || (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
		while ((src[i] >> 24) == GE_CMD_BONEMATRIXDATA) {
			const u32 newVal = src[i] << 8;
			if (dst[i] != newVal) {
				Flush();
				dst[i] = newVal;
			}
			if (++i >= end) {
				break;
			}
		}

		const int numPlusCount = (op & 0x7F) + i;
		for (int num = op & 0x7F; num < numPlusCount; num += 12) {
			shaderManager_->DirtyUniform(DIRTY_BONEMATRIX0 << (num / 12));
		}
	} else {
		while ((src[i] >> 24) == GE_CMD_BONEMATRIXDATA) {
			dst[i] = src[i] << 8;
			if (++i >= end) {
				break;
			}
		}

		const int numPlusCount = (op & 0x7F) + i;
		for (int num = op & 0x7F; num < numPlusCount; num += 12) {
			gstate_c.deferredVertTypeDirty |= DIRTY_BONEMATRIX0 << (num / 12);
		}
	}

	const int count = i;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | ((op + count) & 0x7F);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void HighGpuFrontend::Execute_BoneMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.boneMatrixNumber & 0x7F;
	u32 newVal = op << 8;
	if (num < 96 && newVal != ((const u32 *)gstate.boneMatrix)[num]) {
		// Bone matrices should NOT flush when software skinning is enabled!
		if (!g_Config.bSoftwareSkinning || (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
			Flush();
			shaderManager_->DirtyUniform(DIRTY_BONEMATRIX0 << (num / 12));
		} else {
			gstate_c.deferredVertTypeDirty |= DIRTY_BONEMATRIX0 << (num / 12);
		}
		((u32 *)gstate.boneMatrix)[num] = newVal;
	}
	num++;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x7F);
}

void HighGpuFrontend::Execute_BlockTransferStart(u32 op, u32 diff) {
	// TODO: Here we should check if the transfer overlaps a framebuffer or any textures,
	// and take appropriate action. This is a block transfer between RAM and VRAM, or vice versa.
	// Can we skip this on SkipDraw?
	DoBlockTransfer();

	// Fixes Gran Turismo's funky text issue, since it overwrites the current texture.
	gstate_c.textureChanged = TEXCHANGE_UPDATED;
}

void HighGpuFrontend::FastLoadBoneMatrix(u32 target) {
	const int num = gstate.boneMatrixNumber & 0x7F;
	const int mtxNum = num / 12;
	uint32_t uniformsToDirty = DIRTY_BONEMATRIX0 << mtxNum;
	if ((num - 12 * mtxNum) != 0) {
		uniformsToDirty |= DIRTY_BONEMATRIX0 << ((mtxNum + 1) & 7);
	}

	if (!g_Config.bSoftwareSkinning || (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
		Flush();
		shaderManager_->DirtyUniform(uniformsToDirty);
	} else {
		gstate_c.deferredVertTypeDirty |= uniformsToDirty;
	}
	gstate.FastLoadBoneMatrix(target);
}

void HighGpuFrontend::UpdateStats() {
	gpuStats.numVertexShaders = shaderManager_->NumVertexShaders();
	gpuStats.numFragmentShaders = shaderManager_->NumFragmentShaders();
	gpuStats.numShaders = shaderManager_->NumPrograms();
	gpuStats.numTextures = (int)textureCache_.NumLoadedTextures();
	gpuStats.numFBOs = (int)framebufferManager_.NumVFBs();
}

void HighGpuFrontend::DoBlockTransfer() {
	// TODO: This is used a lot to copy data around between render targets and textures,
	// and also to quickly load textures from RAM to VRAM. So we should do checks like the following:
	//  * Does dstBasePtr point to an existing texture? If so maybe reload it immediately.
	//
	//  * Does srcBasePtr point to a render target, and dstBasePtr to a texture? If so
	//    either copy between rt and texture or reassign the texture to point to the render target
	//
	// etc....

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

	if (!Memory::IsValidAddress(srcBasePtr)) {
		ERROR_LOG_REPORT(G3D, "BlockTransfer: Bad source transfer address %08x!", srcBasePtr);
		return;
	}

	if (!Memory::IsValidAddress(dstBasePtr)) {
		ERROR_LOG_REPORT(G3D, "BlockTransfer: Bad destination transfer address %08x!", dstBasePtr);
		return;
	}
	
	// Check that the last address of both source and dest are valid addresses

	u32 srcLastAddr = srcBasePtr + ((height - 1 + srcY) * srcStride + (srcX + width - 1)) * bpp;
	u32 dstLastAddr = dstBasePtr + ((height - 1 + dstY) * dstStride + (dstX + width - 1)) * bpp;

	if (!Memory::IsValidAddress(srcLastAddr)) {
		ERROR_LOG_REPORT(G3D, "Bottom-right corner of source of block transfer is at an invalid address: %08x", srcLastAddr);
		return;
	}
	if (!Memory::IsValidAddress(dstLastAddr)) {
		ERROR_LOG_REPORT(G3D, "Bottom-right corner of destination of block transfer is at an invalid address: %08x", srcLastAddr);
		return;
	}

	// Tell the framebuffer manager to take action if possible. If it does the entire thing, let's just return.
	if (!framebufferManager_.NotifyBlockTransferBefore(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp)) {
		// Do the copy! (Hm, if we detect a drawn video frame (see below) then we could maybe skip this?)
		// Can use GetPointerUnchecked because we checked the addresses above. We could also avoid them
		// entirely by walking a couple of pointers...
		if (srcStride == dstStride && (u32)width == srcStride) {
			// Common case in God of War, let's do it all in one chunk.
			u32 srcLineStartAddr = srcBasePtr + (srcY * srcStride + srcX) * bpp;
			u32 dstLineStartAddr = dstBasePtr + (dstY * dstStride + dstX) * bpp;
			const u8 *src = Memory::GetPointerUnchecked(srcLineStartAddr);
			u8 *dst = Memory::GetPointerUnchecked(dstLineStartAddr);
			memcpy(dst, src, width * height * bpp);
		} else {
			for (int y = 0; y < height; y++) {
				u32 srcLineStartAddr = srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp;
				u32 dstLineStartAddr = dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp;

				const u8 *src = Memory::GetPointerUnchecked(srcLineStartAddr);
				u8 *dst = Memory::GetPointerUnchecked(dstLineStartAddr);
				memcpy(dst, src, width * bpp);
			}
		}

		textureCache_.Invalidate(dstBasePtr + (dstY * dstStride + dstX) * bpp, height * dstStride * bpp, GPU_INVALIDATE_HINT);
		framebufferManager_.NotifyBlockTransferAfter(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp);
	}

#ifndef MOBILE_DEVICE
	CBreakPoints::ExecMemCheck(srcBasePtr + (srcY * srcStride + srcX) * bpp, false, height * srcStride * bpp, currentMIPS->pc);
	CBreakPoints::ExecMemCheck(dstBasePtr + (srcY * dstStride + srcX) * bpp, true, height * dstStride * bpp, currentMIPS->pc);
#endif

	// TODO: Correct timing appears to be 1.9, but erring a bit low since some of our other timing is inaccurate.
	cyclesExecuted += ((height * width * bpp) * 16) / 10;
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
	if (framebufferManager_.MayIntersectFramebuffer(src) || framebufferManager_.MayIntersectFramebuffer(dest)) {
		if (IsOnSeparateCPUThread()) {
			GPUEvent ev(GPU_EVENT_FB_MEMCPY);
			ev.fb_memcpy.dst = dest;
			ev.fb_memcpy.src = src;
			ev.fb_memcpy.size = size;
			ScheduleEvent(ev);

			// This is a memcpy, so we need to wait for it to complete.
			SyncThread();
		} else {
			PerformMemoryCopyInternal(dest, src, size);
		}
		return true;
	}

	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	return false;
}

bool HighGpuFrontend::PerformMemorySet(u32 dest, u8 v, int size) {
	// This may indicate a memset, usually to 0, of a framebuffer.
	if (framebufferManager_.MayIntersectFramebuffer(dest)) {
		Memory::Memset(dest, v, size);

		if (IsOnSeparateCPUThread()) {
			GPUEvent ev(GPU_EVENT_FB_MEMSET);
			ev.fb_memset.dst = dest;
			ev.fb_memset.v = v;
			ev.fb_memset.size = size;
			ScheduleEvent(ev);

			// We don't need to wait for the framebuffer to be updated.
		} else {
			PerformMemorySetInternal(dest, v, size);
		}
		return true;
	}

	// Or perhaps a texture, let's invalidate.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
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
	if (framebufferManager_.MayIntersectFramebuffer(dest)) {
		if (IsOnSeparateCPUThread()) {
			GPUEvent ev(GPU_EVENT_FB_STENCIL_UPLOAD);
			ev.fb_stencil_upload.dst = dest;
			ev.fb_stencil_upload.size = size;
			ScheduleEvent(ev);
		} else {
			PerformStencilUploadInternal(dest, size);
		}
		return true;
	}
	return false;
}

void HighGpuFrontend::ClearCacheNextFrame() {
	textureCache_.ClearNextFrame();
}

void HighGpuFrontend::Resized() {
	resized_ = true;
	framebufferManager_.Resized();
}

void HighGpuFrontend::ClearShaderCache() {
	shaderManager_->ClearCache(true);
}

void HighGpuFrontend::CleanupBeforeUI() {
	// Clear any enabled vertex arrays.
	shaderManager_->DirtyLastShader();
	glstate.arrayBuffer.bind(0);
	glstate.elementArrayBuffer.bind(0);
}

std::vector<FramebufferInfo> HighGpuFrontend::GetFramebufferList() {
	return framebufferManager_.GetFramebufferList();
}

void HighGpuFrontend::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	// In Freeze-Frame mode, we don't want to do any of this.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCache_.Clear(true);
		depalShaderCache_.Clear();
		transformDraw_.ClearTrackedVertexArrays();

		gstate_c.textureChanged = TEXCHANGE_UPDATED;
		framebufferManager_.DestroyAllFBOs();
		shaderManager_->ClearCache(true);
	}
}

bool HighGpuFrontend::GetCurrentFramebuffer(GPUDebugBuffer &buffer) {
	return framebufferManager_.GetCurrentFramebuffer(buffer);
}

bool HighGpuFrontend::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	return framebufferManager_.GetCurrentDepthbuffer(buffer);
}

bool HighGpuFrontend::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	return framebufferManager_.GetCurrentStencilbuffer(buffer);
}

bool HighGpuFrontend::GetCurrentTexture(GPUDebugBuffer &buffer, int level) {
	if (!gstate.isTextureMapEnabled()) {
		return false;
	}

#ifndef USING_GLES2
	GPUgstate saved;
	if (level != 0) {
		saved = gstate;

		// The way we set textures is a bit complex.  Let's just override level 0.
		gstate.texsize[0] = gstate.texsize[level];
		gstate.texaddr[0] = gstate.texaddr[level];
		gstate.texbufwidth[0] = gstate.texbufwidth[level];
	}

	textureCache_.SetTexture(true);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

	if (level != 0) {
		gstate = saved;
	}

	buffer.Allocate(w, h, GE_FORMAT_8888, gstate_c.flipTexture);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.GetData());

	return true;
#else
	return false;
#endif
}

bool HighGpuFrontend::GetDisplayFramebuffer(GPUDebugBuffer &buffer) {
	return FramebufferManager::GetDisplayFramebuffer(buffer);
}

bool HighGpuFrontend::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	return transformDraw_.GetCurrentSimpleVertices(count, vertices, indices);
}

bool HighGpuFrontend::DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (transformDraw_.IsCodePtrVertexDecoder(ptr)) {
		name = "VertexDecoderJit";
		return true;
	}
	return false;
}

}  // namespace HighGpu
