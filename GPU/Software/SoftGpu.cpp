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

#include <set>

#include "Common/System/Display.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/GraphicsContext.h"
#include "Common/LogReporting.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Util/PPGeDraw.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/thin3d.h"

#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "Common/GPU/ShaderTranslation.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Debugger/Record.h"

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;

uint8_t clut[1024];
FormatBuffer fb;
FormatBuffer depthbuf;

struct CommandInfo {
	uint64_t flags;
	SoftGPU::CmdFunc func;
};
static CommandInfo softgpuCmdInfo[256];

struct SoftwareCommandTableEntry {
	uint8_t cmd;
	uint8_t flags;
	SoftDirty dirty;
	SoftGPU::CmdFunc func;
};

// Software uses a different one, because dirty flags and execute funcs are a bit different.
const SoftwareCommandTableEntry softgpuCommandTable[] = {
	{ GE_CMD_OFFSETADDR, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_OffsetAddr },
	{ GE_CMD_ORIGIN, FLAG_EXECUTE | FLAG_READS_PC, SoftDirty::NONE, &GPUCommon::Execute_Origin },
	{ GE_CMD_JUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, SoftDirty::NONE, &GPUCommon::Execute_Jump },
	{ GE_CMD_CALL, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, SoftDirty::NONE, &SoftGPU::Execute_Call },
	{ GE_CMD_RET, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, SoftDirty::NONE, &GPUCommon::Execute_Ret },
	{ GE_CMD_END, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, SoftDirty::NONE, &GPUCommon::Execute_End },
	{ GE_CMD_VADDR, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Vaddr },
	{ GE_CMD_IADDR, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Iaddr },
	{ GE_CMD_BJUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, SoftDirty::NONE, &GPUCommon::Execute_BJump },
	{ GE_CMD_BOUNDINGBOX, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_BoundingBox },

	{ GE_CMD_PRIM, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_Prim },
	{ GE_CMD_BEZIER, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_Bezier },
	{ GE_CMD_SPLINE, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_Spline },

	// Vertex type affects a number of things, mainly because of through.
	{ GE_CMD_VERTEXTYPE, FLAG_EXECUTEONCHANGE, SoftDirty::TRANSFORM_BASIC, &SoftGPU::Execute_VertexType },

	{ GE_CMD_LOADCLUT, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_LoadClut },

	// These two are actually processed in CMD_END, no flush needed.
	{ GE_CMD_SIGNAL },
	{ GE_CMD_FINISH },

	// Changes that dirty the framebuffer or depthbuffer pointer/size.
	{ GE_CMD_FRAMEBUFPTR, FLAG_EXECUTEONCHANGE, SoftDirty::BINNER_RANGE, &SoftGPU::Execute_FramebufPtr },
	{ GE_CMD_FRAMEBUFWIDTH, FLAG_EXECUTEONCHANGE, SoftDirty::BINNER_RANGE | SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED, &SoftGPU::Execute_FramebufPtr },
	{ GE_CMD_FRAMEBUFPIXFORMAT, FLAG_EXECUTEONCHANGE, SoftDirty::BINNER_RANGE | SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_STENCIL | SoftDirty::PIXEL_WRITEMASK, &SoftGPU::Execute_FramebufFormat },
	{ GE_CMD_ZBUFPTR, FLAG_EXECUTEONCHANGE, SoftDirty::BINNER_RANGE, &SoftGPU::Execute_ZbufPtr },
	{ GE_CMD_ZBUFWIDTH, FLAG_EXECUTEONCHANGE, SoftDirty::BINNER_RANGE | SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED, &SoftGPU::Execute_ZbufPtr },

	{ GE_CMD_FOGCOLOR, 0, SoftDirty::PIXEL_CACHED },
	{ GE_CMD_FOG1, 0, SoftDirty::TRANSFORM_FOG },
	{ GE_CMD_FOG2, 0, SoftDirty::TRANSFORM_FOG },

	{ GE_CMD_CLEARMODE, 0, SoftDirty::TRANSFORM_BASIC | SoftDirty::RAST_BASIC | SoftDirty::RAST_TEX | SoftDirty::SAMPLER_BASIC | SoftDirty::SAMPLER_TEXLIST | SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_ALPHA | SoftDirty::PIXEL_STENCIL | SoftDirty::PIXEL_CACHED | SoftDirty::BINNER_RANGE | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXTUREMAPENABLE, 0, SoftDirty::SAMPLER_BASIC | SoftDirty::SAMPLER_TEXLIST | SoftDirty::RAST_TEX | SoftDirty::TRANSFORM_BASIC | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_FOGENABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED | SoftDirty::TRANSFORM_BASIC | SoftDirty::TRANSFORM_FOG | SoftDirty::TRANSFORM_MATRIX },
	{ GE_CMD_TEXMODE, 0, SoftDirty::SAMPLER_BASIC | SoftDirty::SAMPLER_TEXLIST | SoftDirty::RAST_TEX },
	// Currently this doesn't affect any state, but maybe it should.
	{ GE_CMD_TEXSHADELS },
	{ GE_CMD_SHADEMODE, 0, SoftDirty::RAST_BASIC },
	{ GE_CMD_TEXFUNC, 0, SoftDirty::SAMPLER_BASIC },
	{ GE_CMD_COLORTEST, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED },
	{ GE_CMD_ALPHATESTENABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_ALPHA },
	{ GE_CMD_COLORTESTENABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED },
	{ GE_CMD_COLORTESTMASK, 0, SoftDirty::PIXEL_CACHED },

	{ GE_CMD_REVERSENORMAL, 0, SoftDirty::TRANSFORM_BASIC },
	{ GE_CMD_LIGHTINGENABLE, 0, SoftDirty::TRANSFORM_BASIC | SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 | SoftDirty::LIGHT_1 | SoftDirty::LIGHT_2 | SoftDirty::LIGHT_3 },
	{ GE_CMD_LIGHTENABLE0, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 },
	{ GE_CMD_LIGHTENABLE1, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_1 },
	{ GE_CMD_LIGHTENABLE2, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_2 },
	{ GE_CMD_LIGHTENABLE3, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_3 },
	{ GE_CMD_LIGHTTYPE0, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_0 },
	{ GE_CMD_LIGHTTYPE1, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_1 },
	{ GE_CMD_LIGHTTYPE2, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_2 },
	{ GE_CMD_LIGHTTYPE3, 0, SoftDirty::TRANSFORM_MATRIX | SoftDirty::LIGHT_3 },
	{ GE_CMD_MATERIALUPDATE, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL },

	{ GE_CMD_LIGHTMODE, 0, SoftDirty::LIGHT_BASIC },

	{ GE_CMD_TEXFILTER, 0, SoftDirty::SAMPLER_BASIC | SoftDirty::SAMPLER_TEXLIST | SoftDirty::RAST_TEX },
	{ GE_CMD_TEXWRAP, 0, SoftDirty::SAMPLER_BASIC },

	{ GE_CMD_ALPHATEST, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_ALPHA },
	{ GE_CMD_COLORREF, 0, SoftDirty::PIXEL_CACHED },
	{ GE_CMD_TEXENVCOLOR, 0, SoftDirty::PIXEL_CACHED },

	// Currently, this is not part of state, just read on vertex processing.
	{ GE_CMD_CULL },
	{ GE_CMD_CULLFACEENABLE },

	{ GE_CMD_DITHERENABLE, 0, SoftDirty::PIXEL_BASIC },
	{ GE_CMD_STENCILOP, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_STENCIL },
	{ GE_CMD_STENCILTEST, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_STENCIL },
	{ GE_CMD_STENCILTESTENABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_STENCIL },
	{ GE_CMD_ALPHABLENDENABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_ALPHA },
	{ GE_CMD_BLENDMODE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_ALPHA },
	{ GE_CMD_BLENDFIXEDA, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_ALPHA | SoftDirty::PIXEL_CACHED },
	{ GE_CMD_BLENDFIXEDB, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_ALPHA | SoftDirty::PIXEL_CACHED },
	{ GE_CMD_MASKRGB, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_WRITEMASK },
	{ GE_CMD_MASKALPHA, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_WRITEMASK },
	{ GE_CMD_ZTEST, 0, SoftDirty::PIXEL_BASIC },
	{ GE_CMD_ZTESTENABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::BINNER_RANGE | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_ZWRITEDISABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::BINNER_RANGE | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_LOGICOP, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED },
	{ GE_CMD_LOGICOPENABLE, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED },

	{ GE_CMD_TEXMAPMODE, 0, SoftDirty::TRANSFORM_BASIC | SoftDirty::RAST_TEX },

	// These are read on every SubmitPrim, no need for dirtying or flushing.
	{ GE_CMD_TEXSCALEU },
	{ GE_CMD_TEXSCALEV },
	{ GE_CMD_TEXOFFSETU },
	{ GE_CMD_TEXOFFSETV },

	{ GE_CMD_TEXSIZE0, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXSIZE1, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXSIZE2, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXSIZE3, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXSIZE4, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXSIZE5, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXSIZE6, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXSIZE7, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXFORMAT, 0, SoftDirty::SAMPLER_BASIC | SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXLEVEL, 0, SoftDirty::RAST_TEX },
	{ GE_CMD_TEXLODSLOPE, 0, SoftDirty::RAST_TEX },
	{ GE_CMD_TEXADDR0, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXADDR1, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXADDR2, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXADDR3, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXADDR4, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXADDR5, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXADDR6, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXADDR7, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH0, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH1, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH2, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH3, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH4, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH5, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH6, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },
	{ GE_CMD_TEXBUFWIDTH7, 0, SoftDirty::SAMPLER_TEXLIST | SoftDirty::BINNER_OVERLAP },

	{ GE_CMD_CLUTADDR },
	{ GE_CMD_CLUTADDRUPPER },
	{ GE_CMD_CLUTFORMAT, 0, SoftDirty::SAMPLER_BASIC },

	// Morph weights. TODO: Remove precomputation?
	{ GE_CMD_MORPHWEIGHT0, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT1, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT2, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT3, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT4, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT5, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT6, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT7, FLAG_EXECUTEONCHANGE, SoftDirty::NONE, &GPUCommon::Execute_MorphWeight },

	// No state of flushing required for patch parameters, currently.
	{ GE_CMD_PATCHDIVISION },
	{ GE_CMD_PATCHPRIMITIVE },
	{ GE_CMD_PATCHFACING },
	{ GE_CMD_PATCHCULLENABLE },

	// Can probably ignore this one as we don't support AA lines.
	{ GE_CMD_ANTIALIASENABLE, 0, SoftDirty::RAST_BASIC },

	// Viewport and offset for positions.
	{ GE_CMD_OFFSETX, 0, SoftDirty::RAST_OFFSET },
	{ GE_CMD_OFFSETY, 0, SoftDirty::RAST_OFFSET },
	{ GE_CMD_VIEWPORTXSCALE, 0, SoftDirty::TRANSFORM_VIEWPORT },
	{ GE_CMD_VIEWPORTYSCALE, 0, SoftDirty::TRANSFORM_VIEWPORT },
	{ GE_CMD_VIEWPORTXCENTER, 0, SoftDirty::TRANSFORM_VIEWPORT },
	{ GE_CMD_VIEWPORTYCENTER, 0, SoftDirty::TRANSFORM_VIEWPORT },
	{ GE_CMD_VIEWPORTZSCALE, 0, SoftDirty::TRANSFORM_VIEWPORT },
	{ GE_CMD_VIEWPORTZCENTER, 0, SoftDirty::TRANSFORM_VIEWPORT },
	{ GE_CMD_DEPTHCLAMPENABLE, 0, SoftDirty::TRANSFORM_BASIC },

	// Z clipping.
	{ GE_CMD_MINZ, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED },
	{ GE_CMD_MAXZ, 0, SoftDirty::PIXEL_BASIC | SoftDirty::PIXEL_CACHED },

	// Region doesn't seem to affect scissor or anything.
	// As long as REGION1 is zero, REGION2 is effectively another scissor.
	{ GE_CMD_REGION1, 0, SoftDirty::BINNER_RANGE },
	{ GE_CMD_REGION2, 0, SoftDirty::BINNER_RANGE },

	// Scissor, only used by the binner.
	{ GE_CMD_SCISSOR1, 0, SoftDirty::BINNER_RANGE },
	{ GE_CMD_SCISSOR2, 0, SoftDirty::BINNER_RANGE },

	// Lighting base colors.
	{ GE_CMD_AMBIENTCOLOR, 0, SoftDirty::LIGHT_MATERIAL },
	{ GE_CMD_AMBIENTALPHA, 0, SoftDirty::LIGHT_MATERIAL },
	{ GE_CMD_MATERIALDIFFUSE, 0, SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 | SoftDirty::LIGHT_1 | SoftDirty::LIGHT_2 | SoftDirty::LIGHT_3 },
	// Not currently state, but maybe should be.
	{ GE_CMD_MATERIALEMISSIVE, 0, SoftDirty::NONE },
	{ GE_CMD_MATERIALAMBIENT, 0, SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 | SoftDirty::LIGHT_1 | SoftDirty::LIGHT_2 | SoftDirty::LIGHT_3 },
	{ GE_CMD_MATERIALALPHA, 0, SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 | SoftDirty::LIGHT_1 | SoftDirty::LIGHT_2 | SoftDirty::LIGHT_3 },
	{ GE_CMD_MATERIALSPECULAR, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 | SoftDirty::LIGHT_1 | SoftDirty::LIGHT_2 | SoftDirty::LIGHT_3 },
	{ GE_CMD_MATERIALSPECULARCOEF, 0, SoftDirty::LIGHT_BASIC },

	{ GE_CMD_LX0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LY0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LZ0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LX1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LY1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LZ1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LX2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LY2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LZ2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LX3, 0, SoftDirty::LIGHT_3 },
	{ GE_CMD_LY3, 0, SoftDirty::LIGHT_3 },
	{ GE_CMD_LZ3, 0, SoftDirty::LIGHT_3 },

	{ GE_CMD_LDX0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LDY0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LDZ0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LDX1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LDY1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LDZ1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LDX2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LDY2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LDZ2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LDX3, 0, SoftDirty::LIGHT_3 },
	{ GE_CMD_LDY3, 0, SoftDirty::LIGHT_3 },
	{ GE_CMD_LDZ3, 0, SoftDirty::LIGHT_3 },

	{ GE_CMD_LKA0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LKB0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LKC0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LKA1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LKB1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LKC1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LKA2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LKB2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LKC2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LKA3, 0, SoftDirty::LIGHT_3 },
	{ GE_CMD_LKB3, 0, SoftDirty::LIGHT_3 },
	{ GE_CMD_LKC3, 0, SoftDirty::LIGHT_3 },

	{ GE_CMD_LKS0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LKS1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LKS2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LKS3, 0, SoftDirty::LIGHT_3 },

	{ GE_CMD_LKO0, 0, SoftDirty::LIGHT_0 },
	{ GE_CMD_LKO1, 0, SoftDirty::LIGHT_1 },
	{ GE_CMD_LKO2, 0, SoftDirty::LIGHT_2 },
	{ GE_CMD_LKO3, 0, SoftDirty::LIGHT_3 },

	// Specific light colors.
	{ GE_CMD_LAC0, 0, SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 },
	{ GE_CMD_LDC0, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 },
	{ GE_CMD_LSC0, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 },
	{ GE_CMD_LAC1, 0, SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_1 },
	{ GE_CMD_LDC1, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_1 },
	{ GE_CMD_LSC1, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_1 },
	{ GE_CMD_LAC2, 0, SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_2 },
	{ GE_CMD_LDC2, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_2 },
	{ GE_CMD_LSC2, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_2 },
	{ GE_CMD_LAC3, 0, SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_3 },
	{ GE_CMD_LDC3, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_3 },
	{ GE_CMD_LSC3, 0, SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_3 },

	// These are currently ignored, but might do flushing later.
	{ GE_CMD_TEXFLUSH },
	{ GE_CMD_TEXSYNC },

	// These are just nop or part of other later commands.
	{ GE_CMD_NOP },
	{ GE_CMD_BASE },
	{ GE_CMD_TRANSFERSRC },
	{ GE_CMD_TRANSFERSRCW },
	{ GE_CMD_TRANSFERDST },
	{ GE_CMD_TRANSFERDSTW },
	{ GE_CMD_TRANSFERSRCPOS },
	{ GE_CMD_TRANSFERDSTPOS },
	{ GE_CMD_TRANSFERSIZE },

	// This will flush if necessary.
	{ GE_CMD_TRANSFERSTART, FLAG_EXECUTE | FLAG_READS_PC, SoftDirty::NONE, &SoftGPU::Execute_BlockTransferStart },

	// We cache the dither matrix, but the values affect little.
	{ GE_CMD_DITH0, 0, SoftDirty::PIXEL_DITHER },
	{ GE_CMD_DITH1, 0, SoftDirty::PIXEL_DITHER },
	{ GE_CMD_DITH2, 0, SoftDirty::PIXEL_DITHER },
	{ GE_CMD_DITH3, 0, SoftDirty::PIXEL_DITHER },

	{ GE_CMD_WORLDMATRIXNUMBER, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_WorldMtxNum },
	{ GE_CMD_WORLDMATRIXDATA, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_WorldMtxData },
	{ GE_CMD_VIEWMATRIXNUMBER, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_ViewMtxNum },
	{ GE_CMD_VIEWMATRIXDATA, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_ViewMtxData },
	{ GE_CMD_PROJMATRIXNUMBER, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_ProjMtxNum },
	{ GE_CMD_PROJMATRIXDATA, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_ProjMtxData },
	// Currently not state.
	{ GE_CMD_TGENMATRIXNUMBER, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_TgenMtxNum },
	{ GE_CMD_TGENMATRIXDATA, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_TgenMtxData },
	{ GE_CMD_BONEMATRIXNUMBER, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_BoneMtxNum },
	{ GE_CMD_BONEMATRIXDATA, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_BoneMtxData },

	// Vertex Screen/Texture/Color
	{ GE_CMD_VSCX },
	{ GE_CMD_VSCY },
	{ GE_CMD_VSCZ },
	{ GE_CMD_VTCS },
	{ GE_CMD_VTCT },
	{ GE_CMD_VTCQ },
	{ GE_CMD_VCV },
	{ GE_CMD_VAP, FLAG_EXECUTE, SoftDirty::NONE, &SoftGPU::Execute_ImmVertexAlphaPrim },
	{ GE_CMD_VFC },
	{ GE_CMD_VSCV },

	// "Missing" commands (gaps in the sequence)
	{ GE_CMD_UNKNOWN_03, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_0D, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_11, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_29, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_34, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_35, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_39, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_4E, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_4F, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_52, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_59, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_5A, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_B6, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_B7, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_D1, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_ED, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_EF, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FA, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FB, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FC, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FD, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FE, FLAG_EXECUTE, SoftDirty::NONE, &GPUCommon::Execute_Unknown },
	// Appears to be debugging related or something?  Hit a lot in GoW.
	{ GE_CMD_NOP_FF },
};

SoftGPU::SoftGPU(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw)
{
	fb.data = Memory::GetPointerWrite(0x44000000); // TODO: correct default address?
	depthbuf.data = Memory::GetPointerWrite(0x44000000); // TODO: correct default address?

	memset(softgpuCmdInfo, 0, sizeof(softgpuCmdInfo));

	// Convert the command table to a faster format, and check for dupes.
	std::set<u8> dupeCheck;
	for (size_t i = 0; i < ARRAY_SIZE(softgpuCommandTable); i++) {
		const u8 cmd = softgpuCommandTable[i].cmd;
		if (dupeCheck.find(cmd) != dupeCheck.end()) {
			ERROR_LOG(Log::G3D, "Command table Dupe: %02x (%i)", (int)cmd, (int)cmd);
		} else {
			dupeCheck.insert(cmd);
		}
		softgpuCmdInfo[cmd].flags |= (uint64_t)softgpuCommandTable[i].flags | ((uint64_t)softgpuCommandTable[i].dirty << 8);
		softgpuCmdInfo[cmd].func = softgpuCommandTable[i].func;
		if ((softgpuCmdInfo[cmd].flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE)) && !softgpuCmdInfo[cmd].func) {
			// Can't have FLAG_EXECUTE commands without a function pointer to execute.
			Crash();
		}
	}
	// Find commands missing from the table.
	for (int i = 0; i < 0xEF; i++) {
		if (dupeCheck.find((u8)i) == dupeCheck.end()) {
			ERROR_LOG(Log::G3D, "Command missing from table: %02x (%i)", i, i);
		}
	}

	memset(vramDirty_, (uint8_t)(SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY), sizeof(vramDirty_));
	// TODO: Is there a default?
	displayFramebuf_ = 0;
	displayStride_ = 512;
	displayFormat_ = GE_FORMAT_8888;

	Rasterizer::Init();
	Sampler::Init();
	drawEngine_ = new SoftwareDrawEngine();
	drawEngine_->SetGPUCommon(this);
	drawEngine_->Init();
	drawEngineCommon_ = drawEngine_;

	// Push the initial CLUT buffer in case it's all zero (we push only on change.)
	if (drawEngine_->transformUnit.IsStarted())
		drawEngine_->transformUnit.NotifyClutUpdate(clut);

	// No need to flush for simple parameter changes.
	flushOnParams_ = false;

	if (gfxCtx && draw) {
		presentation_ = new PresentationCommon(draw_);
		presentation_->SetLanguage(draw_->GetShaderLanguageDesc().shaderLanguage);
		presentation_->UpdateDisplaySize(PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		presentation_->UpdateRenderSize(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
	}

	NotifyConfigChanged();
	NotifyDisplayResized();
	NotifyRenderResized();
}

void SoftGPU::DeviceLost() {
	if (presentation_)
		presentation_->DeviceLost();
	draw_ = nullptr;
	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}
}

void SoftGPU::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	if (presentation_)
		presentation_->DeviceRestore(draw_);
	PPGeSetDrawContext(draw_);
}

SoftGPU::~SoftGPU() {
	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}

	delete presentation_;
	delete drawEngine_;

	Sampler::Shutdown();
	Rasterizer::Shutdown();
}

void SoftGPU::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	// Seems like this can point into RAM, but should be VRAM if not in RAM.
	displayFramebuf_ = (framebuf & 0xFF000000) == 0 ? 0x44000000 | framebuf : framebuf;
	displayStride_ = stride;
	displayFormat_ = format;

	NotifyDisplay(framebuf, stride, format);
}

DSStretch g_DarkStalkerStretch;

void SoftGPU::ConvertTextureDescFrom16(Draw::TextureDesc &desc, int srcwidth, int srcheight, const uint16_t *overrideData) {
	// TODO: This should probably be converted in a shader instead..
	fbTexBuffer_.resize(srcwidth * srcheight);
	const uint16_t *displayBuffer = overrideData;
	if (!displayBuffer)
		displayBuffer = (const uint16_t *)Memory::GetPointer(displayFramebuf_);

	for (int y = 0; y < srcheight; ++y) {
		u32 *buf_line = &fbTexBuffer_[y * srcwidth];
		const u16 *fb_line = &displayBuffer[y * displayStride_];

		switch (displayFormat_) {
		case GE_FORMAT_565:
			ConvertRGB565ToRGBA8888(buf_line, fb_line, srcwidth);
			break;

		case GE_FORMAT_5551:
			ConvertRGBA5551ToRGBA8888(buf_line, fb_line, srcwidth);
			break;

		case GE_FORMAT_4444:
			ConvertRGBA4444ToRGBA8888(buf_line, fb_line, srcwidth);
			break;

		default:
			ERROR_LOG_REPORT(Log::G3D, "Software: Unexpected framebuffer format: %d", displayFormat_);
			break;
		}
	}

	desc.width = srcwidth;
	desc.height = srcheight;
	desc.initData.push_back((uint8_t *)fbTexBuffer_.data());
}

// Copies RGBA8 data from RAM to the currently bound render target.
void SoftGPU::CopyToCurrentFboFromDisplayRam(int srcwidth, int srcheight) {
	if (!draw_ || !presentation_)
		return;
	float u0 = 0.0f;
	float u1;
	float v0 = 0.0f;
	float v1 = 1.0f;

	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}

	// For accuracy, try to handle 0 stride - sometimes used.
	if (displayStride_ == 0) {
		srcheight = 1;
		u1 = 1.0f;
	} else {
		u1 = (float)srcwidth / displayStride_;
	}

	Draw::TextureDesc desc{};
	desc.type = Draw::TextureType::LINEAR2D;
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.tag = "SoftGPU";
	bool hasImage = true;

	OutputFlags outputFlags = g_Config.iDisplayFilter == SCALE_NEAREST ? OutputFlags::NEAREST : OutputFlags::LINEAR;
	bool hasPostShader = presentation_ && presentation_->HasPostShader();

	if (PSP_CoreParameter().compat.flags().DarkStalkersPresentHack && displayFormat_ == GE_FORMAT_5551 && g_DarkStalkerStretch != DSStretch::Off) {
		const u8 *data = Memory::GetPointerWrite(0x04088000);
		bool fillDesc = true;
		if (draw_->GetDataFormatSupport(Draw::DataFormat::A1B5G5R5_UNORM_PACK16) & Draw::FMT_TEXTURE) {
			// The perfect one.
			desc.format = Draw::DataFormat::A1B5G5R5_UNORM_PACK16;
		} else if (!hasPostShader && (draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16) & Draw::FMT_TEXTURE)) {
			// RB swapped, compensate with a shader.
			desc.format = Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
			outputFlags |= OutputFlags::RB_SWIZZLE;
		} else {
			ConvertTextureDescFrom16(desc, srcwidth, srcheight, (const uint16_t *)data);
			fillDesc = false;
		}
		if (fillDesc) {
			desc.width = displayStride_ == 0 ? srcwidth : displayStride_;
			desc.height = srcheight;
			desc.initData.push_back(data);
		}
		u0 = 64.5f / (float)desc.width;
		u1 = 447.5f / (float)desc.width;
		v0 = 16.0f / (float)desc.height;
		v1 = 240.0f / (float)desc.height;
		if (g_DarkStalkerStretch == DSStretch::Normal) {
			outputFlags |= OutputFlags::PILLARBOX;
		}
	} else if (!Memory::IsValidAddress(displayFramebuf_) || srcwidth == 0 || srcheight == 0) {
		hasImage = false;
		u1 = 1.0f;
	} else if (displayFormat_ == GE_FORMAT_8888) {
		const u8 *data = Memory::GetPointer(displayFramebuf_);
		desc.width = displayStride_ == 0 ? srcwidth : displayStride_;
		desc.height = srcheight;
		desc.initData.push_back(data);
		desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	} else if (displayFormat_ == GE_FORMAT_5551) {
		const u8 *data = Memory::GetPointer(displayFramebuf_);
		bool fillDesc = true;
		if (draw_->GetDataFormatSupport(Draw::DataFormat::A1B5G5R5_UNORM_PACK16) & Draw::FMT_TEXTURE) {
			// The perfect one.
			desc.format = Draw::DataFormat::A1B5G5R5_UNORM_PACK16;
		} else if (!hasPostShader && (draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16) & Draw::FMT_TEXTURE)) {
			// RB swapped, compensate with a shader.
			desc.format = Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
			outputFlags |= OutputFlags::RB_SWIZZLE;
		} else {
			ConvertTextureDescFrom16(desc, srcwidth, srcheight);
			u1 = 1.0f;
			fillDesc = false;
		}
		if (fillDesc) {
			desc.width = displayStride_ == 0 ? srcwidth : displayStride_;
			desc.height = srcheight;
			desc.initData.push_back(data);
		}
	} else {
		ConvertTextureDescFrom16(desc, srcwidth, srcheight);
		u1 = 1.0f;
	}
	if (!hasImage) {
		draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "CopyToCurrentFboFromDisplayRam");
		presentation_->NotifyPresent();
		return;
	}

	fbTex = draw_->CreateTexture(desc);

	switch (GetGPUBackend()) {
	case GPUBackend::OPENGL:
		outputFlags |= OutputFlags::BACKBUFFER_FLIPPED;
		break;
	case GPUBackend::DIRECT3D11:
		outputFlags |= OutputFlags::POSITION_FLIPPED;
		break;
	case GPUBackend::VULKAN:
		break;
	}

	presentation_->SourceTexture(fbTex, desc.width, desc.height);
	presentation_->CopyToOutput(outputFlags, g_Config.iInternalScreenRotation, u0, v0, u1, v1);
}

void SoftGPU::CopyDisplayToOutput(bool reallyDirty) {
	drawEngine_->transformUnit.Flush(this, "output");
	// The display always shows 480x272.
	CopyToCurrentFboFromDisplayRam(FB_WIDTH, FB_HEIGHT);
	MarkDirty(displayFramebuf_, displayStride_, 272, displayFormat_, SoftGPUVRAMDirty::CLEAR);
}

void SoftGPU::BeginHostFrame() {
	GPUCommon::BeginHostFrame();
	if (presentation_) {
		presentation_->BeginFrame();
	}
}

bool SoftGPU::PresentedThisFrame() const {
	return presentation_->PresentedThisFrame();
}

void SoftGPU::MarkDirty(uint32_t addr, uint32_t stride, uint32_t height, GEBufferFormat fmt, SoftGPUVRAMDirty value) {
	uint32_t bytes = height * stride * (fmt == GE_FORMAT_8888 ? 4 : 2);
	MarkDirty(addr, bytes, value);
}

void SoftGPU::MarkDirty(uint32_t addr, uint32_t bytes, SoftGPUVRAMDirty value) {
	// Only bother tracking if frameskipping.
	if (g_Config.iFrameSkip == 0)
		return;
	if (!Memory::IsVRAMAddress(addr) || !Memory::IsVRAMAddress(addr + bytes - 1))
		return;
	if (lastDirtyAddr_ == addr && lastDirtySize_ == bytes && lastDirtyValue_ == value)
		return;

	uint32_t start = ((addr - PSP_GetVidMemBase()) & 0x001FFFFF) >> 10;
	uint32_t end = start + ((bytes + 1023) >> 10);
	if (end > sizeof(vramDirty_)) {
		end = sizeof(vramDirty_);
	}
	if (value == SoftGPUVRAMDirty::CLEAR || value == (SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY)) {
		memset(vramDirty_ + start, (uint8_t)value, end - start);
	} else {
		for (uint32_t i = start; i < end; ++i) {
			vramDirty_[i] |= (uint8_t)value;
		}
	}

	lastDirtyAddr_ = addr;
	lastDirtySize_ = bytes;
	lastDirtyValue_ = value;
}

bool SoftGPU::ClearDirty(uint32_t addr, uint32_t stride, uint32_t height, GEBufferFormat fmt, SoftGPUVRAMDirty value) {
	uint32_t bytes = height * stride * (fmt == GE_FORMAT_8888 ? 4 : 2);
	return ClearDirty(addr, bytes, value);
}

bool SoftGPU::ClearDirty(uint32_t addr, uint32_t bytes, SoftGPUVRAMDirty value) {
	if (!Memory::IsVRAMAddress(addr) || !Memory::IsVRAMAddress(addr + bytes - 1))
		return false;

	uint32_t start = ((addr - PSP_GetVidMemBase()) & 0x001FFFFF) >> 10;
	uint32_t end = start + ((bytes + 1023) >> 10);
	bool result = false;
	for (uint32_t i = start; i < end; ++i) {
		if (vramDirty_[i] & (uint8_t)value) {
			result = true;
			vramDirty_[i] &= ~(uint8_t)value;
		}
	}

	lastDirtyAddr_ = 0;
	lastDirtySize_ = 0;

	return result;
}

void SoftGPU::NotifyRenderResized() {
	// Force the render params to 480x272 so other things work.
	if (g_Config.IsPortrait()) {
		PSP_CoreParameter().renderWidth = 272;
		PSP_CoreParameter().renderHeight = 480;
	} else {
		PSP_CoreParameter().renderWidth = 480;
		PSP_CoreParameter().renderHeight = 272;
	}
}

void SoftGPU::NotifyDisplayResized() {
	displayResized_ = true;
}

void SoftGPU::CheckDisplayResized() {
	if (displayResized_ && presentation_) {
		presentation_->UpdateDisplaySize(PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		presentation_->UpdateRenderSize(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
		presentation_->UpdatePostShader();
		displayResized_ = false;
	}
}

void SoftGPU::CheckConfigChanged() {
	if (configChanged_) {
		drawEngineCommon_->NotifyConfigChanged();
		BuildReportingInfo();
		if (presentation_) {
			presentation_->UpdatePostShader();
		}
		configChanged_ = false;
	}
}

void SoftGPU::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("soft_runloop");
	const auto *cmdInfo = softgpuCmdInfo;
	int dc = downcount;
	SoftDirty dirty = dirtyFlags_;
	for (; dc > 0; --dc) {
		u32 op = Memory::ReadUnchecked_U32(list.pc);
		const u32 cmd = op >> 24;
		const auto &info = cmdInfo[cmd];
		const u32 diff = op ^ gstate.cmdmem[cmd];
		if (diff == 0) {
			if (info.flags & FLAG_EXECUTE) {
				downcount = dc;
				dirtyFlags_ = dirty;
				(this->*info.func)(op, diff);
				dirty = dirtyFlags_;
				dc = downcount;
			}
		} else {
			uint64_t flags = info.flags;
			gstate.cmdmem[cmd] = op;
			dirty |= SoftDirty(flags >> 8);
			if (flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE)) {
				downcount = dc;
				dirtyFlags_ = dirty;
				(this->*info.func)(op, diff);
				dirty = dirtyFlags_;
				dc = downcount;
			}
		}
		list.pc += 4;
	}
	downcount = 0;
	dirtyFlags_ = dirty;
}

bool SoftGPU::IsStarted() {
	return drawEngine_ && drawEngine_->transformUnit.IsStarted();
}

void SoftGPU::ExecuteOp(u32 op, u32 diff) {
	const u8 cmd = op >> 24;
	const auto info = softgpuCmdInfo[cmd];
	if (diff == 0) {
		if (info.flags & FLAG_EXECUTE)
			(this->*info.func)(op, diff);
	} else {
		dirtyFlags_ |= SoftDirty(info.flags >> 8);
		if (info.flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE))
			(this->*info.func)(op, diff);
	}
}

void SoftGPU::Execute_BlockTransferStart(u32 op, u32 diff) {
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

	// Use height less one to account for width, which can be greater or less than stride.
	const uint32_t src = srcBasePtr + (srcY * srcStride + srcX) * bpp;
	const uint32_t srcSize = (height - 1) * (srcStride + width) * bpp;
	const uint32_t dst = dstBasePtr + (dstY * dstStride + dstX) * bpp;
	const uint32_t dstSize = (height - 1) * (dstStride + width) * bpp;

	// Need to flush both source and target, so we overwrite properly.
	if (Memory::IsValidRange(src, srcSize) && Memory::IsValidRange(dst, dstSize)) {
		drawEngine_->transformUnit.FlushIfOverlap(this, "blockxfer", false, src, srcStride, width * bpp, height);
		drawEngine_->transformUnit.FlushIfOverlap(this, "blockxfer", true, dst, dstStride, width * bpp, height);
	} else {
		drawEngine_->transformUnit.Flush(this, "blockxfer_wrap");
	}

	DoBlockTransfer(gstate_c.skipDrawReason);

	// Could theoretically dirty the framebuffer.
	MarkDirty(dst, dstSize, SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY);
}

void SoftGPU::Execute_Prim(u32 op, u32 diff) {
	u32 count = op & 0xFFFF;
	// Upper bits are ignored.
	GEPrimitiveType prim = static_cast<GEPrimitiveType>((op >> 16) & 7);
	if (count == 0)
		return;
	FlushImm();

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(Log::G3D, "Software: Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	const void *verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	const void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(Log::G3D, "Software: Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	cyclesExecuted += EstimatePerVertexCost() * count;
	int bytesRead;
	gstate_c.UpdateUVScaleOffset();
	drawEngine_->transformUnit.SetDirty(dirtyFlags_);
	drawEngine_->transformUnit.SubmitPrimitive(verts, indices, prim, count, gstate.vertType, &bytesRead, drawEngine_);
	dirtyFlags_ = drawEngine_->transformUnit.GetDirty();

	SoftGPUVRAMDirty mark = (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) != 0 ? SoftGPUVRAMDirty::DIRTY : SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY;
	MarkDirty(gstate.getFrameBufAddress(), gstate.FrameBufStride(), gstate.getRegionY2() + 1, gstate.FrameBufFormat(), mark);

	// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void SoftGPU::Execute_Bezier(u32 op, u32 diff) {
	// This also make skipping drawing very effective.
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(Log::G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	const void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	const void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(Log::G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if ((gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) || vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(Log::G3D, "Unusual bezier/spline vtype: %08x, morph: %d, bones: %d", gstate.vertType, (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT, vertTypeGetNumBoneWeights(gstate.vertType));
	}

	Spline::BezierSurface surface;
	surface.tess_u = gstate.getPatchDivisionU();
	surface.tess_v = gstate.getPatchDivisionV();
	surface.num_points_u = op & 0xFF;
	surface.num_points_v = (op >> 8) & 0xFF;
	surface.num_patches_u = (surface.num_points_u - 1) / 3;
	surface.num_patches_v = (surface.num_points_v - 1) / 3;
	surface.primType = gstate.getPatchPrimitiveType();
	surface.patchFacing = gstate.patchfacing & 1;

	SetDrawType(DRAW_BEZIER, PatchPrimToPrim(surface.primType));

	int bytesRead = 0;
	gstate_c.UpdateUVScaleOffset();
	drawEngine_->transformUnit.SetDirty(dirtyFlags_);
	drawEngineCommon_->SubmitCurve(control_points, indices, surface, gstate.vertType, &bytesRead, "bezier");
	dirtyFlags_ = drawEngine_->transformUnit.GetDirty();

	SoftGPUVRAMDirty mark = (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) != 0 ? SoftGPUVRAMDirty::DIRTY : SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY;
	MarkDirty(gstate.getFrameBufAddress(), gstate.FrameBufStride(), gstate.getRegionY2() + 1, gstate.FrameBufFormat(), mark);

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = surface.num_points_u * surface.num_points_v;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void SoftGPU::Execute_Spline(u32 op, u32 diff) {
	// This also make skipping drawing very effective.
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(Log::G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	const void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	const void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(Log::G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if ((gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) || vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(Log::G3D, "Unusual bezier/spline vtype: %08x, morph: %d, bones: %d", gstate.vertType, (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT, vertTypeGetNumBoneWeights(gstate.vertType));
	}

	Spline::SplineSurface surface;
	surface.tess_u = gstate.getPatchDivisionU();
	surface.tess_v = gstate.getPatchDivisionV();
	surface.type_u = (op >> 16) & 0x3;
	surface.type_v = (op >> 18) & 0x3;
	surface.num_points_u = op & 0xFF;
	surface.num_points_v = (op >> 8) & 0xFF;
	surface.num_patches_u = surface.num_points_u - 3;
	surface.num_patches_v = surface.num_points_v - 3;
	surface.primType = gstate.getPatchPrimitiveType();
	surface.patchFacing = gstate.patchfacing & 1;

	SetDrawType(DRAW_SPLINE, PatchPrimToPrim(surface.primType));

	int bytesRead = 0;
	gstate_c.UpdateUVScaleOffset();
	drawEngine_->transformUnit.SetDirty(dirtyFlags_);
	drawEngineCommon_->SubmitCurve(control_points, indices, surface, gstate.vertType, &bytesRead, "spline");
	dirtyFlags_ = drawEngine_->transformUnit.GetDirty();

	SoftGPUVRAMDirty mark = (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) != 0 ? SoftGPUVRAMDirty::DIRTY : SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY;
	MarkDirty(gstate.getFrameBufAddress(), gstate.FrameBufStride(), gstate.getRegionY2() + 1, gstate.FrameBufFormat(), mark);

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = surface.num_points_u * surface.num_points_v;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void SoftGPU::Execute_LoadClut(u32 op, u32 diff) {
	u32 clutAddr = gstate.getClutAddress();
	// Avoid the hack in getClutLoadBytes() to inaccurately allow more palette data.
	u32 clutTotalBytes = (gstate.getClutLoadBlocks() & 0x3F) * 32;
	if (clutTotalBytes > 1024)
		clutTotalBytes = 1024;

	// Might be copying drawing into the CLUT, so flush.
	drawEngine_->transformUnit.FlushIfOverlap(this, "loadclut", false, clutAddr, clutTotalBytes, clutTotalBytes, 1);

	bool changed = false;
	if (Memory::IsValidAddress(clutAddr)) {
		u32 validSize = Memory::ValidSize(clutAddr, clutTotalBytes);
		changed = memcmp(clut, Memory::GetPointerUnchecked(clutAddr), validSize) != 0;
		if (changed)
			Memory::MemcpyUnchecked(clut, clutAddr, validSize);
		if (validSize < clutTotalBytes) {
			// Zero out the parts that were outside valid memory.
			memset((u8 *)clut + validSize, 0x00, clutTotalBytes - validSize);
			changed = true;
		}
	} else if (clutAddr != 0) {
		// Some invalid addresses trigger a crash, others fill with zero.  We always fill zero.
		DEBUG_LOG(Log::G3D, "Software: Invalid CLUT address, filling with garbage instead of crashing");
		memset(clut, 0x00, clutTotalBytes);
		changed = true;
	}

	if (changed)
		drawEngine_->transformUnit.NotifyClutUpdate(clut);
	dirtyFlags_ |= SoftDirty::SAMPLER_CLUT;
}

void SoftGPU::Execute_FramebufPtr(u32 op, u32 diff) {
	// We assume fb.data won't change while we're drawing.
	if (diff) {
		drawEngine_->transformUnit.Flush(this, "framebuf");
		fb.data = Memory::GetPointerWrite(gstate.getFrameBufAddress());
	}
}

void SoftGPU::Execute_FramebufFormat(u32 op, u32 diff) {
	// We should flush, because ranges within bins may change.
	if (diff)
		drawEngine_->transformUnit.Flush(this, "framebuf");
}

void SoftGPU::Execute_BoundingBox(u32 op, u32 diff) {
	gstate_c.Dirty(DIRTY_CULL_PLANES);
	GPUCommon::Execute_BoundingBox(op, diff);
}

void SoftGPU::Execute_ZbufPtr(u32 op, u32 diff) {
	// We assume depthbuf.data won't change while we're drawing.
	if (diff) {
		drawEngine_->transformUnit.Flush(this, "depthbuf");
		// For the pointer, ignore memory mirrors.  This also gives some buffer for draws that go outside.
		// TODO: Confirm how wrapping is handled in drawing.  Adjust if we ever handle VRAM mirrors more accurately.
		depthbuf.data = Memory::GetPointerWrite(gstate.getDepthBufAddress() & 0x041FFFF0);
	}
}

void SoftGPU::Execute_VertexType(u32 op, u32 diff) {
	if ((diff & GE_VTYPE_THROUGH_MASK) != 0) {
		// This affects a lot of things, but some don't matter if it's off - so defer to when it's back on.
		dirtyFlags_ |= SoftDirty::RAST_BASIC | SoftDirty::PIXEL_BASIC;
		if ((op & GE_VTYPE_THROUGH_MASK) == 0) {
			dirtyFlags_ |= SoftDirty::TRANSFORM_MATRIX | SoftDirty::TRANSFORM_VIEWPORT | SoftDirty::TRANSFORM_FOG;
			dirtyFlags_ |= SoftDirty::LIGHT_BASIC | SoftDirty::LIGHT_MATERIAL | SoftDirty::LIGHT_0 | SoftDirty::LIGHT_1 | SoftDirty::LIGHT_2 | SoftDirty::LIGHT_3;
			dirtyFlags_ |= SoftDirty::PIXEL_CACHED;
		}
	}
}

void SoftGPU::Execute_WorldMtxNum(u32 op, u32 diff) {
	// Setting 0xFFFFF0 will reset to 0.
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | (op & 0xF);
}

void SoftGPU::Execute_ViewMtxNum(u32 op, u32 diff) {
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | (op & 0xF);
}

void SoftGPU::Execute_ProjMtxNum(u32 op, u32 diff) {
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | (op & 0xF);
}

void SoftGPU::Execute_TgenMtxNum(u32 op, u32 diff) {
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | (op & 0xF);
}

void SoftGPU::Execute_BoneMtxNum(u32 op, u32 diff) {
	// Setting any bits outside 0x7F are ignored and resets the internal counter.
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (op & 0x7F);
}

void SoftGPU::Execute_WorldMtxData(u32 op, u32 diff) {
	int num = gstate.worldmtxnum & 0x00FFFFFF;
	if (num < 12) {
		u32 *target = (u32 *)&gstate.worldMatrix[num];
		u32 newVal = op << 8;
		if (newVal != *target) {
			*target = newVal;
			dirtyFlags_ |= SoftDirty::TRANSFORM_MATRIX;
			gstate_c.Dirty(DIRTY_CULL_PLANES);
		}
	}

	// Also update the CPU visible values, which update differently.
	u32 *target = &matrixVisible.all[12 * 8 + (num & 0xF)];
	*target = op & 0x00FFFFFF;

	num++;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.worldmtxdata = GE_CMD_WORLDMATRIXDATA << 24;
}

void SoftGPU::Execute_ViewMtxData(u32 op, u32 diff) {
	int num = gstate.viewmtxnum & 0x00FFFFFF;
	if (num < 12) {
		u32 *target = (u32 *)&gstate.viewMatrix[num];
		u32 newVal = op << 8;
		if (newVal != *target) {
			*target = newVal;
			dirtyFlags_ |= SoftDirty::TRANSFORM_MATRIX;
			gstate_c.Dirty(DIRTY_CULL_PLANES);
		}
	}

	// Also update the CPU visible values, which update differently.
	u32 *target = &matrixVisible.all[12 * 8 + 12 + (num & 0xF)];
	*target = op & 0x00FFFFFF;

	num++;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.viewmtxdata = GE_CMD_VIEWMATRIXDATA << 24;
}

void SoftGPU::Execute_ProjMtxData(u32 op, u32 diff) {
	int num = gstate.projmtxnum & 0x00FFFFFF;
	if (num < 16) {
		u32 *target = (u32 *)&gstate.projMatrix[num];
		u32 newVal = op << 8;
		if (newVal != *target) {
			*target = newVal;
			dirtyFlags_ |= SoftDirty::TRANSFORM_MATRIX;
			gstate_c.Dirty(DIRTY_CULL_PLANES);
		}
	}

	// Also update the CPU visible values, which update differently.
	u32 *target = &matrixVisible.all[12 * 8 + 12 + 12 + (num & 0xF)];
	*target = op & 0x00FFFFFF;

	num++;
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.projmtxdata = GE_CMD_PROJMATRIXDATA << 24;
}

void SoftGPU::Execute_TgenMtxData(u32 op, u32 diff) {
	int num = gstate.texmtxnum & 0x00FFFFFF;
	if (num < 12) {
		u32 *target = (u32 *)&gstate.tgenMatrix[num];
		u32 newVal = op << 8;
		if (newVal != *target) {
			*target = newVal;
			// This is mainly used in vertex read, but also affects if we enable texture projection.
			dirtyFlags_ |= SoftDirty::RAST_TEX;
		}
	}

	// Doesn't wrap to any other matrix.
	if ((num & 0xF) < 12) {
		matrixVisible.tgen[num & 0xF] = op & 0x00FFFFFF;
	}

	num++;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.texmtxdata = GE_CMD_TGENMATRIXDATA << 24;
}

void SoftGPU::Execute_BoneMtxData(u32 op, u32 diff) {
	int num = gstate.boneMatrixNumber & 0x00FFFFFF;

	if (num < 96) {
		u32 *target = (u32 *)&gstate.boneMatrix[num];
		u32 newVal = op << 8;
		// No dirtying, we read bone data during vertex read.
		*target = newVal;
	}

	// Also update the CPU visible values, which update differently.
	u32 *target = &matrixVisible.all[(num & 0x7F)];
	*target = op & 0x00FFFFFF;

	num++;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.boneMatrixData  = GE_CMD_BONEMATRIXDATA << 24;
}

static void CopyMatrix24(u32_le *result, const u32 *mtx, u32 count, u32 cmdbits) {
	for (u32 i = 0; i < count; ++i) {
		result[i] = mtx[i] | cmdbits;
	}
}

bool SoftGPU::GetMatrix24(GEMatrixType type, u32_le *result, u32 cmdbits) {
	switch (type) {
	case GE_MTX_BONE0:
	case GE_MTX_BONE1:
	case GE_MTX_BONE2:
	case GE_MTX_BONE3:
	case GE_MTX_BONE4:
	case GE_MTX_BONE5:
	case GE_MTX_BONE6:
	case GE_MTX_BONE7:
		CopyMatrix24(result, matrixVisible.bone + (type - GE_MTX_BONE0) * 12, 12, cmdbits);
		break;
	case GE_MTX_TEXGEN:
		CopyMatrix24(result, matrixVisible.tgen, 12, cmdbits);
		break;
	case GE_MTX_WORLD:
		CopyMatrix24(result, matrixVisible.world, 12, cmdbits);
		break;
	case GE_MTX_VIEW:
		CopyMatrix24(result, matrixVisible.view, 12, cmdbits);
		break;
	case GE_MTX_PROJECTION:
		CopyMatrix24(result, matrixVisible.proj, 16, cmdbits);
		break;
	default:
		return false;
	}
	return true;
}

void SoftGPU::ResetMatrices() {
	GPUCommon::ResetMatrices();
	dirtyFlags_ |= SoftDirty::TRANSFORM_MATRIX | SoftDirty::RAST_TEX;
}

void SoftGPU::Execute_ImmVertexAlphaPrim(u32 op, u32 diff) {
	GPUCommon::Execute_ImmVertexAlphaPrim(op, diff);
	// We won't flush as often as hardware renderers, so we want to flush right away.
	FlushImm();
}

void SoftGPU::Execute_Call(u32 op, u32 diff) {
	PROFILE_THIS_SCOPE("gpu_call");

	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
	if (!Memory::IsValidAddress(target)) {
		ERROR_LOG(Log::G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		if (g_Config.bIgnoreBadMemAccess) {
			return;
		}
		gpuState = GPUSTATE_ERROR;
		downcount = 0;
		return;
	}

	const u32 retval = currentList->pc + 4;
	if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
		ERROR_LOG(Log::G3D, "CALL: Stack full!");
	} else {
		auto &stackEntry = currentList->stack[currentList->stackptr++];
		stackEntry.pc = retval;
		stackEntry.offsetAddr = gstate_c.offsetAddr;
		// The base address is NOT saved/restored for a regular call.
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4;	// pc will be increased after we return, counteract that
	}
}

void SoftGPU::FinishDeferred() {
	// Need to flush before going back to CPU, so drawing is appropriately visible.
	drawEngine_->transformUnit.Flush(this, "finish");
}

int SoftGPU::ListSync(int listid, int mode) {
	// Take this as a cue that we need to finish drawing.
	drawEngine_->transformUnit.Flush(this, "listsync");
	return GPUCommon::ListSync(listid, mode);
}

u32 SoftGPU::DrawSync(int mode) {
	// Take this as a cue that we need to finish drawing.
	drawEngine_->transformUnit.Flush(this, "drawsync");
	return GPUCommon::DrawSync(mode);
}

void SoftGPU::GetStats(char *buffer, size_t bufsize) {
	drawEngine_->transformUnit.GetStats(buffer, bufsize);
}

void SoftGPU::InvalidateCache(u32 addr, int size, GPUInvalidationType type)
{
	// Nothing to invalidate.
}

void SoftGPU::PerformWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat format)
{
	// Ignore.
}

bool SoftGPU::PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags) {
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	if (!(flags & GPUCopyFlag::DEBUG_NOTIFIED))
		recorder_.NotifyMemcpy(dest, src, size);
	// Let's just be safe.
	MarkDirty(dest, size, SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY);
	return false;
}

bool SoftGPU::PerformMemorySet(u32 dest, u8 v, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	recorder_.NotifyMemset(dest, v, size);
	// Let's just be safe.
	MarkDirty(dest, size, SoftGPUVRAMDirty::DIRTY | SoftGPUVRAMDirty::REALLY_DIRTY);
	return false;
}

bool SoftGPU::PerformReadbackToMemory(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	return false;
}

bool SoftGPU::PerformWriteColorFromMemory(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	recorder_.NotifyUpload(dest, size);
	return false;
}

bool SoftGPU::PerformWriteStencilFromMemory(u32 dest, int size, WriteStencil flags)
{
	return false;
}

bool SoftGPU::FramebufferDirty() {
	if (g_Config.iFrameSkip != 0) {
		return ClearDirty(displayFramebuf_, displayStride_, 272, displayFormat_, SoftGPUVRAMDirty::DIRTY);
	}
	return true;
}

bool SoftGPU::FramebufferReallyDirty() {
	if (g_Config.iFrameSkip != 0) {
		return ClearDirty(displayFramebuf_, displayStride_, 272, displayFormat_, SoftGPUVRAMDirty::REALLY_DIRTY);
	}
	return true;
}

static DrawingCoords GetTargetSize(int stride) {
	int w = std::min(stride, std::max(gstate.getRegionX2(), gstate.getScissorX2()) + 1);
	int h = std::max(gstate.getRegionY2(), gstate.getScissorY2()) + 1;
	if (gstate.getRegionX2() == 1023 && gstate.getRegionY2() == 1023) {
		// Some games max out region, but always scissor to an appropriate size.
		// Both values always scissor, we just prefer region as it's usually a more stable size.
		w = std::max(stride, gstate.getScissorX2() + 1);
		h = std::max(272, gstate.getScissorY2() + 1);
	}

	return DrawingCoords((s16)w, (s16)h);
}

bool SoftGPU::GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) {
	int stride = gstate.FrameBufStride();
	DrawingCoords size = GetTargetSize(stride);
	GEBufferFormat fmt = gstate.FrameBufFormat();
	const u8 *src = fb.data;

	if (!Memory::IsValidAddress(displayFramebuf_))
		return false;

	if (type == GPU_DBG_FRAMEBUF_DISPLAY) {
		size.x = 480;
		size.y = 272;
		stride = displayStride_;
		fmt = displayFormat_;
		src = Memory::GetPointer(displayFramebuf_);
	}

	buffer.Allocate(size.x, size.y, fmt);

	const int depth = fmt == GE_FORMAT_8888 ? 4 : 2;
	u8 *dst = buffer.GetData();
	const int byteWidth = size.x * depth;
	for (int16_t y = 0; y < size.y; ++y) {
		memcpy(dst, src, byteWidth);
		dst += byteWidth;
		src += stride * depth;
	}
	return true;
}

bool SoftGPU::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	return GetCurrentFramebuffer(buffer, GPU_DBG_FRAMEBUF_DISPLAY, 1);
}

bool SoftGPU::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	DrawingCoords size = GetTargetSize(gstate.DepthBufStride());
	buffer.Allocate(size.x, size.y, GPU_DBG_FORMAT_16BIT);

	const int depth = 2;
	const u8 *src = depthbuf.data;
	u8 *dst = buffer.GetData();
	for (int16_t y = 0; y < size.y; ++y) {
		memcpy(dst, src, size.x * depth);
		dst += size.x * depth;
		src += gstate.DepthBufStride() * depth;
	}
	return true;
}

static inline u8 GetPixelStencil(GEBufferFormat fmt, int fbStride, int x, int y) {
	if (fmt == GE_FORMAT_565) {
		// Always treated as 0 for comparison purposes.
		return 0;
	} else if (fmt == GE_FORMAT_5551) {
		return ((fb.Get16(x, y, fbStride) & 0x8000) != 0) ? 0xFF : 0;
	} else if (fmt == GE_FORMAT_4444) {
		return Convert4To8(fb.Get16(x, y, fbStride) >> 12);
	} else {
		return fb.Get32(x, y, fbStride) >> 24;
	}
}

bool SoftGPU::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	DrawingCoords size = GetTargetSize(gstate.FrameBufStride());
	buffer.Allocate(size.x, size.y, GPU_DBG_FORMAT_8BIT);

	u8 *row = buffer.GetData();
	for (int16_t y = 0; y < size.y; ++y) {
		for (int16_t x = 0; x < size.x; ++x) {
			row[x] = GetPixelStencil(gstate.FrameBufFormat(), gstate.FrameBufStride(), x, y);
		}
		row += size.x;
	}
	return true;
}

bool SoftGPU::GetCurrentTexture(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) {
	*isFramebuffer = false;
	return Rasterizer::GetCurrentTexture(buffer, level);
}

bool SoftGPU::GetCurrentClut(GPUDebugBuffer &buffer)
{
	const u32 bpp = gstate.getClutPaletteFormat() == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const u32 pixels = 1024 / bpp;

	buffer.Allocate(pixels, 1, (GEBufferFormat)gstate.getClutPaletteFormat());
	memcpy(buffer.GetData(), clut, 1024);
	return true;
}

bool SoftGPU::GetCurrentDrawAsDebugVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	gstate_c.UpdateUVScaleOffset();
	return drawEngine_->transformUnit.GetCurrentDrawAsDebugVertices(count, vertices, indices);
}

bool SoftGPU::DescribeCodePtr(const u8 *ptr, std::string &name) {
	std::string subname;
	if (Sampler::DescribeCodePtr(ptr, subname)) {
		name = "SamplerJit:" + subname;
		return true;
	}
	if (Rasterizer::DescribeCodePtr(ptr, subname)) {
		name = "RasterizerJit:" + subname;
		return true;
	}
	return GPUCommon::DescribeCodePtr(ptr, name);
}
