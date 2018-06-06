#include <algorithm>
#include <type_traits>
#include <mutex>

#include "base/timeutil.h"
#include "profiler/profiler.h"

#include "Common/ColorConv.h"
#include "Core/Reporting.h"
#include "GPU/GeDisasm.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "ChunkFile.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceGe.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Debugger/Record.h"

const CommonCommandTableEntry commonCommandTable[] = {
	// From Common. No flushing but definitely need execute.
	{ GE_CMD_OFFSETADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_OffsetAddr },
	{ GE_CMD_ORIGIN, FLAG_EXECUTE | FLAG_READS_PC, 0, &GPUCommon::Execute_Origin },
	{ GE_CMD_JUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Jump },
	{ GE_CMD_CALL, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Call },
	{ GE_CMD_RET, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Ret },
	{ GE_CMD_END, FLAG_FLUSHBEFORE | FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_End },
	{ GE_CMD_VADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_Vaddr },
	{ GE_CMD_IADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_Iaddr },
	{ GE_CMD_BJUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_BJump },  // EXECUTE
	{ GE_CMD_BOUNDINGBOX, FLAG_EXECUTE, 0, &GPUCommon::Execute_BoundingBox }, // + FLUSHBEFORE when we implement... or not, do we need to?

	{ GE_CMD_PRIM, FLAG_EXECUTE, 0, &GPUCommon::Execute_Prim },
	{ GE_CMD_BEZIER, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPUCommon::Execute_Bezier },
	{ GE_CMD_SPLINE, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPUCommon::Execute_Spline },

	// Changing the vertex type requires us to flush.
	{ GE_CMD_VERTEXTYPE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_VertexType },

	{ GE_CMD_LOADCLUT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPUCommon::Execute_LoadClut },

	// These two are actually processed in CMD_END. Not sure if FLAG_FLUSHBEFORE matters.
	{ GE_CMD_SIGNAL, FLAG_FLUSHBEFORE },
	{ GE_CMD_FINISH, FLAG_FLUSHBEFORE },

	// Changes that dirty the framebuffer
	{ GE_CMD_FRAMEBUFPTR, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_FRAMEBUFWIDTH, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_FRAMEBUFPIXFORMAT, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_ZBUFPTR, FLAG_FLUSHBEFOREONCHANGE },
	{ GE_CMD_ZBUFWIDTH, FLAG_FLUSHBEFOREONCHANGE },

	{ GE_CMD_FOGCOLOR, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FOGCOLOR },
	{ GE_CMD_FOG1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FOGCOEF },
	{ GE_CMD_FOG2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FOGCOEF },

	// These affect the fragment shader so need flushing.
	{ GE_CMD_CLEARMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_TEXTUREMAPENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_FOGENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE},
	{ GE_CMD_TEXMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_TEXSHADELS, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },
	// Raster state for Direct3D 9, uncommon.
	{ GE_CMD_SHADEMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE },
	{ GE_CMD_TEXFUNC, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_COLORTEST, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_ALPHATESTENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_COLORTESTENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_COLORTESTMASK, FLAG_FLUSHBEFOREONCHANGE, DIRTY_ALPHACOLORMASK | DIRTY_FRAGMENTSHADER_STATE },

	// These change the vertex shader so need flushing.
	{ GE_CMD_REVERSENORMAL, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },
	{ GE_CMD_LIGHTINGENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_LIGHTENABLE0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE  },
	{ GE_CMD_LIGHTENABLE1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE  },
	{ GE_CMD_LIGHTENABLE2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE  },
	{ GE_CMD_LIGHTENABLE3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE  },
	{ GE_CMD_LIGHTTYPE0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT0 },
	{ GE_CMD_LIGHTTYPE1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT1 },
	{ GE_CMD_LIGHTTYPE2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT2 },
	{ GE_CMD_LIGHTTYPE3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT3 },
	{ GE_CMD_MATERIALUPDATE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE },

	// These change both shaders so need flushing.
	{ GE_CMD_LIGHTMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE },

	{ GE_CMD_TEXFILTER, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXWRAP, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS | DIRTY_FRAGMENTSHADER_STATE },

	// Uniform changes. though the fragmentshader optimizes based on these sometimes.
	{ GE_CMD_ALPHATEST, FLAG_FLUSHBEFOREONCHANGE, DIRTY_ALPHACOLORREF | DIRTY_ALPHACOLORMASK | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_COLORREF, FLAG_FLUSHBEFOREONCHANGE, DIRTY_ALPHACOLORREF | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_TEXENVCOLOR, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXENV },

	// Simple render state changes. Handled in StateMapping.cpp.
	{ GE_CMD_CULL, FLAG_FLUSHBEFOREONCHANGE, DIRTY_RASTER_STATE },
	{ GE_CMD_CULLFACEENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_RASTER_STATE },
	{ GE_CMD_DITHERENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_RASTER_STATE },
	{ GE_CMD_STENCILOP, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_STENCILTEST, FLAG_FLUSHBEFOREONCHANGE, DIRTY_STENCILREPLACEVALUE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE },
	{ GE_CMD_STENCILTESTENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_ALPHABLENDENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_BLENDMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_BLENDFIXEDA, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_BLENDFIXEDB, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_MASKRGB, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_MASKALPHA, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_ZTEST, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHSTENCIL_STATE },
	{ GE_CMD_ZTESTENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHSTENCIL_STATE },
	{ GE_CMD_ZWRITEDISABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHSTENCIL_STATE },
	{ GE_CMD_LOGICOP, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_LOGICOPENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE },

	{ GE_CMD_TEXMAPMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE },

	// These are read on every SubmitPrim, no need for dirtying or flushing.
	{ GE_CMD_TEXSCALEU },
	{ GE_CMD_TEXSCALEV },
	{ GE_CMD_TEXOFFSETU },
	{ GE_CMD_TEXOFFSETV },

	{ GE_CMD_TEXSIZE0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPUCommon::Execute_TexSize0 },
	{ GE_CMD_TEXSIZE1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXSIZE2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXSIZE3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXSIZE4, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXSIZE5, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXSIZE6, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXSIZE7, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXFORMAT, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_IMAGE },
	{ GE_CMD_TEXLEVEL, FLAG_EXECUTEONCHANGE, DIRTY_TEXTURE_PARAMS, &GPUCommon::Execute_TexLevel },
	{ GE_CMD_TEXLODSLOPE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXADDR0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_IMAGE | DIRTY_UVSCALEOFFSET },
	{ GE_CMD_TEXADDR1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXADDR2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXADDR3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXADDR4, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXADDR5, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXADDR6, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXADDR7, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXBUFWIDTH0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_IMAGE },
	{ GE_CMD_TEXBUFWIDTH1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXBUFWIDTH2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXBUFWIDTH3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXBUFWIDTH4, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXBUFWIDTH5, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXBUFWIDTH6, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_TEXBUFWIDTH7, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS },

	// These must flush on change, so that LoadClut doesn't have to always flush.
	{ GE_CMD_CLUTADDR, FLAG_FLUSHBEFOREONCHANGE },
	{ GE_CMD_CLUTADDRUPPER, FLAG_FLUSHBEFOREONCHANGE },
	{ GE_CMD_CLUTFORMAT, FLAG_FLUSHBEFOREONCHANGE, DIRTY_TEXTURE_PARAMS | DIRTY_DEPAL },

	// Morph weights. TODO: Remove precomputation?
	{ GE_CMD_MORPHWEIGHT0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT4, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT5, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT6, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },
	{ GE_CMD_MORPHWEIGHT7, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_MorphWeight },

	// Control spline/bezier patches. Don't really require flushing as such, but meh.
	{ GE_CMD_PATCHDIVISION, FLAG_FLUSHBEFOREONCHANGE },
	{ GE_CMD_PATCHPRIMITIVE, FLAG_FLUSHBEFOREONCHANGE },
	{ GE_CMD_PATCHFACING, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },
	{ GE_CMD_PATCHCULLENABLE, FLAG_FLUSHBEFOREONCHANGE },

	// Can probably ignore this one as we don't support AA lines.
	{ GE_CMD_ANTIALIASENABLE, FLAG_FLUSHBEFOREONCHANGE },

	// Viewport.
	{ GE_CMD_OFFSETX, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_OFFSETY, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTXSCALE, FLAG_FLUSHBEFOREONCHANGE,  DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTYSCALE, FLAG_FLUSHBEFOREONCHANGE,  DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTXCENTER, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTYCENTER, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTZSCALE, FLAG_FLUSHBEFOREONCHANGE,  DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_DEPTHRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTZCENTER, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_DEPTHRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_CLIPENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE },

	// Z clip
	{ GE_CMD_MINZ, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHRANGE | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_MAXZ, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHRANGE | DIRTY_VIEWPORTSCISSOR_STATE },

	// Region
	{ GE_CMD_REGION1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_REGION2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },

	// Scissor
	{ GE_CMD_SCISSOR1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_SCISSOR2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE },

	// Lighting base colors
	{ GE_CMD_AMBIENTCOLOR, FLAG_FLUSHBEFOREONCHANGE, DIRTY_AMBIENT },
	{ GE_CMD_AMBIENTALPHA, FLAG_FLUSHBEFOREONCHANGE, DIRTY_AMBIENT },
	{ GE_CMD_MATERIALDIFFUSE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_MATDIFFUSE },
	{ GE_CMD_MATERIALEMISSIVE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_MATEMISSIVE },
	{ GE_CMD_MATERIALAMBIENT, FLAG_FLUSHBEFOREONCHANGE, DIRTY_MATAMBIENTALPHA },
	{ GE_CMD_MATERIALALPHA, FLAG_FLUSHBEFOREONCHANGE, DIRTY_MATAMBIENTALPHA },
	{ GE_CMD_MATERIALSPECULAR, FLAG_FLUSHBEFOREONCHANGE, DIRTY_MATSPECULAR },
	{ GE_CMD_MATERIALSPECULARCOEF, FLAG_FLUSHBEFOREONCHANGE, DIRTY_MATSPECULAR },

	// Light parameters
	{ GE_CMD_LX0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LY0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LZ0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LX1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LY1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LZ1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LX2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LY2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LZ2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LX3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LY3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LZ3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },

	{ GE_CMD_LDX0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LDY0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LDZ0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LDX1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LDY1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LDZ1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LDX2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LDY2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LDZ2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LDX3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LDY3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LDZ3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },

	{ GE_CMD_LKA0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LKB0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LKC0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LKA1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LKB1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LKC1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LKA2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LKB2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LKC2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LKA3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LKB3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LKC3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },

	{ GE_CMD_LKS0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LKS1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LKS2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LKS3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },

	{ GE_CMD_LKO0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LKO1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LKO2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LKO3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },

	{ GE_CMD_LAC0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LDC0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LSC0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT0 },
	{ GE_CMD_LAC1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LDC1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LSC1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT1 },
	{ GE_CMD_LAC2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LDC2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LSC2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT2 },
	{ GE_CMD_LAC3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LDC3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },
	{ GE_CMD_LSC3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_LIGHT3 },

	// Ignored commands
	{ GE_CMD_TEXFLUSH, 0 },
	{ GE_CMD_TEXSYNC, 0 },

	// These are just nop or part of other later commands.
	{ GE_CMD_NOP, 0 },
	{ GE_CMD_BASE, 0 },
	{ GE_CMD_TRANSFERSRC, 0 },
	{ GE_CMD_TRANSFERSRCW, 0 },
	{ GE_CMD_TRANSFERDST, 0 },
	{ GE_CMD_TRANSFERDSTW, 0 },
	{ GE_CMD_TRANSFERSRCPOS, 0 },
	{ GE_CMD_TRANSFERDSTPOS, 0 },
	{ GE_CMD_TRANSFERSIZE, 0 },
	{ GE_CMD_TRANSFERSTART, FLAG_FLUSHBEFORE | FLAG_EXECUTE | FLAG_READS_PC, 0, &GPUCommon::Execute_BlockTransferStart },

	// We don't use the dither table.
	{ GE_CMD_DITH0 },
	{ GE_CMD_DITH1 },
	{ GE_CMD_DITH2 },
	{ GE_CMD_DITH3 },

	// These handle their own flushing.
	{ GE_CMD_WORLDMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_WorldMtxNum },
	{ GE_CMD_WORLDMATRIXDATA,   FLAG_EXECUTE, 0, &GPUCommon::Execute_WorldMtxData },
	{ GE_CMD_VIEWMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_ViewMtxNum },
	{ GE_CMD_VIEWMATRIXDATA,    FLAG_EXECUTE, 0, &GPUCommon::Execute_ViewMtxData },
	{ GE_CMD_PROJMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_ProjMtxNum },
	{ GE_CMD_PROJMATRIXDATA,    FLAG_EXECUTE, 0, &GPUCommon::Execute_ProjMtxData },
	{ GE_CMD_TGENMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_TgenMtxNum },
	{ GE_CMD_TGENMATRIXDATA,    FLAG_EXECUTE, 0, &GPUCommon::Execute_TgenMtxData },
	{ GE_CMD_BONEMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_BoneMtxNum },
	{ GE_CMD_BONEMATRIXDATA,    FLAG_EXECUTE, 0, &GPUCommon::Execute_BoneMtxData },

	// Vertex Screen/Texture/Color
	{ GE_CMD_VSCX },
	{ GE_CMD_VSCY },
	{ GE_CMD_VSCZ },
	{ GE_CMD_VTCS },
	{ GE_CMD_VTCT },
	{ GE_CMD_VTCQ },
	{ GE_CMD_VCV },
	{ GE_CMD_VAP, FLAG_EXECUTE, 0, &GPUCommon::Execute_ImmVertexAlphaPrim },
	{ GE_CMD_VFC },
	{ GE_CMD_VSCV },

	// "Missing" commands (gaps in the sequence)
	{ GE_CMD_UNKNOWN_03, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_0D, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_11, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_29, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_34, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_35, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_39, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_4E, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_4F, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_52, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_59, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_5A, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_B6, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_B7, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_D1, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_ED, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_EF, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FA, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FB, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FC, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FD, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	{ GE_CMD_UNKNOWN_FE, FLAG_EXECUTE, 0, &GPUCommon::Execute_Unknown },
	// Appears to be debugging related or something?  Hit a lot in GoW.
	{ GE_CMD_NOP_FF, 0 },
};
size_t commonCommandTableSize = ARRAY_SIZE(commonCommandTable);

// TODO: Make class member?
GPUCommon::CommandInfo GPUCommon::cmdInfo_[256];

void GPUCommon::Flush() {
	drawEngineCommon_->DispatchFlush();
}

GPUCommon::GPUCommon(GraphicsContext *gfxCtx, Draw::DrawContext *draw) :
	dumpNextFrame_(false),
	dumpThisFrame_(false),
	framebufferManager_(nullptr),
	resized_(false),
	gfxCtx_(gfxCtx),
	draw_(draw)
{
	// This assert failed on GCC x86 32-bit (but not MSVC 32-bit!) before adding the
	// "padding" field at the end. This is important for save state compatibility.
	// The compiler was not rounding the struct size up to an 8 byte boundary, which
	// you'd expect due to the int64 field, but the Linux ABI apparently does not require that.
	static_assert(sizeof(DisplayList) == 456, "Bad DisplayList size");

	Reinitialize();
	SetupColorConv();
	gstate.Reset();
	gstate_c.Reset();
	gpuStats.Reset();

	memset(cmdInfo_, 0, sizeof(cmdInfo_));

	// Convert the command table to a faster format, and check for dupes.
	std::set<u8> dupeCheck;
	for (size_t i = 0; i < commonCommandTableSize; i++) {
		const u8 cmd = commonCommandTable[i].cmd;
		if (dupeCheck.find(cmd) != dupeCheck.end()) {
			ERROR_LOG(G3D, "Command table Dupe: %02x (%i)", (int)cmd, (int)cmd);
		} else {
			dupeCheck.insert(cmd);
		}
		cmdInfo_[cmd].flags |= (uint64_t)commonCommandTable[i].flags | (commonCommandTable[i].dirty << 8);
		cmdInfo_[cmd].func = commonCommandTable[i].func;
		if ((cmdInfo_[cmd].flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE)) && !cmdInfo_[cmd].func) {
			// Can't have FLAG_EXECUTE commands without a function pointer to execute.
			Crash();
		}
	}
	// Find commands missing from the table.
	for (int i = 0; i < 0xEF; i++) {
		if (dupeCheck.find((u8)i) == dupeCheck.end()) {
			ERROR_LOG(G3D, "Command missing from table: %02x (%i)", i, i);
		}
	}

	UpdateCmdInfo();
}

GPUCommon::~GPUCommon() {
}

void GPUCommon::UpdateCmdInfo() {
	if (g_Config.bSoftwareSkinning) {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags &= ~FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPUCommon::Execute_VertexTypeSkinning;
	} else {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags |= FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPUCommon::Execute_VertexType;
	}
}

void GPUCommon::BeginHostFrame() {
	ReapplyGfxState();

	// TODO: Assume config may have changed - maybe move to resize.
	gstate_c.Dirty(DIRTY_ALL);
}

void GPUCommon::EndHostFrame() {

}

void GPUCommon::Reinitialize() {
	memset(dls, 0, sizeof(dls));
	for (int i = 0; i < DisplayListMaxCount; ++i) {
		dls[i].state = PSP_GE_DL_STATE_NONE;
		dls[i].waitTicks = 0;
	}

	nextListID = 0;
	currentList = nullptr;
	isbreak = false;
	drawCompleteTicks = 0;
	busyTicks = 0;
	timeSpentStepping_ = 0.0;
	interruptsEnabled_ = true;
}

int GPUCommon::EstimatePerVertexCost() {
	// TODO: This is transform cost, also account for rasterization cost somehow... although it probably
	// runs in parallel with transform.

	// Also, this is all pure guesswork. If we can find a way to do measurements, that would be great.

	// GTA wants a low value to run smooth, GoW wants a high value (otherwise it thinks things
	// went too fast and starts doing all the work over again).

	int cost = 20;
	if (gstate.isLightingEnabled()) {
		cost += 10;

		for (int i = 0; i < 4; i++) {
			if (gstate.isLightChanEnabled(i))
				cost += 10;
		}
	}

	if (gstate.getUVGenMode() != GE_TEXMAP_TEXTURE_COORDS) {
		cost += 20;
	}
	int morphCount = gstate.getNumMorphWeights();
	if (morphCount > 1) {
		cost += 5 * morphCount;
	}
	return cost;
}

void GPUCommon::PopDLQueue() {
	if(!dlQueue.empty()) {
		dlQueue.pop_front();
		if(!dlQueue.empty()) {
			bool running = currentList->state == PSP_GE_DL_STATE_RUNNING;
			currentList = &dls[dlQueue.front()];
			if (running)
				currentList->state = PSP_GE_DL_STATE_RUNNING;
		} else {
			currentList = nullptr;
		}
	}
}

bool GPUCommon::BusyDrawing() {
	u32 state = DrawSync(1);
	if (state == PSP_GE_LIST_DRAWING || state == PSP_GE_LIST_STALLING) {
		if (currentList && currentList->state != PSP_GE_DL_STATE_PAUSED) {
			return true;
		}
	}
	return false;
}

void GPUCommon::Resized() {
	resized_ = true;
}

void GPUCommon::DumpNextFrame() {
	dumpNextFrame_ = true;
}

u32 GPUCommon::DrawSync(int mode) {
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (mode == 0) {
		if (!__KernelIsDispatchEnabled()) {
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt()) {
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}

		if (drawCompleteTicks > CoreTiming::GetTicks()) {
			__GeWaitCurrentThread(GPU_SYNC_DRAW, 1, "GeDrawSync");
		} else {
			for (int i = 0; i < DisplayListMaxCount; ++i) {
				if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
					dls[i].state = PSP_GE_DL_STATE_NONE;
				}
			}
		}
		return 0;
	}

	// If there's no current list, it must be complete.
	DisplayList *top = NULL;
	for (auto it = dlQueue.begin(), end = dlQueue.end(); it != end; ++it) {
		if (dls[*it].state != PSP_GE_DL_STATE_COMPLETED) {
			top = &dls[*it];
			break;
		}
	}
	if (!top || top->state == PSP_GE_DL_STATE_COMPLETED)
		return PSP_GE_LIST_COMPLETED;

	if (currentList->pc == currentList->stall)
		return PSP_GE_LIST_STALLING;

	return PSP_GE_LIST_DRAWING;
}

void GPUCommon::CheckDrawSync() {
	if (dlQueue.empty()) {
		for (int i = 0; i < DisplayListMaxCount; ++i)
			dls[i].state = PSP_GE_DL_STATE_NONE;
	}
}

int GPUCommon::ListSync(int listid, int mode) {
	if (listid < 0 || listid >= DisplayListMaxCount)
		return SCE_KERNEL_ERROR_INVALID_ID;

	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	DisplayList& dl = dls[listid];
	if (mode == 1) {
		switch (dl.state) {
		case PSP_GE_DL_STATE_QUEUED:
			if (dl.interrupted)
				return PSP_GE_LIST_PAUSED;
			return PSP_GE_LIST_QUEUED;

		case PSP_GE_DL_STATE_RUNNING:
			if (dl.pc == dl.stall)
				return PSP_GE_LIST_STALLING;
			return PSP_GE_LIST_DRAWING;

		case PSP_GE_DL_STATE_COMPLETED:
			return PSP_GE_LIST_COMPLETED;

		case PSP_GE_DL_STATE_PAUSED:
			return PSP_GE_LIST_PAUSED;

		default:
			return SCE_KERNEL_ERROR_INVALID_ID;
		}
	}

	if (!__KernelIsDispatchEnabled()) {
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	if (dl.waitTicks > CoreTiming::GetTicks()) {
		__GeWaitCurrentThread(GPU_SYNC_LIST, listid, "GeListSync");
	}
	return PSP_GE_LIST_COMPLETED;
}

int GPUCommon::GetStack(int index, u32 stackPtr) {
	if (!currentList) {
		// Seems like it doesn't return an error code?
		return 0;
	}

	if (currentList->stackptr <= index) {
		return SCE_KERNEL_ERROR_INVALID_INDEX;
	}

	if (index >= 0) {
		auto stack = PSPPointer<u32>::Create(stackPtr);
		if (stack.IsValid()) {
			auto entry = currentList->stack[index];
			// Not really sure what most of these values are.
			stack[0] = 0;
			stack[1] = entry.pc + 4;
			stack[2] = entry.offsetAddr;
			stack[7] = entry.baseAddr;
		}
	}

	return currentList->stackptr;
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head) {
	// TODO Check the stack values in missing arg and ajust the stack depth

	// Check alignment
	// TODO Check the context and stack alignement too
	if (((listpc | stall) & 3) != 0)
		return SCE_KERNEL_ERROR_INVALID_POINTER;

	int id = -1;
	u64 currentTicks = CoreTiming::GetTicks();
	u32_le stackAddr = args.IsValid() ? args->stackAddr : 0;
	// Check compatibility
	if (sceKernelGetCompiledSdkVersion() > 0x01FFFFFF) {
		//numStacks = 0;
		//stack = NULL;
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state != PSP_GE_DL_STATE_NONE && dls[i].state != PSP_GE_DL_STATE_COMPLETED) {
				// Logically, if the CPU has not interrupted yet, it hasn't seen the latest pc either.
				// Exit enqueues right after an END, which fails without ignoring pendingInterrupt lists.
				if (dls[i].pc == listpc && !dls[i].pendingInterrupt) {
					ERROR_LOG(G3D, "sceGeListEnqueue: can't enqueue, list address %08X already used", listpc);
					return 0x80000021;
				} else if (stackAddr != 0 && dls[i].stackAddr == stackAddr && !dls[i].pendingInterrupt) {
					ERROR_LOG(G3D, "sceGeListEnqueue: can't enqueue, stack address %08X already used", stackAddr);
					return 0x80000021;
				}
			}
		}
	}
	// TODO Check if list stack dls[i].stack already used then return 0x80000021 as above

	for (int i = 0; i < DisplayListMaxCount; ++i) {
		int possibleID = (i + nextListID) % DisplayListMaxCount;
		auto possibleList = dls[possibleID];
		if (possibleList.pendingInterrupt) {
			continue;
		}

		if (possibleList.state == PSP_GE_DL_STATE_NONE) {
			id = possibleID;
			break;
		}
		if (possibleList.state == PSP_GE_DL_STATE_COMPLETED && possibleList.waitTicks < currentTicks) {
			id = possibleID;
		}
	}
	if (id < 0) {
		ERROR_LOG_REPORT(G3D, "No DL ID available to enqueue");
		for (auto it = dlQueue.begin(); it != dlQueue.end(); ++it) {
			DisplayList &dl = dls[*it];
			DEBUG_LOG(G3D, "DisplayList %d status %d pc %08x stall %08x", *it, dl.state, dl.pc, dl.stall);
		}
		return SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	}
	nextListID = id + 1;

	DisplayList &dl = dls[id];
	dl.id = id;
	dl.startpc = listpc & 0x0FFFFFFF;
	dl.pc = listpc & 0x0FFFFFFF;
	dl.stall = stall & 0x0FFFFFFF;
	dl.subIntrBase = std::max(subIntrBase, -1);
	dl.stackptr = 0;
	dl.signal = PSP_GE_SIGNAL_NONE;
	dl.interrupted = false;
	dl.waitTicks = (u64)-1;
	dl.interruptsEnabled = interruptsEnabled_;
	dl.started = false;
	dl.offsetAddr = 0;
	dl.bboxResult = false;
	dl.stackAddr = stackAddr;

	if (args.IsValid() && args->context.IsValid())
		dl.context = args->context;
	else
		dl.context = 0;

	if (head) {
		if (currentList) {
			if (currentList->state != PSP_GE_DL_STATE_PAUSED)
				return SCE_KERNEL_ERROR_INVALID_VALUE;
			currentList->state = PSP_GE_DL_STATE_QUEUED;
		}

		dl.state = PSP_GE_DL_STATE_PAUSED;

		currentList = &dl;
		dlQueue.push_front(id);
	} else if (currentList) {
		dl.state = PSP_GE_DL_STATE_QUEUED;
		dlQueue.push_back(id);
	} else {
		dl.state = PSP_GE_DL_STATE_RUNNING;
		currentList = &dl;
		dlQueue.push_front(id);

		drawCompleteTicks = (u64)-1;

		// TODO save context when starting the list if param is set
		ProcessDLQueue();
	}

	return id;
}

u32 GPUCommon::DequeueList(int listid) {
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;

	auto &dl = dls[listid];
	if (dl.started)
		return SCE_KERNEL_ERROR_BUSY;

	dl.state = PSP_GE_DL_STATE_NONE;

	if (listid == dlQueue.front())
		PopDLQueue();
	else
		dlQueue.remove(listid);

	dl.waitTicks = 0;
	__GeTriggerWait(GPU_SYNC_LIST, listid);

	CheckDrawSync();

	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall) {
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;
	auto &dl = dls[listid];
	if (dl.state == PSP_GE_DL_STATE_COMPLETED)
		return SCE_KERNEL_ERROR_ALREADY;

	dl.stall = newstall & 0x0FFFFFFF;
	
	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Continue() {
	if (!currentList)
		return 0;

	if (currentList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (!isbreak)
		{
			// TODO: Supposedly this returns SCE_KERNEL_ERROR_BUSY in some case, previously it had
			// currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE, but it doesn't reproduce.

			currentList->state = PSP_GE_DL_STATE_RUNNING;
			currentList->signal = PSP_GE_SIGNAL_NONE;

			// TODO Restore context of DL is necessary
			// TODO Restore BASE

			// We have a list now, so it's not complete.
			drawCompleteTicks = (u64)-1;
		}
		else
			currentList->state = PSP_GE_DL_STATE_QUEUED;
	}
	else if (currentList->state == PSP_GE_DL_STATE_RUNNING)
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000020;
		return -1;
	}
	else
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000004;
		return -1;
	}

	ProcessDLQueue();
	return 0;
}

u32 GPUCommon::Break(int mode) {
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (!currentList)
		return SCE_KERNEL_ERROR_ALREADY;

	if (mode == 1)
	{
		// Clear the queue
		dlQueue.clear();
		for (int i = 0; i < DisplayListMaxCount; ++i)
		{
			dls[i].state = PSP_GE_DL_STATE_NONE;
			dls[i].signal = PSP_GE_SIGNAL_NONE;
		}

		nextListID = 0;
		currentList = NULL;
		return 0;
	}

	if (currentList->state == PSP_GE_DL_STATE_NONE || currentList->state == PSP_GE_DL_STATE_COMPLETED)
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000004;
		return -1;
	}

	if (currentList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (sceKernelGetCompiledSdkVersion() > 0x02000010)
		{
			if (currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
			{
				ERROR_LOG_REPORT(G3D, "sceGeBreak: can't break signal-pausing list");
			}
			else
				return SCE_KERNEL_ERROR_ALREADY;
		}
		return SCE_KERNEL_ERROR_BUSY;
	}

	if (currentList->state == PSP_GE_DL_STATE_QUEUED)
	{
		currentList->state = PSP_GE_DL_STATE_PAUSED;
		return currentList->id;
	}

	// TODO Save BASE
	// TODO Adjust pc to be just before SIGNAL/END

	// TODO: Is this right?
	if (currentList->signal == PSP_GE_SIGNAL_SYNC)
		currentList->pc += 8;

	currentList->interrupted = true;
	currentList->state = PSP_GE_DL_STATE_PAUSED;
	currentList->signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;
	isbreak = true;

	return currentList->id;
}

void GPUCommon::NotifySteppingEnter() {
	if (coreCollectDebugStats) {
		time_update();
		timeSteppingStarted_ = time_now_d();
	}
}
void GPUCommon::NotifySteppingExit() {
	if (coreCollectDebugStats) {
		if (timeSteppingStarted_ <= 0.0) {
			ERROR_LOG(G3D, "Mismatched stepping enter/exit.");
		}
		time_update();
		timeSpentStepping_ += time_now_d() - timeSteppingStarted_;
		timeSteppingStarted_ = 0.0;
	}
}

bool GPUCommon::InterpretList(DisplayList &list) {
	// Initialized to avoid a race condition with bShowDebugStats changing.
	double start = 0.0;
	if (coreCollectDebugStats) {
		time_update();
		start = time_now_d();
	}

	if (list.state == PSP_GE_DL_STATE_PAUSED)
		return false;
	currentList = &list;

	if (!list.started && list.context.IsValid()) {
		gstate.Save(list.context);
	}
	list.started = true;

	gstate_c.offsetAddr = list.offsetAddr;

	if (!Memory::IsValidAddress(list.pc)) {
		ERROR_LOG_REPORT(G3D, "DL PC = %08x WTF!!!!", list.pc);
		return true;
	}

	cycleLastPC = list.pc;
	cyclesExecuted += 60;
	downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;
	list.state = PSP_GE_DL_STATE_RUNNING;
	list.interrupted = false;

	gpuState = list.pc == list.stall ? GPUSTATE_STALL : GPUSTATE_RUNNING;

	debugRecording_ = GPURecord::IsActive();
	const bool useDebugger = host->GPUDebuggingActive() || debugRecording_;
	const bool useFastRunLoop = !dumpThisFrame_ && !useDebugger;
	while (gpuState == GPUSTATE_RUNNING) {
		{
			if (list.pc == list.stall) {
				gpuState = GPUSTATE_STALL;
				downcount = 0;
			}
		}

		if (useFastRunLoop) {
			FastRunLoop(list);
		} else {
			SlowRunLoop(list);
		}

		{
			downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;

			if (gpuState == GPUSTATE_STALL && list.stall != list.pc) {
				// Unstalled.
				gpuState = GPUSTATE_RUNNING;
			}
		}
	}

	FinishDeferred();

	// We haven't run the op at list.pc, so it shouldn't count.
	if (cycleLastPC != list.pc) {
		UpdatePC(list.pc - 4, list.pc);
	}

	list.offsetAddr = gstate_c.offsetAddr;

	if (coreCollectDebugStats) {
		time_update();
		double total = time_now_d() - start - timeSpentStepping_;
		hleSetSteppingTime(timeSpentStepping_);
		timeSpentStepping_ = 0.0;
		gpuStats.msProcessingDisplayLists += total;
	}
	return gpuState == GPUSTATE_DONE || gpuState == GPUSTATE_ERROR;
}

// Maybe should write this in ASM...
void GPUCommon::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("gpuloop");
	const CommandInfo *cmdInfo = cmdInfo_;
	int dc = downcount;
	for (; dc > 0; --dc) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer
		const u32 op = *(const u32 *)(Memory::base + list.pc);
		const u32 cmd = op >> 24;
		const CommandInfo &info = cmdInfo[cmd];
		const u32 diff = op ^ gstate.cmdmem[cmd];
		if (diff == 0) {
			if (info.flags & FLAG_EXECUTE) {
				downcount = dc;
				(this->*info.func)(op, diff);
				dc = downcount;
			}
		} else {
			uint64_t flags = info.flags;
			if (flags & FLAG_FLUSHBEFOREONCHANGE) {
				if (drawEngineCommon_->GetNumDrawCalls()) {
					drawEngineCommon_->DispatchFlush();
				}
			}
			gstate.cmdmem[cmd] = op;
			if (flags & (FLAG_EXECUTE | FLAG_EXECUTEONCHANGE)) {
				downcount = dc;
				(this->*info.func)(op, diff);
				dc = downcount;
			} else {
				uint64_t dirty = flags >> 8;
				if (dirty)
					gstate_c.Dirty(dirty);
			}
		}
		list.pc += 4;
	}
	downcount = 0;
}

void GPUCommon::BeginFrame() {
	immCount_ = 0;
	if (dumpNextFrame_) {
		NOTICE_LOG(G3D, "DUMPING THIS FRAME");
		dumpThisFrame_ = true;
		dumpNextFrame_ = false;
	} else if (dumpThisFrame_) {
		dumpThisFrame_ = false;
	}
	GPURecord::NotifyFrame();
}

void GPUCommon::SlowRunLoop(DisplayList &list)
{
	const bool dumpThisFrame = dumpThisFrame_;
	while (downcount > 0)
	{
		host->GPUNotifyCommand(list.pc);
		GPURecord::NotifyCommand(list.pc);
		u32 op = Memory::ReadUnchecked_U32(list.pc);
		u32 cmd = op >> 24;

		u32 diff = op ^ gstate.cmdmem[cmd];
		PreExecuteOp(op, diff);
		if (dumpThisFrame) {
			char temp[256];
			u32 prev;
			if (Memory::IsValidAddress(list.pc - 4)) {
				prev = Memory::ReadUnchecked_U32(list.pc - 4);
			} else {
				prev = 0;
			}
			GeDisassembleOp(list.pc, op, prev, temp, 256);
			NOTICE_LOG(G3D, "%08x: %s", op, temp);
		}
		gstate.cmdmem[cmd] = op;

		ExecuteOp(op, diff);

		list.pc += 4;
		--downcount;
	}
}

// The newPC parameter is used for jumps, we don't count cycles between.
void GPUCommon::UpdatePC(u32 currentPC, u32 newPC) {
	// Rough estimate, 2 CPU ticks (it's double the clock rate) per GPU instruction.
	u32 executed = (currentPC - cycleLastPC) / 4;
	cyclesExecuted += 2 * executed;
	cycleLastPC = newPC;

	if (coreCollectDebugStats) {
		gpuStats.otherGPUCycles += 2 * executed;
		gpuStats.gpuCommandsAtCallLevel[std::min(currentList->stackptr, 3)] += executed;
	}

	// Exit the runloop and recalculate things.  This happens a lot in some games.
	if (currentList)
		downcount = currentList->stall == 0 ? 0x0FFFFFFF : (currentList->stall - newPC) / 4;
	else
		downcount = 0;
}

void GPUCommon::ReapplyGfxState() {
	// The commands are embedded in the command memory so we can just reexecute the words. Convenient.
	// To be safe we pass 0xFFFFFFFF as the diff.

	for (int i = GE_CMD_VERTEXTYPE; i < GE_CMD_BONEMATRIXNUMBER; i++) {
		if (i != GE_CMD_ORIGIN && i != GE_CMD_OFFSETADDR) {
			ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
		}
	}

	// Can't write to bonematrixnumber here

	for (int i = GE_CMD_MORPHWEIGHT0; i <= GE_CMD_PATCHFACING; i++) {
		ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
	}

	// There are a few here in the middle that we shouldn't execute...

	for (int i = GE_CMD_VIEWPORTXSCALE; i < GE_CMD_TRANSFERSTART; i++) {
		ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
	}

	// Let's just skip the transfer size stuff, it's just values.
}

inline void GPUCommon::UpdateState(GPURunState state) {
	gpuState = state;
	if (state != GPUSTATE_RUNNING)
		downcount = 0;
}

int GPUCommon::GetNextListIndex() {
	auto iter = dlQueue.begin();
	if (iter != dlQueue.end()) {
		return *iter;
	} else {
		return -1;
	}
}

void GPUCommon::ProcessDLQueue() {
	startingTicks = CoreTiming::GetTicks();
	cyclesExecuted = 0;

	// Seems to be correct behaviour to process the list anyway?
	if (startingTicks < busyTicks) {
		DEBUG_LOG(G3D, "Can't execute a list yet, still busy for %lld ticks", busyTicks - startingTicks);
		//return;
	}

	for (int listIndex = GetNextListIndex(); listIndex != -1; listIndex = GetNextListIndex()) {
		DisplayList &l = dls[listIndex];
		DEBUG_LOG(G3D, "Starting DL execution at %08x - stall = %08x", l.pc, l.stall);
		if (!InterpretList(l)) {
			return;
		} else {
			// Some other list could've taken the spot while we dilly-dallied around.
			if (l.state != PSP_GE_DL_STATE_QUEUED) {
				// At the end, we can remove it from the queue and continue.
				dlQueue.erase(std::remove(dlQueue.begin(), dlQueue.end(), listIndex), dlQueue.end());
			}
		}
	}

	currentList = nullptr;

	drawCompleteTicks = startingTicks + cyclesExecuted;
	busyTicks = std::max(busyTicks, drawCompleteTicks);
	__GeTriggerSync(GPU_SYNC_DRAW, 1, drawCompleteTicks);
	// Since the event is in CoreTiming, we're in sync.  Just set 0 now.
}

void GPUCommon::PreExecuteOp(u32 op, u32 diff) {
	// Nothing to do
}

void GPUCommon::Execute_OffsetAddr(u32 op, u32 diff) {
	gstate_c.offsetAddr = op << 8;
}

void GPUCommon::Execute_Vaddr(u32 op, u32 diff) {
	gstate_c.vertexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void GPUCommon::Execute_Iaddr(u32 op, u32 diff) {
	gstate_c.indexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void GPUCommon::Execute_Origin(u32 op, u32 diff) {
	gstate_c.offsetAddr = currentList->pc;
}

void GPUCommon::Execute_Jump(u32 op, u32 diff) {
	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
#ifdef _DEBUG
	if (!Memory::IsValidAddress(target)) {
		ERROR_LOG_REPORT(G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		return;
	}
#endif
	UpdatePC(currentList->pc, target - 4);
	currentList->pc = target - 4; // pc will be increased after we return, counteract that
}

void GPUCommon::Execute_BJump(u32 op, u32 diff) {
	if (!currentList->bboxResult) {
		// bounding box jump.
		const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
		if (Memory::IsValidAddress(target)) {
			UpdatePC(currentList->pc, target - 4);
			currentList->pc = target - 4; // pc will be increased after we return, counteract that
		} else {
			ERROR_LOG_REPORT(G3D, "BJUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		}
	}
}

void GPUCommon::Execute_Call(u32 op, u32 diff) {
	PROFILE_THIS_SCOPE("gpu_call");

	// Saint Seiya needs correct support for relative calls.
	const u32 retval = currentList->pc + 4;
	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
#ifdef _DEBUG
	if (!Memory::IsValidAddress(target)) {
		ERROR_LOG_REPORT(G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		return;
	}
#endif

	// Bone matrix optimization - many games will CALL a bone matrix (!).
	// We don't optimize during recording - so the matrix data gets recorded.
	if (!debugRecording_ && (Memory::ReadUnchecked_U32(target) >> 24) == GE_CMD_BONEMATRIXDATA) {
		// Check for the end
		if ((Memory::ReadUnchecked_U32(target + 11 * 4) >> 24) == GE_CMD_BONEMATRIXDATA &&
				(Memory::ReadUnchecked_U32(target + 12 * 4) >> 24) == GE_CMD_RET) {
			// Yep, pretty sure this is a bone matrix call.  Double check stall first.
			if (target > currentList->stall || target + 12 * 4 < currentList->stall) {
				FastLoadBoneMatrix(target);
				return;
			}
		}
	}

	if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
		ERROR_LOG_REPORT(G3D, "CALL: Stack full!");
	} else {
		auto &stackEntry = currentList->stack[currentList->stackptr++];
		stackEntry.pc = retval;
		stackEntry.offsetAddr = gstate_c.offsetAddr;
		// The base address is NOT saved/restored for a regular call.
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4;	// pc will be increased after we return, counteract that
	}
}

void GPUCommon::Execute_Ret(u32 op, u32 diff) {
	if (currentList->stackptr == 0) {
		DEBUG_LOG_REPORT(G3D, "RET: Stack empty!");
	} else {
		auto &stackEntry = currentList->stack[--currentList->stackptr];
		gstate_c.offsetAddr = stackEntry.offsetAddr;
		// We always clear the top (uncached/etc.) bits
		const u32 target = stackEntry.pc & 0x0FFFFFFF;
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4;
#ifdef _DEBUG
		if (!Memory::IsValidAddress(currentList->pc)) {
			ERROR_LOG_REPORT(G3D, "Invalid DL PC %08x on return", currentList->pc);
			UpdateState(GPUSTATE_ERROR);
		}
#endif
	}
}

void GPUCommon::Execute_End(u32 op, u32 diff) {
	Flush();

	const u32 prev = Memory::ReadUnchecked_U32(currentList->pc - 4);
	UpdatePC(currentList->pc, currentList->pc);
	// Count in a few extra cycles on END.
	cyclesExecuted += 60;

	switch (prev >> 24) {
	case GE_CMD_SIGNAL:
		{
			// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
			SignalBehavior behaviour = static_cast<SignalBehavior>((prev >> 16) & 0xFF);
			const int signal = prev & 0xFFFF;
			const int enddata = op & 0xFFFF;
			bool trigger = true;
			currentList->subIntrToken = signal;

			switch (behaviour) {
			case PSP_GE_SIGNAL_HANDLER_SUSPEND:
				// Suspend the list, and call the signal handler.  When it's done, resume.
				// Before sdkver 0x02000010, listsync should return paused.
				if (sceKernelGetCompiledSdkVersion() <= 0x02000010)
					currentList->state = PSP_GE_DL_STATE_PAUSED;
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal with wait. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_HANDLER_CONTINUE:
				// Resume the list right away, then call the handler.
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_HANDLER_PAUSE:
				// Pause the list instead of ending at the next FINISH.
				// Call the handler with the PAUSE signal value at that FINISH.
				// Technically, this ought to trigger an interrupt, but it won't do anything.
				// But right now, signal is always reset by interrupts, so that causes pause to not work.
				trigger = false;
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal with Pause. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_SYNC:
				// Acts as a memory barrier, never calls any user code.
				// Technically, this ought to trigger an interrupt, but it won't do anything.
				// Triggering here can cause incorrect rescheduling, which breaks 3rd Birthday.
				// However, this is likely a bug in how GE signal interrupts are handled.
				trigger = false;
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal with Sync. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_JUMP:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = (((signal << 16) | enddata) & 0xFFFFFFFC) - 4;
					if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(G3D, "Signal with Jump: bad address. signal/end: %04x %04x", signal, enddata);
					} else {
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(G3D, "Signal with Jump. signal/end: %04x %04x", signal, enddata);
					}
				}
				break;
			case PSP_GE_SIGNAL_CALL:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = (((signal << 16) | enddata) & 0xFFFFFFFC) - 4;
					if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
						ERROR_LOG_REPORT(G3D, "Signal with Call: stack full. signal/end: %04x %04x", signal, enddata);
					} else if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(G3D, "Signal with Call: bad address. signal/end: %04x %04x", signal, enddata);
					} else {
						// TODO: This might save/restore other state...
						auto &stackEntry = currentList->stack[currentList->stackptr++];
						stackEntry.pc = currentList->pc;
						stackEntry.offsetAddr = gstate_c.offsetAddr;
						stackEntry.baseAddr = gstate.base;
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(G3D, "Signal with Call. signal/end: %04x %04x", signal, enddata);
					}
				}
				break;
			case PSP_GE_SIGNAL_RET:
				{
					trigger = false;
					currentList->signal = behaviour;
					if (currentList->stackptr == 0) {
						ERROR_LOG_REPORT(G3D, "Signal with Return: stack empty. signal/end: %04x %04x", signal, enddata);
					} else {
						// TODO: This might save/restore other state...
						auto &stackEntry = currentList->stack[--currentList->stackptr];
						gstate_c.offsetAddr = stackEntry.offsetAddr;
						gstate.base = stackEntry.baseAddr;
						UpdatePC(currentList->pc, stackEntry.pc);
						currentList->pc = stackEntry.pc;
						DEBUG_LOG(G3D, "Signal with Return. signal/end: %04x %04x", signal, enddata);
					}
				}
				break;
			default:
				ERROR_LOG_REPORT(G3D, "UNKNOWN Signal UNIMPLEMENTED %i ! signal/end: %04x %04x", behaviour, signal, enddata);
				break;
			}
			// TODO: Technically, jump/call/ret should generate an interrupt, but before the pc change maybe?
			if (currentList->interruptsEnabled && trigger) {
				if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
					currentList->pendingInterrupt = true;
					UpdateState(GPUSTATE_INTERRUPT);
				}
			}
		}
		break;
	case GE_CMD_FINISH:
		switch (currentList->signal) {
		case PSP_GE_SIGNAL_HANDLER_PAUSE:
			currentList->state = PSP_GE_DL_STATE_PAUSED;
			if (currentList->interruptsEnabled) {
				if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
					currentList->pendingInterrupt = true;
					UpdateState(GPUSTATE_INTERRUPT);
				}
			}
			break;

		case PSP_GE_SIGNAL_SYNC:
			currentList->signal = PSP_GE_SIGNAL_NONE;
			// TODO: Technically this should still cause an interrupt.  Probably for memory sync.
			break;

		default:
			currentList->subIntrToken = prev & 0xFFFF;
			UpdateState(GPUSTATE_DONE);
			if (currentList->interruptsEnabled && __GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
				currentList->pendingInterrupt = true;
			} else {
				currentList->state = PSP_GE_DL_STATE_COMPLETED;
				currentList->waitTicks = startingTicks + cyclesExecuted;
				busyTicks = std::max(busyTicks, currentList->waitTicks);
				__GeTriggerSync(GPU_SYNC_LIST, currentList->id, currentList->waitTicks);
				if (currentList->started && currentList->context.IsValid()) {
					gstate.Restore(currentList->context);
					ReapplyGfxState();
				}
			}
			break;
		}
		break;
	default:
		DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
		break;
	}
}

void GPUCommon::Execute_TexLevel(u32 op, u32 diff) {
	// TODO: If you change the rules here, don't forget to update the inner interpreter in Execute_Prim.
	if (diff == 0xFFFFFFFF)
		return;
	gstate.texlevel ^= diff;
	if (gstate.getTexLevelMode() != GE_TEXLEVEL_MODE_AUTO && (0x00FF0000 & gstate.texlevel) != 0) {
		Flush();
	}
	gstate.texlevel ^= diff;
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS | DIRTY_FRAGMENTSHADER_STATE);
}

void GPUCommon::Execute_TexSize0(u32 op, u32 diff) {
	// Render to texture may have overridden the width/height.
	// Don't reset it unless the size is different / the texture has changed.
	if (diff || gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS)) {
		gstate_c.curTextureWidth = gstate.getTextureWidth(0);
		gstate_c.curTextureHeight = gstate.getTextureHeight(0);
		gstate_c.Dirty(DIRTY_UVSCALEOFFSET);
		// We will need to reset the texture now.
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	}
}

void GPUCommon::Execute_VertexType(u32 op, u32 diff) {
	if (diff)
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
	if (diff & (GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK)) {
		gstate_c.Dirty(DIRTY_UVSCALEOFFSET);
		if (diff & GE_VTYPE_THROUGH_MASK)
			gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_FRAGMENTSHADER_STATE);
	}
}

void GPUCommon::Execute_LoadClut(u32 op, u32 diff) {
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	textureCache_->LoadClut(gstate.getClutAddress(), gstate.getClutLoadBytes());
}

void GPUCommon::Execute_VertexTypeSkinning(u32 op, u32 diff) {
	// Don't flush when weight count changes.
	if (diff & ~GE_VTYPE_WEIGHTCOUNT_MASK) {
		// Restore and flush
		gstate.vertType ^= diff;
		Flush();
		gstate.vertType ^= diff;
		if (diff & (GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK))
			gstate_c.Dirty(DIRTY_UVSCALEOFFSET);
		// In this case, we may be doing weights and morphs.
		// Update any bone matrix uniforms so it uses them correctly.
		if ((op & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
			gstate_c.Dirty(gstate_c.deferredVertTypeDirty);
			gstate_c.deferredVertTypeDirty = 0;
		}
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
	}
	if (diff & GE_VTYPE_THROUGH_MASK)
		gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_FRAGMENTSHADER_STATE);
}


void GPUCommon::Execute_Prim(u32 op, u32 diff) {
	// This drives all drawing. All other state we just buffer up, then we apply it only
	// when it's time to draw. As most PSP games set state redundantly ALL THE TIME, this is a huge optimization.

	PROFILE_THIS_SCOPE("execprim");

	u32 data = op & 0xFFFFFF;
	u32 count = data & 0xFFFF;
	if (count == 0)
		return;

	// Upper bits are ignored.
	GEPrimitiveType prim = static_cast<GEPrimitiveType>((data >> 16) & 7);
	SetDrawType(DRAW_PRIM, prim);

	// Discard AA lines as we can't do anything that makes sense with these anyway. The SW plugin might, though.
	if (gstate.isAntiAliasEnabled()) {
		// Discard AA lines in DOA
		if (prim == GE_PRIM_LINE_STRIP)
			return;
		// Discard AA lines in Summon Night 5
		if ((prim == GE_PRIM_LINES) && gstate.isSkinningEnabled())
			return;
	}

	// This also makes skipping drawing very effective.
	framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);

	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// Rough estimate, not sure what's correct.
		cyclesExecuted += EstimatePerVertexCost() * count;
		if (gstate.isModeClear()) {
			gpuStats.numClears++;
		}
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *inds = 0;
	u32 vertexType = gstate.vertType;
	if ((vertexType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		u32 indexAddr = gstate_c.indexAddr;
		if (!Memory::IsValidAddress(indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", indexAddr);
			return;
		}
		inds = Memory::GetPointerUnchecked(indexAddr);
	}

#ifndef MOBILE_DEVICE
	if (prim > GE_PRIM_RECTANGLES) {
		ERROR_LOG_REPORT_ONCE(reportPrim, G3D, "Unexpected prim type: %d", prim);
	}
#endif

	if (gstate_c.dirty & DIRTY_VERTEXSHADER_STATE) {
		vertexCost_ = EstimatePerVertexCost();
	}

	int bytesRead = 0;
	UpdateUVScaleOffset();

	uint32_t vertTypeID = GetVertTypeID(vertexType, gstate.getUVGenMode());
	drawEngineCommon_->SubmitPrim(verts, inds, prim, count, vertTypeID, &bytesRead);
	// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	AdvanceVerts(vertexType, count, bytesRead);
	int totalVertCount = count;

	// PRIMs are often followed by more PRIMs. Save some work and submit them immediately.
	const u32 *src = (const u32 *)Memory::GetPointerUnchecked(currentList->pc + 4);
	const u32 *stall = currentList->stall ? (const u32 *)Memory::GetPointerUnchecked(currentList->stall) : 0;
	int cmdCount = 0;

	// Optimized submission of sequences of PRIM. Allows us to avoid going through all the mess
	// above for each one. This can be expanded to support additional games that intersperse
	// PRIM commands with other commands. A special case that might be interesting is that game
	// that changes culling mode between each prim, we could just change the triangle winding
	// right here to still be able to join draw calls.

	uint32_t vtypeCheckMask = ~GE_VTYPE_WEIGHTCOUNT_MASK;
	if (!g_Config.bSoftwareSkinning)
		vtypeCheckMask = 0xFFFFFFFF;

#ifndef MOBILE_DEVICE
	if (debugRecording_ || host->GPUDebuggingActive())
		goto bail;
#endif

	while (src != stall) {
		uint32_t data = *src;
		switch (data >> 24) {
		case GE_CMD_PRIM:
		{
			u32 count = data & 0xFFFF;
			if (count == 0) {
				// Ignore.
				break;
			}

			GEPrimitiveType newPrim = static_cast<GEPrimitiveType>((data >> 16) & 7);
			// TODO: more efficient updating of verts/inds
			verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
			inds = 0;
			if ((vertexType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
			}

			drawEngineCommon_->SubmitPrim(verts, inds, newPrim, count, vertTypeID, &bytesRead);
			AdvanceVerts(vertexType, count, bytesRead);
			totalVertCount += count;
			break;
		}
		case GE_CMD_VERTEXTYPE:
		{
			uint32_t diff = data ^ vertexType;
			// don't mask upper bits, vertexType is unmasked
			if (diff & vtypeCheckMask) {
				goto bail;
			} else {
				vertexType = data;
				vertTypeID = GetVertTypeID(vertexType, gstate.getUVGenMode());
			}
			break;
		}
		case GE_CMD_VADDR:
			gstate_c.vertexAddr = gstate_c.getRelativeAddress(data & 0x00FFFFFF);
			break;
		case GE_CMD_OFFSETADDR:
			gstate.cmdmem[GE_CMD_OFFSETADDR] = data;
			gstate_c.offsetAddr = data << 8;
			break;
		case GE_CMD_BASE:
			gstate.cmdmem[GE_CMD_BASE] = data;
			break;
		case GE_CMD_NOP:
		case GE_CMD_NOP_FF:
			break;
		case GE_CMD_BONEMATRIXNUMBER:
			gstate.cmdmem[GE_CMD_BONEMATRIXNUMBER] = data;
			break;
		case GE_CMD_TEXSCALEU:
			gstate.cmdmem[GE_CMD_TEXSCALEU] = data;
			gstate_c.uv.uScale = getFloat24(data);
			break;
		case GE_CMD_TEXSCALEV:
			gstate.cmdmem[GE_CMD_TEXSCALEV] = data;
			gstate_c.uv.vScale = getFloat24(data);
			break;
		case GE_CMD_TEXLEVEL:
			// Same Gran Turismo hack from Execute_TexLevel
			if ((data & 3) != GE_TEXLEVEL_MODE_AUTO && (0x00FF0000 & data) != 0) {
				goto bail;
			}
			gstate.cmdmem[GE_CMD_TEXLEVEL] = data;
			break;
		case GE_CMD_CALL:
		{
			// A bone matrix probably. If not we bail.
			const u32 target = gstate_c.getRelativeAddress(data & 0x00FFFFFC);
			if ((Memory::ReadUnchecked_U32(target) >> 24) == GE_CMD_BONEMATRIXDATA &&
				(Memory::ReadUnchecked_U32(target + 11 * 4) >> 24) == GE_CMD_BONEMATRIXDATA &&
				(Memory::ReadUnchecked_U32(target + 12 * 4) >> 24) == GE_CMD_RET &&
				(target > currentList->stall || target + 12 * 4 < currentList->stall)) {
				FastLoadBoneMatrix(target);
			} else {
				goto bail;
			}
			break;
		}

		default:
			// All other commands might need a flush or something, stop this inner loop.
			goto bail;
		}
		cmdCount++;
		src++;
	}

bail:
	gstate.cmdmem[GE_CMD_VERTEXTYPE] = vertexType;
	// Skip over the commands we just read out manually.
	if (cmdCount > 0) {
		UpdatePC(currentList->pc, currentList->pc + cmdCount * 4);
		currentList->pc += cmdCount * 4;
	}

	gpuStats.vertexGPUCycles += vertexCost_ * totalVertCount;
	cyclesExecuted += vertexCost_ * totalVertCount;
}

void GPUCommon::Execute_Bezier(u32 op, u32 diff) {
	drawEngineCommon_->DispatchFlush();

	// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	// This also make skipping drawing very effective.
	framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if (vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(G3D, "Unusual bezier/spline vtype: %08x, morph: %d, bones: %d", gstate.vertType, (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT, vertTypeGetNumBoneWeights(gstate.vertType));
	}

	GEPatchPrimType patchPrim = gstate.getPatchPrimitiveType();
	SetDrawType(DRAW_BEZIER, PatchPrimToPrim(patchPrim));

	int bz_ucount = op & 0xFF;
	int bz_vcount = (op >> 8) & 0xFF;
	bool computeNormals = gstate.isLightingEnabled();
	bool patchFacing = gstate.patchfacing & 1;

	if (g_Config.bHardwareTessellation && g_Config.bHardwareTransform && !g_Config.bSoftwareRendering) {
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
		gstate_c.bezier = true;
		if (gstate_c.spline_count_u != bz_ucount) {
			gstate_c.Dirty(DIRTY_BEZIERSPLINE);
			gstate_c.spline_count_u = bz_ucount;
		}
	}

	int bytesRead = 0;
	UpdateUVScaleOffset();
	drawEngineCommon_->SubmitBezier(control_points, indices, gstate.getPatchDivisionU(), gstate.getPatchDivisionV(), bz_ucount, bz_vcount, patchPrim, computeNormals, patchFacing, gstate.vertType, &bytesRead);

	if (gstate_c.bezier)
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
	gstate_c.bezier = false;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = bz_ucount * bz_vcount;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPUCommon::Execute_Spline(u32 op, u32 diff) {
	drawEngineCommon_->DispatchFlush();

	// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	// This also make skipping drawing very effective.
	framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if (vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(G3D, "Unusual bezier/spline vtype: %08x, morph: %d, bones: %d", gstate.vertType, (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT, vertTypeGetNumBoneWeights(gstate.vertType));
	}

	int sp_ucount = op & 0xFF;
	int sp_vcount = (op >> 8) & 0xFF;
	int sp_utype = (op >> 16) & 0x3;
	int sp_vtype = (op >> 18) & 0x3;
	GEPatchPrimType patchPrim = gstate.getPatchPrimitiveType();
	SetDrawType(DRAW_SPLINE, PatchPrimToPrim(patchPrim));
	bool computeNormals = gstate.isLightingEnabled();
	bool patchFacing = gstate.patchfacing & 1;
	u32 vertType = gstate.vertType;

	if (g_Config.bHardwareTessellation && g_Config.bHardwareTransform && !g_Config.bSoftwareRendering) {
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
		gstate_c.spline = true;
		bool countsChanged = gstate_c.spline_count_u != sp_ucount || gstate_c.spline_count_v != sp_vcount;
		bool typesChanged = gstate_c.spline_type_u != sp_utype || gstate_c.spline_type_v != sp_vtype;
		if (countsChanged || typesChanged) {
			gstate_c.Dirty(DIRTY_BEZIERSPLINE);
			gstate_c.spline_count_u = sp_ucount;
			gstate_c.spline_count_v = sp_vcount;
			gstate_c.spline_type_u = sp_utype;
			gstate_c.spline_type_v = sp_vtype;
		}
	}

	int bytesRead = 0;
	UpdateUVScaleOffset();
	drawEngineCommon_->SubmitSpline(control_points, indices, gstate.getPatchDivisionU(), gstate.getPatchDivisionV(), sp_ucount, sp_vcount, sp_utype, sp_vtype, patchPrim, computeNormals, patchFacing, vertType, &bytesRead);

	if (gstate_c.spline)
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
	gstate_c.spline = false;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = sp_ucount * sp_vcount;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPUCommon::Execute_BoundingBox(u32 op, u32 diff) {
	// Just resetting, nothing to check bounds for.
	const u32 count = op & 0xFFFFFF;
	if (count == 0) {
		currentList->bboxResult = false;
		return;
	}
	if (((count & 7) == 0) && count <= 64) {  // Sanity check
		void *control_points = Memory::GetPointer(gstate_c.vertexAddr);
		if (!control_points) {
			return;
		}

		if (gstate.vertType & GE_VTYPE_IDX_MASK) {
			ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Indexed bounding box data not supported.");
			// Data seems invalid. Let's assume the box test passed.
			currentList->bboxResult = true;
			return;
		}

		// Test if the bounding box is within the drawing region.
		int bytesRead;
		currentList->bboxResult = drawEngineCommon_->TestBoundingBox(control_points, count, gstate.vertType, &bytesRead);
		AdvanceVerts(gstate.vertType, count, bytesRead);
	} else {
		ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Bad bounding box data: %06x", count);
		// Data seems invalid. Let's assume the box test passed.
		currentList->bboxResult = true;
	}
}

void GPUCommon::Execute_BlockTransferStart(u32 op, u32 diff) {
	PROFILE_THIS_SCOPE("block");
	Flush();
	// and take appropriate action. This is a block transfer between RAM and VRAM, or vice versa.
	// Can we skip this on SkipDraw?
	DoBlockTransfer(gstate_c.skipDrawReason);
}

void GPUCommon::Execute_WorldMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_WORLDMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.worldMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	// We must record the individual data commands while debugRecording_.
	bool fastLoad = !debugRecording_;
	// Stalling in the middle of a matrix would be stupid, I doubt this check is necessary.
	if (currentList->pc < currentList->stall && currentList->pc + end * 4 >= currentList->stall) {
		fastLoad = false;
	}

	if (fastLoad) {
		while ((src[i] >> 24) == GE_CMD_WORLDMATRIXDATA) {
			const u32 newVal = src[i] << 8;
			if (dst[i] != newVal) {
				Flush();
				dst[i] = newVal;
				gstate_c.Dirty(DIRTY_WORLDMATRIX);
			}
			if (++i >= end) {
				break;
			}
		}
	}

	const int count = i;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_WorldMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.worldmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.worldMatrix)[num]) {
		Flush();
		((u32 *)gstate.worldMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_WORLDMATRIX);
	}
	num++;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | (num & 0xF);
}

void GPUCommon::Execute_ViewMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_VIEWMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.viewMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	bool fastLoad = !debugRecording_;
	if (currentList->pc < currentList->stall && currentList->pc + end * 4 >= currentList->stall) {
		fastLoad = false;
	}

	if (fastLoad) {
		while ((src[i] >> 24) == GE_CMD_VIEWMATRIXDATA) {
			const u32 newVal = src[i] << 8;
			if (dst[i] != newVal) {
				Flush();
				dst[i] = newVal;
				gstate_c.Dirty(DIRTY_VIEWMATRIX);
			}
			if (++i >= end) {
				break;
			}
		}
	}

	const int count = i;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_ViewMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.viewmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.viewMatrix)[num]) {
		Flush();
		((u32 *)gstate.viewMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_VIEWMATRIX);
	}
	num++;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | (num & 0xF);
}

void GPUCommon::Execute_ProjMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_PROJMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.projMatrix + (op & 0xF));
	const int end = 16 - (op & 0xF);
	int i = 0;

	bool fastLoad = !debugRecording_;
	if (currentList->pc < currentList->stall && currentList->pc + end * 4 >= currentList->stall) {
		fastLoad = false;
	}

	if (fastLoad) {
		while ((src[i] >> 24) == GE_CMD_PROJMATRIXDATA) {
			const u32 newVal = src[i] << 8;
			if (dst[i] != newVal) {
				Flush();
				dst[i] = newVal;
				gstate_c.Dirty(DIRTY_PROJMATRIX);
			}
			if (++i >= end) {
				break;
			}
		}
	}

	const int count = i;
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | ((op + count) & 0x1F);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_ProjMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.projmtxnum & 0x1F;    // NOTE: Changed from 0xF to catch overflows
	u32 newVal = op << 8;
	if (num < 0x10 && newVal != ((const u32 *)gstate.projMatrix)[num]) {
		Flush();
		((u32 *)gstate.projMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_PROJMATRIX);
	}
	num++;
	if (num <= 16)
		gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | (num & 0xF);
}

void GPUCommon::Execute_TgenMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_TGENMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.tgenMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	bool fastLoad = !debugRecording_;
	if (currentList->pc < currentList->stall && currentList->pc + end * 4 >= currentList->stall) {
		fastLoad = false;
	}

	if (fastLoad) {
		while ((src[i] >> 24) == GE_CMD_TGENMATRIXDATA) {
			const u32 newVal = src[i] << 8;
			if (dst[i] != newVal) {
				Flush();
				dst[i] = newVal;
				gstate_c.Dirty(DIRTY_TEXMATRIX);
			}
			if (++i >= end) {
				break;
			}
		}
	}

	const int count = i;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_TgenMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.texmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.tgenMatrix)[num]) {
		Flush();
		((u32 *)gstate.tgenMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_TEXMATRIX | DIRTY_FRAGMENTSHADER_STATE);  // We check the matrix to see if we need projection
	}
	num++;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | (num & 0xF);
}

void GPUCommon::Execute_BoneMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_BONEMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.boneMatrix + (op & 0x7F));
	const int end = 12 * 8 - (op & 0x7F);
	int i = 0;

	bool fastLoad = !debugRecording_ && end > 0;
	if (currentList->pc < currentList->stall && currentList->pc + end * 4 >= currentList->stall) {
		fastLoad = false;
	}

	if (fastLoad) {
		// If we can't use software skinning, we have to flush and dirty.
		if (!g_Config.bSoftwareSkinning) {
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

			const unsigned int numPlusCount = (op & 0x7F) + i;
			for (unsigned int num = op & 0x7F; num < numPlusCount; num += 12) {
				gstate_c.Dirty(DIRTY_BONEMATRIX0 << (num / 12));
			}
		} else {
			while ((src[i] >> 24) == GE_CMD_BONEMATRIXDATA) {
				dst[i] = src[i] << 8;
				if (++i >= end) {
					break;
				}
			}

			const unsigned int numPlusCount = (op & 0x7F) + i;
			for (unsigned int num = op & 0x7F; num < numPlusCount; num += 12) {
				gstate_c.deferredVertTypeDirty |= DIRTY_BONEMATRIX0 << (num / 12);
			}
		}
	}

	const int count = i;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | ((op + count) & 0x7F);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_BoneMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.boneMatrixNumber & 0x7F;
	u32 newVal = op << 8;
	if (num < 96 && newVal != ((const u32 *)gstate.boneMatrix)[num]) {
		// Bone matrices should NOT flush when software skinning is enabled!
		if (!g_Config.bSoftwareSkinning) {
			Flush();
			gstate_c.Dirty(DIRTY_BONEMATRIX0 << (num / 12));
		} else {
			gstate_c.deferredVertTypeDirty |= DIRTY_BONEMATRIX0 << (num / 12);
		}
		((u32 *)gstate.boneMatrix)[num] = newVal;
	}
	num++;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x7F);
}

void GPUCommon::Execute_MorphWeight(u32 op, u32 diff) {
	gstate_c.morphWeights[(op >> 24) - GE_CMD_MORPHWEIGHT0] = getFloat24(op);
}

void GPUCommon::Execute_ImmVertexAlphaPrim(u32 op, u32 diff) {
	// Safety check.
	if (immCount_ >= MAX_IMMBUFFER_SIZE) {
		// Only print once for each overrun.
		if (immCount_ == MAX_IMMBUFFER_SIZE) {
			ERROR_LOG_REPORT_ONCE(exceed_imm_buffer, G3D, "Exceeded immediate draw buffer size. gstate.imm_ap=%06x , prim=%d", gstate.imm_ap & 0xFFFFFF, (int)immPrim_);
		}
		if (immCount_ < 0x7fffffff)  // Paranoia :)
			immCount_++;
		return;
	}

	uint32_t data = op & 0xFFFFFF;
	TransformedVertex &v = immBuffer_[immCount_++];

	// Formula deduced from ThrillVille's clear.
	int offsetX = gstate.getOffsetX16();
	int offsetY = gstate.getOffsetY16();
	v.x = ((gstate.imm_vscx & 0xFFFFFF) - offsetX) / 16.0f;
	v.y = ((gstate.imm_vscy & 0xFFFFFF) - offsetY) / 16.0f;
	v.z = gstate.imm_vscz & 0xFFFF;
	v.u = getFloat24(gstate.imm_vtcs);
	v.v = getFloat24(gstate.imm_vtct);
	v.w = getFloat24(gstate.imm_vtcq);
	v.color0_32 = (gstate.imm_cv & 0xFFFFFF) | (gstate.imm_ap << 24);
	v.fog = 0.0f; // we have no information about the scale here
	v.color1_32 = gstate.imm_scv & 0xFFFFFF;
	int prim = (op >> 8) & 0x7;
	if (prim != GE_PRIM_KEEP_PREVIOUS) {
		immPrim_ = (GEPrimitiveType)prim;
	} else if (prim == GE_PRIM_KEEP_PREVIOUS && immCount_ == 2) {
		// Instead of finding a proper point to flush, we just emit a full rectangle every time one
		// is finished.
		FlushImm();
		// Need to reset immCount_ here. If we do it in FlushImm it could get skipped by gstate_c.skipDrawReason.
		immCount_ = 0;
	} else {
		ERROR_LOG_REPORT_ONCE(imm_draw_prim, G3D, "Immediate draw: Unexpected primitive %d at count %d", prim, immCount_);
	}
}

void GPUCommon::FlushImm() {
	SetDrawType(DRAW_PRIM, immPrim_);
	framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// No idea how many cycles to skip, heh.
		return;
	}
	UpdateUVScaleOffset();

	// Instead of plumbing through properly (we'd need to inject these pretransformed vertices in the middle
	// of SoftwareTransform(), which would take a lot of refactoring), we'll cheat and just turn these into
	// through vertices.
	// Since the only known use is Thrillville and it only uses it to clear, we just use color and pos.
	struct ImmVertex {
		uint32_t color;
		float xyz[3];
	};
	ImmVertex temp[MAX_IMMBUFFER_SIZE];
	for (int i = 0; i < immCount_; i++) {
		temp[i].color = immBuffer_[i].color0_32;
		temp[i].xyz[0] = immBuffer_[i].pos[0];
		temp[i].xyz[1] = immBuffer_[i].pos[1];
		temp[i].xyz[2] = immBuffer_[i].pos[2];
	}
	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_THROUGH;

	int bytesRead;
	uint32_t vertTypeID = GetVertTypeID(vtype, 0);
	drawEngineCommon_->DispatchSubmitPrim(temp, nullptr, immPrim_, immCount_, vertTypeID, &bytesRead);
	drawEngineCommon_->DispatchFlush();
	// TOOD: In the future, make a special path for these.
	// drawEngineCommon_->DispatchSubmitImm(immBuffer_, immCount_);
}

void GPUCommon::ExecuteOp(u32 op, u32 diff) {
	const u32 cmd = op >> 24;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_NOP:
		break;

	case GE_CMD_OFFSETADDR:
		Execute_OffsetAddr(op, diff);
		break;

	case GE_CMD_ORIGIN:
		Execute_Origin(op, diff);
		break;

	case GE_CMD_JUMP:
		Execute_Jump(op, diff);
		break;

	case GE_CMD_BJUMP:
		Execute_BJump(op, diff);
		break;

	case GE_CMD_CALL:
		Execute_Call(op, diff);
		break;

	case GE_CMD_RET:
		Execute_Ret(op, diff);
		break;

	case GE_CMD_SIGNAL:
	case GE_CMD_FINISH:
		// Processed in GE_END.
		break;

	case GE_CMD_END:
		Execute_End(op, diff);
		break;

	default:
		DEBUG_LOG(G3D, "DL Unknown: %08x @ %08x", op, currentList == NULL ? 0 : currentList->pc);
		break;
	}
}

void GPUCommon::Execute_Unknown(u32 op, u32 diff) {
	if ((op & 0xFFFFFF) != 0)
		WARN_LOG_REPORT_ONCE(unknowncmd, G3D, "Unknown GE command : %08x ", op);
}

void GPUCommon::FastLoadBoneMatrix(u32 target) {
	const int num = gstate.boneMatrixNumber & 0x7F;
	const int mtxNum = num / 12;
	uint32_t uniformsToDirty = DIRTY_BONEMATRIX0 << mtxNum;
	if ((num - 12 * mtxNum) != 0) {
		uniformsToDirty |= DIRTY_BONEMATRIX0 << ((mtxNum + 1) & 7);
	}

	if (!g_Config.bSoftwareSkinning) {
		Flush();
		gstate_c.Dirty(uniformsToDirty);
	} else {
		gstate_c.deferredVertTypeDirty |= uniformsToDirty;
	}
	gstate.FastLoadBoneMatrix(target);
}

struct DisplayList_v1 {
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListState state;
	SignalBehavior signal;
	int subIntrBase;
	u16 subIntrToken;
	DisplayListStackEntry stack[32];
	int stackptr;
	bool interrupted;
	u64 waitTicks;
	bool interruptsEnabled;
	bool pendingInterrupt;
	bool started;
	size_t contextPtr;
	u32 offsetAddr;
	bool bboxResult;
};

struct DisplayList_v2 {
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListState state;
	SignalBehavior signal;
	int subIntrBase;
	u16 subIntrToken;
	DisplayListStackEntry stack[32];
	int stackptr;
	bool interrupted;
	u64 waitTicks;
	bool interruptsEnabled;
	bool pendingInterrupt;
	bool started;
	PSPPointer<u32_le> context;
	u32 offsetAddr;
	bool bboxResult;
};

void GPUCommon::DoState(PointerWrap &p) {
	auto s = p.Section("GPUCommon", 1, 4);
	if (!s)
		return;

	p.Do<int>(dlQueue);
	if (s >= 4) {
		p.DoArray(dls, ARRAY_SIZE(dls));
	} else if (s >= 3) {
		// This may have been saved with or without padding, depending on platform.
		// We need to upconvert it to our consistently-padded struct.
		static const size_t DisplayList_v3_size = 452;
		static const size_t DisplayList_v4_size = 456;
		static_assert(DisplayList_v4_size == sizeof(DisplayList), "Make sure to change here when updating DisplayList");

		p.DoVoid(&dls[0], DisplayList_v3_size);
		dls[0].padding = 0;

		const u8 *savedPtr = *p.GetPPtr();
		const u32 *savedPtr32 = (const u32 *)savedPtr;
		// Here's the trick: the first member (id) is always the same as the index.
		// The second member (startpc) is always an address, or 0, never 1.  So we can see the padding.
		const bool hasPadding = savedPtr32[1] == 1;
		if (hasPadding) {
			u32 padding;
			p.Do(padding);
		}

		for (size_t i = 1; i < ARRAY_SIZE(dls); ++i) {
			p.DoVoid(&dls[i], DisplayList_v3_size);
			dls[i].padding = 0;
			if (hasPadding) {
				u32 padding;
				p.Do(padding);
			}
		}
	} else if (s >= 2) {
		for (size_t i = 0; i < ARRAY_SIZE(dls); ++i) {
			DisplayList_v2 oldDL;
			p.Do(oldDL);
			// Copy over everything except the last, new member (stackAddr.)
			memcpy(&dls[i], &oldDL, sizeof(DisplayList_v2));
			dls[i].stackAddr = 0;
		}
	} else {
		// Can only be in read mode here.
		for (size_t i = 0; i < ARRAY_SIZE(dls); ++i) {
			DisplayList_v1 oldDL;
			p.Do(oldDL);
			// On 32-bit, they're the same, on 64-bit oldDL is bigger.
			memcpy(&dls[i], &oldDL, sizeof(DisplayList));
			// Fix the other fields.  Let's hope context wasn't important, it was a pointer.
			dls[i].context = 0;
			dls[i].offsetAddr = oldDL.offsetAddr;
			dls[i].bboxResult = oldDL.bboxResult;
			dls[i].stackAddr = 0;
		}
	}
	int currentID = 0;
	if (currentList != nullptr) {
		currentID = (int)(currentList - &dls[0]);
	}
	p.Do(currentID);
	if (currentID == 0) {
		currentList = nullptr;
	} else {
		currentList = &dls[currentID];
	}
	p.Do(interruptRunning);
	p.Do(gpuState);
	p.Do(isbreak);
	p.Do(drawCompleteTicks);
	p.Do(busyTicks);
}

void GPUCommon::InterruptStart(int listid) {
	interruptRunning = true;
}
void GPUCommon::InterruptEnd(int listid) {
	interruptRunning = false;
	isbreak = false;

	DisplayList &dl = dls[listid];
	dl.pendingInterrupt = false;
	// TODO: Unless the signal handler could change it?
	if (dl.state == PSP_GE_DL_STATE_COMPLETED || dl.state == PSP_GE_DL_STATE_NONE) {
		if (dl.started && dl.context.IsValid()) {
			gstate.Restore(dl.context);
			ReapplyGfxState();
		}
		dl.waitTicks = 0;
		__GeTriggerWait(GPU_SYNC_LIST, listid);
	}

	ProcessDLQueue();
}

// TODO: Maybe cleaner to keep this in GE and trigger the clear directly?
void GPUCommon::SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) {
	if (waitType == GPU_SYNC_DRAW && wokeThreads)
	{
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
				dls[i].state = PSP_GE_DL_STATE_NONE;
			}
		}
	}
}

bool GPUCommon::GetCurrentDisplayList(DisplayList &list) {
	if (!currentList) {
		return false;
	}
	list = *currentList;
	return true;
}

std::vector<DisplayList> GPUCommon::ActiveDisplayLists() {
	std::vector<DisplayList> result;

	for (auto it = dlQueue.begin(), end = dlQueue.end(); it != end; ++it) {
		result.push_back(dls[*it]);
	}

	return result;
}

void GPUCommon::ResetListPC(int listID, u32 pc) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(G3D, false, "listID out of range: %d", listID);
		return;
	}

	dls[listID].pc = pc;
}

void GPUCommon::ResetListStall(int listID, u32 stall) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(G3D, false, "listID out of range: %d", listID);
		return;
	}

	dls[listID].stall = stall;
}

void GPUCommon::ResetListState(int listID, DisplayListState state) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(G3D, false, "listID out of range: %d", listID);
		return;
	}

	dls[listID].state = state;
}

GPUDebugOp GPUCommon::DissassembleOp(u32 pc, u32 op) {
	char buffer[1024];
	GeDisassembleOp(pc, op, Memory::Read_U32(pc - 4), buffer, sizeof(buffer));

	GPUDebugOp info;
	info.pc = pc;
	info.cmd = op >> 24;
	info.op = op;
	info.desc = buffer;
	return info;
}

std::vector<GPUDebugOp> GPUCommon::DissassembleOpRange(u32 startpc, u32 endpc) {
	char buffer[1024];
	std::vector<GPUDebugOp> result;
	GPUDebugOp info;

	// Don't trigger a pause.
	u32 prev = Memory::IsValidAddress(startpc - 4) ? Memory::Read_U32(startpc - 4) : 0;
	for (u32 pc = startpc; pc < endpc; pc += 4) {
		u32 op = Memory::IsValidAddress(pc) ? Memory::Read_U32(pc) : 0;
		GeDisassembleOp(pc, op, prev, buffer, sizeof(buffer));
		prev = op;

		info.pc = pc;
		info.cmd = op >> 24;
		info.op = op;
		info.desc = buffer;
		result.push_back(info);
	}
	return result;
}

u32 GPUCommon::GetRelativeAddress(u32 data) {
	return gstate_c.getRelativeAddress(data);
}

u32 GPUCommon::GetVertexAddress() {
	return gstate_c.vertexAddr;
}

u32 GPUCommon::GetIndexAddress() {
	return gstate_c.indexAddr;
}

GPUgstate GPUCommon::GetGState() {
	return gstate;
}

void GPUCommon::SetCmdValue(u32 op) {
	u32 cmd = op >> 24;
	u32 diff = op ^ gstate.cmdmem[cmd];

	PreExecuteOp(op, diff);
	gstate.cmdmem[cmd] = op;
	ExecuteOp(op, diff);
}

void GPUCommon::DoBlockTransfer(u32 skipDrawReason) {
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

	u32 srcLastAddr = srcBasePtr + ((srcY + height - 1) * srcStride + (srcX + width - 1)) * bpp;
	u32 dstLastAddr = dstBasePtr + ((dstY + height - 1) * dstStride + (dstX + width - 1)) * bpp;

	if (!Memory::IsValidAddress(srcLastAddr)) {
		ERROR_LOG_REPORT(G3D, "Bottom-right corner of source of block transfer is at an invalid address: %08x", srcLastAddr);
		return;
	}
	if (!Memory::IsValidAddress(dstLastAddr)) {
		ERROR_LOG_REPORT(G3D, "Bottom-right corner of destination of block transfer is at an invalid address: %08x", srcLastAddr);
		return;
	}

	// Tell the framebuffer manager to take action if possible. If it does the entire thing, let's just return.
	if (!framebufferManager_->NotifyBlockTransferBefore(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp, skipDrawReason)) {
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

		// Fixes Gran Turismo's funky text issue, since it overwrites the current texture.
		textureCache_->Invalidate(dstBasePtr + (dstY * dstStride + dstX) * bpp, height * dstStride * bpp, GPU_INVALIDATE_HINT);
		framebufferManager_->NotifyBlockTransferAfter(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp, skipDrawReason);
	}

#ifndef MOBILE_DEVICE
	CBreakPoints::ExecMemCheck(srcBasePtr + (srcY * srcStride + srcX) * bpp, false, height * srcStride * bpp, currentMIPS->pc);
	CBreakPoints::ExecMemCheck(dstBasePtr + (dstY * dstStride + dstX) * bpp, true, height * dstStride * bpp, currentMIPS->pc);
#endif

	// TODO: Correct timing appears to be 1.9, but erring a bit low since some of our other timing is inaccurate.
	cyclesExecuted += ((height * width * bpp) * 16) / 10;
}

bool GPUCommon::PerformMemoryCopy(u32 dest, u32 src, int size) {
	// Track stray copies of a framebuffer in RAM. MotoGP does this.
	if (framebufferManager_->MayIntersectFramebuffer(src) || framebufferManager_->MayIntersectFramebuffer(dest)) {
		if (!framebufferManager_->NotifyFramebufferCopy(src, dest, size, false, gstate_c.skipDrawReason)) {
			// We use a little hack for Download/Upload using a VRAM mirror.
			// Since they're identical we don't need to copy.
			if (!Memory::IsVRAMAddress(dest) || (dest ^ 0x00400000) != src) {
				Memory::Memcpy(dest, src, size);
			}
		}
		InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
		return true;
	}

	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	GPURecord::NotifyMemcpy(dest, src, size);
	return false;
}

bool GPUCommon::PerformMemorySet(u32 dest, u8 v, int size) {
	// This may indicate a memset, usually to 0, of a framebuffer.
	if (framebufferManager_->MayIntersectFramebuffer(dest)) {
		Memory::Memset(dest, v, size);
		if (!framebufferManager_->NotifyFramebufferCopy(dest, dest, size, true, gstate_c.skipDrawReason)) {
			InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
		}
		return true;
	}

	// Or perhaps a texture, let's invalidate.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	GPURecord::NotifyMemset(dest, v, size);
	return false;
}

bool GPUCommon::PerformMemoryDownload(u32 dest, int size) {
	// Cheat a bit to force a download of the framebuffer.
	// VRAM + 0x00400000 is simply a VRAM mirror.
	if (Memory::IsVRAMAddress(dest)) {
		return PerformMemoryCopy(dest ^ 0x00400000, dest, size);
	}
	return false;
}

bool GPUCommon::PerformMemoryUpload(u32 dest, int size) {
	// Cheat a bit to force an upload of the framebuffer.
	// VRAM + 0x00400000 is simply a VRAM mirror.
	if (Memory::IsVRAMAddress(dest)) {
		GPURecord::NotifyUpload(dest, size);
		return PerformMemoryCopy(dest, dest ^ 0x00400000, size);
	}
	return false;
}

void GPUCommon::InvalidateCache(u32 addr, int size, GPUInvalidationType type) {
	if (size > 0)
		textureCache_->Invalidate(addr, size, type);
	else
		textureCache_->InvalidateAll(type);

	if (type != GPU_INVALIDATE_ALL && framebufferManager_->MayIntersectFramebuffer(addr)) {
		// If we're doing block transfers, we shouldn't need this, and it'll only confuse us.
		// Vempire invalidates (with writeback) after drawing, but before blitting.
		if (!g_Config.bBlockTransferGPU || type == GPU_INVALIDATE_SAFE) {
			framebufferManager_->UpdateFromMemory(addr, size, type == GPU_INVALIDATE_SAFE);
		}
	}
}

void GPUCommon::NotifyVideoUpload(u32 addr, int size, int width, int format) {
	if (Memory::IsVRAMAddress(addr)) {
		framebufferManager_->NotifyVideoUpload(addr, size, width, (GEBufferFormat)format);
	}
	textureCache_->NotifyVideoUpload(addr, size, width, (GEBufferFormat)format);
	InvalidateCache(addr, size, GPU_INVALIDATE_SAFE);
}

bool GPUCommon::PerformStencilUpload(u32 dest, int size) {
	if (framebufferManager_->MayIntersectFramebuffer(dest)) {
		framebufferManager_->NotifyStencilUpload(dest, size);
		return true;
	}
	return false;
}

bool GPUCommon::GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) {
	u32 fb_address = type == GPU_DBG_FRAMEBUF_RENDER ? gstate.getFrameBufRawAddress() : framebufferManager_->DisplayFramebufAddr();
	int fb_stride = type == GPU_DBG_FRAMEBUF_RENDER ? gstate.FrameBufStride() : framebufferManager_->DisplayFramebufStride();
	GEBufferFormat format = type == GPU_DBG_FRAMEBUF_RENDER ? gstate.FrameBufFormat() : framebufferManager_->DisplayFramebufFormat();
	return framebufferManager_->GetFramebuffer(fb_address, fb_stride, format, buffer, maxRes);
}

bool GPUCommon::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	u32 z_address = gstate.getDepthBufRawAddress();
	int z_stride = gstate.DepthBufStride();

	return framebufferManager_->GetDepthbuffer(fb_address, fb_stride, z_address, z_stride, buffer);
}

bool GPUCommon::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	return framebufferManager_->GetStencilbuffer(fb_address, fb_stride, buffer);
}

bool GPUCommon::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	// framebufferManager_ can be null here when taking screens in software rendering mode.
	// TODO: Actually grab the framebuffer anyway.
	return framebufferManager_ ? framebufferManager_->GetOutputFramebuffer(buffer) : false;
}

std::vector<FramebufferInfo> GPUCommon::GetFramebufferList() {
	return framebufferManager_->GetFramebufferList();
}

bool GPUCommon::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	return drawEngineCommon_->GetCurrentSimpleVertices(count, vertices, indices);
}

bool GPUCommon::GetCurrentClut(GPUDebugBuffer &buffer) {
	return textureCache_->GetCurrentClutBuffer(buffer);
}

bool GPUCommon::GetCurrentTexture(GPUDebugBuffer &buffer, int level) {
	if (!gstate.isTextureMapEnabled()) {
		return false;
	}
	return textureCache_->GetCurrentTextureDebug(buffer, level);
}

bool GPUCommon::DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (drawEngineCommon_->IsCodePtrVertexDecoder(ptr)) {
		name = "VertexDecoderJit";
		return true;
	}
	return false;
}

bool GPUCommon::FramebufferDirty() {
	VirtualFramebuffer *vfb = framebufferManager_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->dirtyAfterDisplay;
		vfb->dirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

bool GPUCommon::FramebufferReallyDirty() {
	VirtualFramebuffer *vfb = framebufferManager_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->reallyDirtyAfterDisplay;
		vfb->reallyDirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}
