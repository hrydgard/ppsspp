#include "ppsspp_config.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

#include <algorithm>
#include <type_traits>
#include <mutex>

#include "Common/Profiler/Profiler.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/GraphicsContext.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeList.h"
#include "Common/TimeUtil.h"
#include "Core/Reporting.h"
#include "GPU/GeDisasm.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceGe.h"
#include "Core/HW/Display.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PPGeDraw.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/Record.h"

const CommonCommandTableEntry commonCommandTable[] = {
	// From Common. No flushing but definitely need execute.
	{ GE_CMD_OFFSETADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_OffsetAddr },
	{ GE_CMD_ORIGIN, FLAG_EXECUTE | FLAG_READS_PC, 0, &GPUCommon::Execute_Origin },
	{ GE_CMD_JUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Jump },
	{ GE_CMD_CALL, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Call },
	{ GE_CMD_RET, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Ret },
	{ GE_CMD_END, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_End },
	{ GE_CMD_VADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_Vaddr },
	{ GE_CMD_IADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_Iaddr },
	{ GE_CMD_BJUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_BJump },  // EXECUTE
	{ GE_CMD_BOUNDINGBOX, FLAG_EXECUTE, 0, &GPUCommon::Execute_BoundingBox }, // Shouldn't need to FLUSHBEFORE.

	{ GE_CMD_PRIM, FLAG_EXECUTE, 0, &GPUCommon::Execute_Prim },
	{ GE_CMD_BEZIER, FLAG_EXECUTE, 0, &GPUCommon::Execute_Bezier },
	{ GE_CMD_SPLINE, FLAG_EXECUTE, 0, &GPUCommon::Execute_Spline },

	// Changing the vertex type requires us to flush.
	{ GE_CMD_VERTEXTYPE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPUCommon::Execute_VertexType },

	{ GE_CMD_LOADCLUT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPUCommon::Execute_LoadClut },

	// These two are actually processed in CMD_END.
	{ GE_CMD_SIGNAL },
	{ GE_CMD_FINISH },

	// Changes that dirty the framebuffer
	{ GE_CMD_FRAMEBUFPTR, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS },
	{ GE_CMD_FRAMEBUFWIDTH, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },
	{ GE_CMD_FRAMEBUFPIXFORMAT, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_ZBUFPTR, FLAG_FLUSHBEFOREONCHANGE },
	{ GE_CMD_ZBUFWIDTH, FLAG_FLUSHBEFOREONCHANGE },

	{ GE_CMD_FOGCOLOR, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FOGCOLOR },
	{ GE_CMD_FOG1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FOGCOEF },
	{ GE_CMD_FOG2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FOGCOEF },

	// These affect the fragment shader so need flushing.
	{ GE_CMD_CLEARMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE },
	{ GE_CMD_TEXTUREMAPENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE },
	{ GE_CMD_FOGENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAGMENTSHADER_STATE },
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
	{ GE_CMD_LIGHTINGENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE },
	{ GE_CMD_LIGHTENABLE0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },
	{ GE_CMD_LIGHTENABLE1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },
	{ GE_CMD_LIGHTENABLE2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },
	{ GE_CMD_LIGHTENABLE3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },
	{ GE_CMD_LIGHTTYPE0, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT0 },
	{ GE_CMD_LIGHTTYPE1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT1 },
	{ GE_CMD_LIGHTTYPE2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT2 },
	{ GE_CMD_LIGHTTYPE3, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_LIGHT3 },
	{ GE_CMD_MATERIALUPDATE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE },

	// These change both shaders so need flushing.
	{ GE_CMD_LIGHTMODE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE },

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
	{ GE_CMD_MASKRGB, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_COLORWRITEMASK },
	{ GE_CMD_MASKALPHA, FLAG_FLUSHBEFOREONCHANGE, DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_COLORWRITEMASK },
	{ GE_CMD_ZTEST, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHSTENCIL_STATE },
	{ GE_CMD_ZTESTENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHSTENCIL_STATE | DIRTY_FRAGMENTSHADER_STATE },
	{ GE_CMD_ZWRITEDISABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHSTENCIL_STATE | DIRTY_FRAGMENTSHADER_STATE },
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
	{ GE_CMD_OFFSETX, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },
	{ GE_CMD_OFFSETY, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },
	{ GE_CMD_VIEWPORTXSCALE, FLAG_FLUSHBEFOREONCHANGE,  DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_CULLRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTYSCALE, FLAG_FLUSHBEFOREONCHANGE,  DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_CULLRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTXCENTER, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_CULLRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTYCENTER, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_CULLRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTZSCALE, FLAG_FLUSHBEFOREONCHANGE,  DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_CULLRANGE | DIRTY_DEPTHRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_VIEWPORTZCENTER, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_CULLRANGE | DIRTY_DEPTHRANGE | DIRTY_PROJMATRIX | DIRTY_VIEWPORTSCISSOR_STATE },
	{ GE_CMD_DEPTHCLAMPENABLE, FLAG_FLUSHBEFOREONCHANGE, DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE | DIRTY_RASTER_STATE },

	// Z clip
	{ GE_CMD_MINZ, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHRANGE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },
	{ GE_CMD_MAXZ, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHRANGE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },

	// Region
	{ GE_CMD_REGION1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },
	{ GE_CMD_REGION2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },

	// Scissor
	{ GE_CMD_SCISSOR1, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },
	{ GE_CMD_SCISSOR2, FLAG_FLUSHBEFOREONCHANGE, DIRTY_FRAMEBUF | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE },

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
	{ GE_CMD_TRANSFERSTART, FLAG_EXECUTE | FLAG_READS_PC, 0, &GPUCommon::Execute_BlockTransferStart },

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

// TODO: Make class member?
GPUCommon::CommandInfo GPUCommon::cmdInfo_[256];

void GPUCommon::Flush() {
	drawEngineCommon_->DispatchFlush();
}

void GPUCommon::DispatchFlush() {
	drawEngineCommon_->DispatchFlush();
}

GPUCommon::GPUCommon(GraphicsContext *gfxCtx, Draw::DrawContext *draw) :
	gfxCtx_(gfxCtx),
	draw_(draw)
{
	// This assert failed on GCC x86 32-bit (but not MSVC 32-bit!) before adding the
	// "padding" field at the end. This is important for save state compatibility.
	// The compiler was not rounding the struct size up to an 8 byte boundary, which
	// you'd expect due to the int64 field, but the Linux ABI apparently does not require that.
	static_assert(sizeof(DisplayList) == 456, "Bad DisplayList size");

	Reinitialize();
	gstate.Reset();
	gstate_c.Reset();
	gpuStats.Reset();

	memset(cmdInfo_, 0, sizeof(cmdInfo_));

	// Convert the command table to a faster format, and check for dupes.
	std::set<u8> dupeCheck;
	for (size_t i = 0; i < ARRAY_SIZE(commonCommandTable); i++) {
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
	UpdateVsyncInterval(true);
	ResetMatrices();

	PPGeSetDrawContext(draw);

	UpdateMSAALevel(draw);
}

GPUCommon::~GPUCommon() {
	// Probably not necessary.
	PPGeSetDrawContext(nullptr);
}

void GPUCommon::UpdateCmdInfo() {
	if (g_Config.bSoftwareSkinning) {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags &= ~FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPUCommon::Execute_VertexTypeSkinning;
	} else {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags |= FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPUCommon::Execute_VertexType;
	}

	if (g_Config.bFastMemory) {
		cmdInfo_[GE_CMD_JUMP].func = &GPUCommon::Execute_JumpFast;
		cmdInfo_[GE_CMD_CALL].func = &GPUCommon::Execute_CallFast;
	} else {
		cmdInfo_[GE_CMD_JUMP].func = &GPUCommon::Execute_Jump;
		cmdInfo_[GE_CMD_CALL].func = &GPUCommon::Execute_Call;
	}

	// Reconfigure for light ubershader or not.
	for (int i = 0; i < 4; i++) {
		if (gstate_c.Use(GPU_USE_LIGHT_UBERSHADER)) {
			cmdInfo_[GE_CMD_LIGHTENABLE0 + i].RemoveDirty(DIRTY_VERTEXSHADER_STATE);
			cmdInfo_[GE_CMD_LIGHTENABLE0 + i].AddDirty(DIRTY_LIGHT_CONTROL);
			cmdInfo_[GE_CMD_LIGHTTYPE0 + i].RemoveDirty(DIRTY_VERTEXSHADER_STATE);
			cmdInfo_[GE_CMD_LIGHTTYPE0 + i].AddDirty(DIRTY_LIGHT_CONTROL);
		} else {
			cmdInfo_[GE_CMD_LIGHTENABLE0 + i].RemoveDirty(DIRTY_LIGHT_CONTROL);
			cmdInfo_[GE_CMD_LIGHTENABLE0 + i].AddDirty(DIRTY_VERTEXSHADER_STATE);
			cmdInfo_[GE_CMD_LIGHTTYPE0 + i].RemoveDirty(DIRTY_LIGHT_CONTROL);
			cmdInfo_[GE_CMD_LIGHTTYPE0 + i].AddDirty(DIRTY_VERTEXSHADER_STATE);
		}
	}

	if (gstate_c.Use(GPU_USE_LIGHT_UBERSHADER)) {
		cmdInfo_[GE_CMD_MATERIALUPDATE].RemoveDirty(DIRTY_VERTEXSHADER_STATE);
		cmdInfo_[GE_CMD_MATERIALUPDATE].AddDirty(DIRTY_LIGHT_CONTROL);
	} else {
		cmdInfo_[GE_CMD_MATERIALUPDATE].RemoveDirty(DIRTY_LIGHT_CONTROL);
		cmdInfo_[GE_CMD_MATERIALUPDATE].AddDirty(DIRTY_VERTEXSHADER_STATE);
	}
}

void GPUCommon::BeginHostFrame() {
	UpdateVsyncInterval(displayResized_);
	ReapplyGfxState();

	// TODO: Assume config may have changed - maybe move to resize.
	gstate_c.Dirty(DIRTY_ALL);

	UpdateCmdInfo();

	UpdateMSAALevel(draw_);
	CheckConfigChanged();
	CheckDisplayResized();
	CheckRenderResized();
}

void GPUCommon::EndHostFrame() {
	// Probably not necessary.
	if (draw_) {
		draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	}
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

	if (textureCache_)
		textureCache_->Clear(true);
	if (framebufferManager_)
		framebufferManager_->DestroyAllFBOs();
}

// Call at the END of the GPU implementation's DeviceLost
void GPUCommon::DeviceLost() {
	framebufferManager_->DeviceLost();
	draw_ = nullptr;
}

// Call at the start of the GPU implementation's DeviceRestore
void GPUCommon::DeviceRestore() {
	draw_ = (Draw::DrawContext *)PSP_CoreParameter().graphicsContext->GetDrawContext();
	framebufferManager_->DeviceRestore(draw_);
	PPGeSetDrawContext(draw_);
}

void GPUCommon::UpdateVsyncInterval(bool force) {
#if !(PPSSPP_PLATFORM(ANDROID) || defined(USING_QT_UI) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(IOS))
	int desiredVSyncInterval = g_Config.bVSync ? 1 : 0;
	if (PSP_CoreParameter().fastForward) {
		desiredVSyncInterval = 0;
	}
	if (PSP_CoreParameter().fpsLimit != FPSLimit::NORMAL) {
		int limit;
		if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM1)
			limit = g_Config.iFpsLimit1;
		else if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM2)
			limit = g_Config.iFpsLimit2;
		else
			limit = PSP_CoreParameter().analogFpsLimit;

		// For an alternative speed that is a clean factor of 60, the user probably still wants vsync.
		if (limit == 0 || (limit >= 0 && limit != 15 && limit != 30 && limit != 60)) {
			desiredVSyncInterval = 0;
		}
	}

	if (desiredVSyncInterval != lastVsync_ || force) {
		// Disabled EXT_swap_control_tear for now, it never seems to settle at the correct timing
		// so it just keeps tearing. Not what I hoped for... (gl_extensions.EXT_swap_control_tear)
		// See http://developer.download.nvidia.com/opengl/specs/WGL_EXT_swap_control_tear.txt
		if (gfxCtx_)
			gfxCtx_->SwapInterval(desiredVSyncInterval);
		lastVsync_ = desiredVSyncInterval;
	}
#endif
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
				cost += 7;
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

void GPUCommon::NotifyConfigChanged() {
	configChanged_ = true;
}

void GPUCommon::NotifyRenderResized() {
	renderResized_ = true;
}

void GPUCommon::NotifyDisplayResized() {
	displayResized_ = true;
}

void GPUCommon::ClearCacheNextFrame() {
	textureCache_->ClearNextFrame();
}

// Called once per frame. Might also get called during the pause screen
// if "transparent".
void GPUCommon::CheckConfigChanged() {
	if (configChanged_) {
		ClearCacheNextFrame();
		gstate_c.SetUseFlags(CheckGPUFeatures());
		drawEngineCommon_->NotifyConfigChanged();
		textureCache_->NotifyConfigChanged();
		framebufferManager_->NotifyConfigChanged();
		BuildReportingInfo();
		configChanged_ = false;
	}

	// Check needed when running tests.
	if (framebufferManager_) {
		framebufferManager_->CheckPostShaders();
	}
}

void GPUCommon::CheckDisplayResized() {
	if (displayResized_) {
		framebufferManager_->NotifyDisplayResized();
		displayResized_ = false;
	}
}

void GPUCommon::CheckRenderResized() {
	if (renderResized_) {
		framebufferManager_->NotifyRenderResized(msaaLevel_);
		renderResized_ = false;
	}
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
		auto stack = PSPPointer<u32_le>::Create(stackPtr);
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

static void CopyMatrix24(u32_le *result, const float *mtx, u32 count, u32 cmdbits) {
	for (u32 i = 0; i < count; ++i) {
		result[i] = toFloat24(mtx[i]) | cmdbits;
	}
}

bool GPUCommon::GetMatrix24(GEMatrixType type, u32_le *result, u32 cmdbits) {
	switch (type) {
	case GE_MTX_BONE0:
	case GE_MTX_BONE1:
	case GE_MTX_BONE2:
	case GE_MTX_BONE3:
	case GE_MTX_BONE4:
	case GE_MTX_BONE5:
	case GE_MTX_BONE6:
	case GE_MTX_BONE7:
		CopyMatrix24(result, gstate.boneMatrix + (type - GE_MTX_BONE0) * 12, 12, cmdbits);
		break;
	case GE_MTX_TEXGEN:
		CopyMatrix24(result, gstate.tgenMatrix, 12, cmdbits);
		break;
	case GE_MTX_WORLD:
		CopyMatrix24(result, gstate.worldMatrix, 12, cmdbits);
		break;
	case GE_MTX_VIEW:
		CopyMatrix24(result, gstate.viewMatrix, 12, cmdbits);
		break;
	case GE_MTX_PROJECTION:
		CopyMatrix24(result, gstate.projMatrix, 16, cmdbits);
		break;
	default:
		return false;
	}
	return true;
}

void GPUCommon::ResetMatrices() {
	// This means we restored a context, so update the visible matrix data.
	for (size_t i = 0; i < ARRAY_SIZE(gstate.boneMatrix); ++i)
		matrixVisible.bone[i] = toFloat24(gstate.boneMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.worldMatrix); ++i)
		matrixVisible.world[i] = toFloat24(gstate.worldMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.viewMatrix); ++i)
		matrixVisible.view[i] = toFloat24(gstate.viewMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.projMatrix); ++i)
		matrixVisible.proj[i] = toFloat24(gstate.projMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.tgenMatrix); ++i)
		matrixVisible.tgen[i] = toFloat24(gstate.tgenMatrix[i]);

	// Assume all the matrices changed, so dirty things related to them.
	gstate_c.Dirty(DIRTY_WORLDMATRIX | DIRTY_VIEWMATRIX | DIRTY_PROJMATRIX | DIRTY_TEXMATRIX | DIRTY_FRAGMENTSHADER_STATE | DIRTY_BONE_UNIFORMS);
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head) {
	// TODO Check the stack values in missing arg and ajust the stack depth

	// Check alignment
	// TODO Check the context and stack alignement too
	if (((listpc | stall) & 3) != 0 || !Memory::IsValidAddress(listpc)) {
		ERROR_LOG_REPORT(G3D, "sceGeListEnqueue: invalid address %08x", listpc);
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}

	// If args->size is below 16, it's the old struct without stack info.
	if (args.IsValid() && args->size >= 16 && args->numStacks >= 256) {
		return hleLogError(G3D, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid stack depth %d", args->numStacks);
	}

	int id = -1;
	u64 currentTicks = CoreTiming::GetTicks();
	u32 stackAddr = args.IsValid() && args->size >= 16 ? (u32)args->stackAddr : 0;
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
			// Make sure we clear the signal so we don't try to pause it again.
			currentList->signal = PSP_GE_SIGNAL_NONE;
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
		if (!isbreak) {
			// TODO: Supposedly this returns SCE_KERNEL_ERROR_BUSY in some case, previously it had
			// currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE, but it doesn't reproduce.

			currentList->state = PSP_GE_DL_STATE_RUNNING;
			currentList->signal = PSP_GE_SIGNAL_NONE;

			// TODO Restore context of DL is necessary
			// TODO Restore BASE

			// We have a list now, so it's not complete.
			drawCompleteTicks = (u64)-1;
		} else {
			currentList->state = PSP_GE_DL_STATE_QUEUED;
			currentList->signal = PSP_GE_SIGNAL_NONE;
		}
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
		timeSteppingStarted_ = time_now_d();
	}
}
void GPUCommon::NotifySteppingExit() {
	if (coreCollectDebugStats) {
		if (timeSteppingStarted_ <= 0.0) {
			ERROR_LOG(G3D, "Mismatched stepping enter/exit.");
		}
		double total = time_now_d() - timeSteppingStarted_;
		_dbg_assert_msg_(total >= 0.0, "Time spent stepping became negative");
		timeSpentStepping_ += total;
		timeSteppingStarted_ = 0.0;
	}
}

bool GPUCommon::InterpretList(DisplayList &list) {
	// Initialized to avoid a race condition with bShowDebugStats changing.
	double start = 0.0;
	if (coreCollectDebugStats) {
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

	// To enable breakpoints, we don't do fast matrix loads while debugger active.
	debugRecording_ = GPUDebug::IsActive() || GPURecord::IsActive();
	const bool useFastRunLoop = !dumpThisFrame_ && !debugRecording_;
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
	if (debugRecording_)
		GPURecord::NotifyCPU();

	// We haven't run the op at list.pc, so it shouldn't count.
	if (cycleLastPC != list.pc) {
		UpdatePC(list.pc - 4, list.pc);
	}

	list.offsetAddr = gstate_c.offsetAddr;

	if (coreCollectDebugStats) {
		double total = time_now_d() - start - timeSpentStepping_;
		_dbg_assert_msg_(total >= 0.0, "Time spent DL processing became negative");
		hleSetSteppingTime(timeSpentStepping_);
		DisplayNotifySleep(timeSpentStepping_);
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
		const u32 op = *(const u32_le *)(Memory::base + list.pc);
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
	GPUDebug::NotifyBeginFrame();
	GPURecord::NotifyBeginFrame();

	if (drawEngineCommon_->EverUsedExactEqualDepth() && !sawExactEqualDepth_) {
		sawExactEqualDepth_ = true;
		gstate_c.SetUseFlags(CheckGPUFeatures());
	}
}

void GPUCommon::SlowRunLoop(DisplayList &list)
{
	const bool dumpThisFrame = dumpThisFrame_;
	while (downcount > 0)
	{
		bool process = GPUDebug::NotifyCommand(list.pc);
		if (process) {
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
		}

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

	// TODO: Consider whether any of this should really be done. We might be able to get all the way
	// by simplying dirtying the appropriate gstate_c dirty flags.

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

	// 0x42 to 0xEA
	for (int i = GE_CMD_VIEWPORTXSCALE; i < GE_CMD_TRANSFERSTART; i++) {
		switch (i) {
		case GE_CMD_LOADCLUT:
		case GE_CMD_TEXSYNC:
		case GE_CMD_TEXFLUSH:
			break;
		default:
			ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
			break;
		}
	}

	// Let's just skip the transfer size stuff, it's just values.
}

uint32_t GPUCommon::SetAddrTranslation(uint32_t value) {
	std::swap(edramTranslation_, value);
	return value;
}

uint32_t GPUCommon::GetAddrTranslation() {
	return edramTranslation_;
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
	if (currentList)
		gstate_c.offsetAddr = currentList->pc;
}

void GPUCommon::Execute_Jump(u32 op, u32 diff) {
	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
	if (!Memory::IsValidAddress(target)) {
		ERROR_LOG(G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		UpdateState(GPUSTATE_ERROR);
		return;
	}
	UpdatePC(currentList->pc, target - 4);
	currentList->pc = target - 4; // pc will be increased after we return, counteract that
}

void GPUCommon::Execute_JumpFast(u32 op, u32 diff) {
	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
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
			ERROR_LOG(G3D, "BJUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
			UpdateState(GPUSTATE_ERROR);
		}
	}
}

void GPUCommon::Execute_Call(u32 op, u32 diff) {
	PROFILE_THIS_SCOPE("gpu_call");

	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
	if (!Memory::IsValidAddress(target)) {
		ERROR_LOG(G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		UpdateState(GPUSTATE_ERROR);
		return;
	}
	DoExecuteCall(target);
}

void GPUCommon::Execute_CallFast(u32 op, u32 diff) {
	PROFILE_THIS_SCOPE("gpu_call");

	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
	DoExecuteCall(target);
}

void GPUCommon::DoExecuteCall(u32 target) {
	// Saint Seiya needs correct support for relative calls.
	const u32 retval = currentList->pc + 4;

	// Bone matrix optimization - many games will CALL a bone matrix (!).
	// We don't optimize during recording - so the matrix data gets recorded.
	if (!debugRecording_ && (Memory::ReadUnchecked_U32(target) >> 24) == GE_CMD_BONEMATRIXDATA) {
		// Check for the end
		if ((Memory::ReadUnchecked_U32(target + 11 * 4) >> 24) == GE_CMD_BONEMATRIXDATA &&
				(Memory::ReadUnchecked_U32(target + 12 * 4) >> 24) == GE_CMD_RET &&
				(gstate.boneMatrixNumber & 0x00FFFFFF) <= 96 - 12) {
			// Yep, pretty sure this is a bone matrix call.  Double check stall first.
			if (target > currentList->stall || target + 12 * 4 < currentList->stall) {
				FastLoadBoneMatrix(target);
				return;
			}
		}
	}

	if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
		ERROR_LOG(G3D, "CALL: Stack full!");
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
		DEBUG_LOG(G3D, "RET: Stack empty!");
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
	if (flushOnParams_)
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
			case PSP_GE_SIGNAL_RJUMP:
			case PSP_GE_SIGNAL_OJUMP:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = (((signal << 16) | enddata) & 0xFFFFFFFC) - 4;
					const char *targetType = "absolute";
					if (behaviour == PSP_GE_SIGNAL_RJUMP) {
						target += currentList->pc - 4;
						targetType = "relative";
					} else if (behaviour == PSP_GE_SIGNAL_OJUMP) {
						target = gstate_c.getRelativeAddress(target);
						targetType = "origin";
					}

					if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(G3D, "Signal with Jump (%s): bad address. signal/end: %04x %04x", targetType, signal, enddata);
						UpdateState(GPUSTATE_ERROR);
					} else {
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(G3D, "Signal with Jump (%s). signal/end: %04x %04x", targetType, signal, enddata);
					}
				}
				break;
			case PSP_GE_SIGNAL_CALL:
			case PSP_GE_SIGNAL_RCALL:
			case PSP_GE_SIGNAL_OCALL:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = (((signal << 16) | enddata) & 0xFFFFFFFC) - 4;
					const char *targetType = "absolute";
					if (behaviour == PSP_GE_SIGNAL_RCALL) {
						target += currentList->pc - 4;
						targetType = "relative";
					} else if (behaviour == PSP_GE_SIGNAL_OCALL) {
						target = gstate_c.getRelativeAddress(target);
						targetType = "origin";
					}

					if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
						ERROR_LOG_REPORT(G3D, "Signal with Call (%s): stack full. signal/end: %04x %04x", targetType, signal, enddata);
					} else if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(G3D, "Signal with Call (%s): bad address. signal/end: %04x %04x", targetType, signal, enddata);
						UpdateState(GPUSTATE_ERROR);
					} else {
						// TODO: This might save/restore other state...
						auto &stackEntry = currentList->stack[currentList->stackptr++];
						stackEntry.pc = currentList->pc;
						stackEntry.offsetAddr = gstate_c.offsetAddr;
						stackEntry.baseAddr = gstate.base;
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(G3D, "Signal with Call (%s). signal/end: %04x %04x", targetType, signal, enddata);
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
			FlushImm();
			currentList->subIntrToken = prev & 0xFFFF;
			UpdateState(GPUSTATE_DONE);
			// Since we marked done, we have to restore the context now before the next list runs.
			if (currentList->started && currentList->context.IsValid()) {
				gstate.Restore(currentList->context);
				ReapplyGfxState();
				// Don't restore the context again.
				currentList->started = false;
			}

			if (currentList->interruptsEnabled && __GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
				currentList->pendingInterrupt = true;
			} else {
				currentList->state = PSP_GE_DL_STATE_COMPLETED;
				currentList->waitTicks = startingTicks + cyclesExecuted;
				busyTicks = std::max(busyTicks, currentList->waitTicks);
				__GeTriggerSync(GPU_SYNC_LIST, currentList->id, currentList->waitTicks);
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

	if (diff & 0xFF0000) {
		// Piggyback on this flag for 3D textures.
		gstate_c.Dirty(DIRTY_MIPBIAS);
	}
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
			gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE | DIRTY_CULLRANGE);
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
		gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE | DIRTY_CULLRANGE);
}

void GPUCommon::CheckDepthUsage(VirtualFramebuffer *vfb) {
	if (!gstate_c.usingDepth) {
		bool isReadingDepth = false;
		bool isClearingDepth = false;
		bool isWritingDepth = false;
		if (gstate.isModeClear()) {
			isClearingDepth = gstate.isClearModeDepthMask();
			isWritingDepth = isClearingDepth;
		} else if (gstate.isDepthTestEnabled()) {
			isWritingDepth = gstate.isDepthWriteEnabled();
			isReadingDepth = gstate.getDepthTestFunction() > GE_COMP_ALWAYS;
		}

		if (isWritingDepth || isReadingDepth) {
			gstate_c.usingDepth = true;
			gstate_c.clearingDepth = isClearingDepth;
			vfb->last_frame_depth_render = gpuStats.numFlips;
			if (isWritingDepth) {
				vfb->last_frame_depth_updated = gpuStats.numFlips;
			}
			framebufferManager_->SetDepthFrameBuffer(isClearingDepth);
		}
	}
}

void GPUCommon::Execute_Prim(u32 op, u32 diff) {
	// This drives all drawing. All other state we just buffer up, then we apply it only
	// when it's time to draw. As most PSP games set state redundantly ALL THE TIME, this is a huge optimization.

	PROFILE_THIS_SCOPE("execprim");

	u32 data = op & 0xFFFFFF;
	u32 count = data & 0xFFFF;
	if (count == 0)
		return;
	FlushImm();

	// Upper bits are ignored.
	GEPrimitiveType prim = static_cast<GEPrimitiveType>((data >> 16) & 7);
	SetDrawType(DRAW_PRIM, prim);

	// Discard AA lines as we can't do anything that makes sense with these anyway. The SW plugin might, though.
	if (gstate.isAntiAliasEnabled()) {
		// Heuristic derived from discussions in #6483 and #12588.
		// Discard AA lines in Persona 3 Portable, DOA Paradise and Summon Night 5, while still keeping AA lines in Echochrome.
		if ((prim == GE_PRIM_LINE_STRIP || prim == GE_PRIM_LINES) && gstate.getTextureFunction() == GE_TEXFUNC_REPLACE)
			return;
	}

	// Update cached framebuffer format.
	// We store it in the cache so it can be modified for blue-to-alpha, next.
	gstate_c.framebufFormat = gstate.FrameBufFormat();

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	// See the documentation for gstate_c.blueToAlpha.
	bool blueToAlpha = false;
	if (PSP_CoreParameter().compat.flags().BlueToAlpha) {
		if (gstate_c.framebufFormat == GEBufferFormat::GE_FORMAT_565 && gstate.getColorMask() == 0x0FFFFF && !gstate.isLogicOpEnabled()) {
			blueToAlpha = true;
			gstate_c.framebufFormat = GEBufferFormat::GE_FORMAT_4444;
		}
		if (blueToAlpha != gstate_c.blueToAlpha) {
			gstate_c.blueToAlpha = blueToAlpha;
			gstate_c.Dirty(DIRTY_FRAMEBUF | DIRTY_FRAGMENTSHADER_STATE | DIRTY_BLEND_STATE);
		}
	}

	if (PSP_CoreParameter().compat.flags().SplitFramebufferMargin) {
		switch (gstate.vertType & 0xFFFFFF) {
		case 0x00800102:  // through, u16 uv, u16 pos (used for the framebuffer effect in-game)
		case 0x0080011c:  // through, 8888 color, s16 pos (used for clearing in the margin of the title screen)
		case 0x00000183:  // float uv, float pos (used for drawing in the margin of the title screen)
			// Need to re-check the framebuffer every one of these draws, to update the split if needed.
			gstate_c.Dirty(DIRTY_FRAMEBUF);
		}
	}

	// This also makes skipping drawing very effective.
	VirtualFramebuffer *vfb = framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (blueToAlpha) {
		vfb->usageFlags |= FB_USAGE_BLUE_TO_ALPHA;
	}

	// Must check this after SetRenderFrameBuffer so we know SKIPDRAW_NON_DISPLAYED_FB.
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// Rough estimate, not sure what's correct.
		cyclesExecuted += EstimatePerVertexCost() * count;
		if (gstate.isModeClear()) {
			gpuStats.numClears++;
		}
		return;
	}

	CheckDepthUsage(vfb);

	const void *verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	const void *inds = nullptr;
	u32 vertexType = gstate.vertType;
	if ((vertexType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		u32 indexAddr = gstate_c.indexAddr;
		if (!Memory::IsValidAddress(indexAddr)) {
			ERROR_LOG(G3D, "Bad index address %08x!", indexAddr);
			return;
		}
		inds = Memory::GetPointerUnchecked(indexAddr);
	}

	if (gstate_c.dirty & DIRTY_VERTEXSHADER_STATE) {
		vertexCost_ = EstimatePerVertexCost();
	}

	int bytesRead = 0;
	UpdateUVScaleOffset();

	// cull mode
	int cullMode = gstate.getCullMode();

	uint32_t vertTypeID = GetVertTypeID(vertexType, gstate.getUVGenMode(), g_Config.bSoftwareSkinning);
	drawEngineCommon_->SubmitPrim(verts, inds, prim, count, vertTypeID, cullMode, &bytesRead);
	// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	AdvanceVerts(vertexType, count, bytesRead);
	int totalVertCount = count;

	// PRIMs are often followed by more PRIMs. Save some work and submit them immediately.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	const u32_le *stall = currentList->stall ? (const u32_le *)Memory::GetPointerUnchecked(currentList->stall) : 0;
	int cmdCount = 0;

	// Optimized submission of sequences of PRIM. Allows us to avoid going through all the mess
	// above for each one. This can be expanded to support additional games that intersperse
	// PRIM commands with other commands. A special case is Earth Defence Force 2 that changes culling mode
	// between each prim, we just change the triangle winding right here to still be able to join draw calls.

	uint32_t vtypeCheckMask = ~GE_VTYPE_WEIGHTCOUNT_MASK;
	if (!g_Config.bSoftwareSkinning)
		vtypeCheckMask = 0xFFFFFFFF;

	if (debugRecording_)
		goto bail;

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
			SetDrawType(DRAW_PRIM, newPrim);
			// TODO: more efficient updating of verts/inds
			verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
			inds = nullptr;
			if ((vertexType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
			}

			drawEngineCommon_->SubmitPrim(verts, inds, newPrim, count, vertTypeID, cullMode, &bytesRead);
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
				vertTypeID = GetVertTypeID(vertexType, gstate.getUVGenMode(), g_Config.bSoftwareSkinning);
			}
			break;
		}
		case GE_CMD_VADDR:
			gstate.cmdmem[GE_CMD_VADDR] = data;
			gstate_c.vertexAddr = gstate_c.getRelativeAddress(data & 0x00FFFFFF);
			break;
		case GE_CMD_IADDR:
			gstate.cmdmem[GE_CMD_IADDR] = data;
			gstate_c.indexAddr = gstate_c.getRelativeAddress(data & 0x00FFFFFF);
			break;
		case GE_CMD_OFFSETADDR:
			gstate.cmdmem[GE_CMD_OFFSETADDR] = data;
			gstate_c.offsetAddr = data << 8;
			break;
		case GE_CMD_BASE:
			gstate.cmdmem[GE_CMD_BASE] = data;
			break;
		case GE_CMD_CULLFACEENABLE:
			// Earth Defence Force 2
			if (gstate.cmdmem[GE_CMD_CULLFACEENABLE] != data) {
				goto bail;
			}
			break;
		case GE_CMD_CULL:
			// flip face by indices for triangles
			cullMode = data & 1;
			break;
		case GE_CMD_TEXFLUSH:
		case GE_CMD_NOP:
		case GE_CMD_NOP_FF:
			gstate.cmdmem[data >> 24] = data;
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
		case GE_CMD_TEXOFFSETU:
			gstate.cmdmem[GE_CMD_TEXOFFSETU] = data;
			gstate_c.uv.uOff = getFloat24(data);
			break;
		case GE_CMD_TEXOFFSETV:
			gstate.cmdmem[GE_CMD_TEXOFFSETV] = data;
			gstate_c.uv.vOff = getFloat24(data);
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
				(target > currentList->stall || target + 12 * 4 < currentList->stall) &&
				(gstate.boneMatrixNumber & 0x00FFFFFF) <= 96 - 12) {
				FastLoadBoneMatrix(target);
			} else {
				goto bail;
			}
			break;
		}

		case GE_CMD_TEXBUFWIDTH0:
		case GE_CMD_TEXADDR0:
			if (data != gstate.cmdmem[data >> 24])
				goto bail;
			break;

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
		// flush back cull mode
		if (cullMode != gstate.getCullMode()) {
			// We rewrote everything to the old cull mode, so flush first.
			drawEngineCommon_->DispatchFlush();

			// Now update things for next time.
			gstate.cmdmem[GE_CMD_CULL] ^= 1;
			gstate_c.Dirty(DIRTY_RASTER_STATE);
		}
	}

	gpuStats.vertexGPUCycles += vertexCost_ * totalVertCount;
	cyclesExecuted += vertexCost_ * totalVertCount;
}

void GPUCommon::Execute_Bezier(u32 op, u32 diff) {
	// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	gstate_c.framebufFormat = gstate.FrameBufFormat();

	// This also make skipping drawing very effective.
	VirtualFramebuffer *vfb = framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	CheckDepthUsage(vfb);

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	const void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	const void *indices = NULL;
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

	// Can't flush after setting gstate_c.submitType below since it'll be a mess - it must be done already.
	if (flushOnParams_)
		drawEngineCommon_->DispatchFlush();

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

	gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
	if (drawEngineCommon_->CanUseHardwareTessellation(surface.primType)) {
		gstate_c.submitType = SubmitType::HW_BEZIER;
		if (gstate_c.spline_num_points_u != surface.num_points_u) {
			gstate_c.Dirty(DIRTY_BEZIERSPLINE);
			gstate_c.spline_num_points_u = surface.num_points_u;
		}
	} else {
		gstate_c.submitType = SubmitType::BEZIER;
	}

	int bytesRead = 0;
	UpdateUVScaleOffset();
	drawEngineCommon_->SubmitCurve(control_points, indices, surface, gstate.vertType, &bytesRead, "bezier");

	gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
	gstate_c.submitType = SubmitType::DRAW;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = surface.num_points_u * surface.num_points_v;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPUCommon::Execute_Spline(u32 op, u32 diff) {
	// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	gstate_c.framebufFormat = gstate.FrameBufFormat();

	// This also make skipping drawing very effective.
	VirtualFramebuffer *vfb = framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	CheckDepthUsage(vfb);

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	const void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	const void *indices = NULL;
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

	// Can't flush after setting gstate_c.submitType below since it'll be a mess - it must be done already.
	if (flushOnParams_)
		drawEngineCommon_->DispatchFlush();

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

	gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
	if (drawEngineCommon_->CanUseHardwareTessellation(surface.primType)) {
		gstate_c.submitType = SubmitType::HW_SPLINE;
		if (gstate_c.spline_num_points_u != surface.num_points_u) {
			gstate_c.Dirty(DIRTY_BEZIERSPLINE);
			gstate_c.spline_num_points_u = surface.num_points_u;
		}
	} else {
		gstate_c.submitType = SubmitType::SPLINE;
	}

	int bytesRead = 0;
	UpdateUVScaleOffset();
	drawEngineCommon_->SubmitCurve(control_points, indices, surface, gstate.vertType, &bytesRead, "spline");

	gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
	gstate_c.submitType = SubmitType::DRAW;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = surface.num_points_u * surface.num_points_v;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPUCommon::Execute_BoundingBox(u32 op, u32 diff) {
	// Just resetting, nothing to check bounds for.
	const u32 count = op & 0xFFFF;
	if (count == 0) {
		currentList->bboxResult = false;
		return;
	}

	// Approximate based on timings of several counts on a PSP.
	cyclesExecuted += count * 22;

	const bool useInds = (gstate.vertType & GE_VTYPE_IDX_MASK) != 0;
	VertexDecoder *dec = drawEngineCommon_->GetVertexDecoder(gstate.vertType);
	int bytesRead = (useInds ? 1 : dec->VertexSize()) * count;

	if (Memory::IsValidRange(gstate_c.vertexAddr, bytesRead)) {
		const void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
		if (!control_points) {
			ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Invalid verts in bounding box check");
			currentList->bboxResult = true;
			return;
		}

		const void *inds = nullptr;
		if (useInds) {
			int indexShift = ((gstate.vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
			inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
			if (!inds || !Memory::IsValidRange(gstate_c.indexAddr, count << indexShift)) {
				ERROR_LOG_REPORT_ONCE(boundingboxInds, G3D, "Invalid inds in bounding box check");
				currentList->bboxResult = true;
				return;
			}
		}

		// Test if the bounding box is within the drawing region.
		// The PSP only seems to vary the result based on a single range of 0x100.
		if (count > 0x200) {
			// The second to last set of 0x100 is checked (even for odd counts.)
			size_t skipSize = (count - 0x200) * dec->VertexSize();
			currentList->bboxResult = drawEngineCommon_->TestBoundingBox((const uint8_t *)control_points + skipSize, inds, 0x100, gstate.vertType);
		} else if (count > 0x100) {
			int checkSize = count - 0x100;
			currentList->bboxResult = drawEngineCommon_->TestBoundingBox(control_points, inds, checkSize, gstate.vertType);
		} else {
			currentList->bboxResult = drawEngineCommon_->TestBoundingBox(control_points, inds, count, gstate.vertType);
		}
		AdvanceVerts(gstate.vertType, count, bytesRead);
	} else {
		ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Bad bounding box data: %06x", count);
		// Data seems invalid. Let's assume the box test passed.
		currentList->bboxResult = true;
	}
}

void GPUCommon::Execute_BlockTransferStart(u32 op, u32 diff) {
	Flush();

	PROFILE_THIS_SCOPE("block");  // don't include the flush in the profile, would be misleading.

	gstate_c.framebufFormat = gstate.FrameBufFormat();

	// and take appropriate action. This is a block transfer between RAM and VRAM, or vice versa.
	// Can we skip this on SkipDraw?
	DoBlockTransfer(gstate_c.skipDrawReason);
}

void GPUCommon::Execute_WorldMtxNum(u32 op, u32 diff) {
	if (!currentList) {
		gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | (op & 0xF);
		return;
	}

	// This is almost always followed by GE_CMD_WORLDMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.worldMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	// We must record the individual data commands while debugRecording_.
	bool fastLoad = !debugRecording_ && end > 0;
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
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | ((op & 0xF) + count);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_WorldMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.worldmtxnum & 0x00FFFFFF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.worldMatrix)[num]) {
		Flush();
		((u32 *)gstate.worldMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_WORLDMATRIX);
	}
	num++;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.worldmtxdata = GE_CMD_WORLDMATRIXDATA << 24;
}

void GPUCommon::Execute_ViewMtxNum(u32 op, u32 diff) {
	if (!currentList) {
		gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | (op & 0xF);
		return;
	}

	// This is almost always followed by GE_CMD_VIEWMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.viewMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	bool fastLoad = !debugRecording_ && end > 0;
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
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | ((op & 0xF) + count);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_ViewMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.viewmtxnum & 0x00FFFFFF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.viewMatrix)[num]) {
		Flush();
		((u32 *)gstate.viewMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_VIEWMATRIX);
	}
	num++;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.viewmtxdata = GE_CMD_VIEWMATRIXDATA << 24;
}

void GPUCommon::Execute_ProjMtxNum(u32 op, u32 diff) {
	if (!currentList) {
		gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | (op & 0xF);
		return;
	}

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
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | ((op & 0xF) + count);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_ProjMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.projmtxnum & 0x00FFFFFF;
	u32 newVal = op << 8;
	if (num < 16 && newVal != ((const u32 *)gstate.projMatrix)[num]) {
		Flush();
		((u32 *)gstate.projMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_PROJMATRIX);
	}
	num++;
	if (num <= 16)
		gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.projmtxdata = GE_CMD_PROJMATRIXDATA << 24;
}

void GPUCommon::Execute_TgenMtxNum(u32 op, u32 diff) {
	if (!currentList) {
		gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | (op & 0xF);
		return;
	}

	// This is almost always followed by GE_CMD_TGENMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.tgenMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	bool fastLoad = !debugRecording_ && end > 0;
	if (currentList->pc < currentList->stall && currentList->pc + end * 4 >= currentList->stall) {
		fastLoad = false;
	}

	if (fastLoad) {
		while ((src[i] >> 24) == GE_CMD_TGENMATRIXDATA) {
			const u32 newVal = src[i] << 8;
			if (dst[i] != newVal) {
				Flush();
				dst[i] = newVal;
				// We check the matrix to see if we need projection.
				gstate_c.Dirty(DIRTY_TEXMATRIX | DIRTY_FRAGMENTSHADER_STATE);
			}
			if (++i >= end) {
				break;
			}
		}
	}

	const int count = i;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | ((op & 0xF) + count);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_TgenMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.texmtxnum & 0x00FFFFFF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.tgenMatrix)[num]) {
		Flush();
		((u32 *)gstate.tgenMatrix)[num] = newVal;
		gstate_c.Dirty(DIRTY_TEXMATRIX | DIRTY_FRAGMENTSHADER_STATE);  // We check the matrix to see if we need projection
	}
	num++;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.texmtxdata = GE_CMD_TGENMATRIXDATA << 24;
}

void GPUCommon::Execute_BoneMtxNum(u32 op, u32 diff) {
	if (!currentList) {
		gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (op & 0x7F);
		return;
	}

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
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | ((op & 0x7F) + count);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPUCommon::Execute_BoneMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.boneMatrixNumber & 0x00FFFFFF;
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
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
	gstate.boneMatrixData = GE_CMD_BONEMATRIXDATA << 24;
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

	int prim = (op >> 8) & 0x7;
	if (prim != GE_PRIM_KEEP_PREVIOUS) {
		// Flush before changing the prim type.  Only continue can be used to continue a prim.
		FlushImm();
	}

	TransformedVertex &v = immBuffer_[immCount_++];

	// ThrillVille does a clear with this, additional parameters found via tests.
	// The current vtype affects how the coordinate is processed.
	if (gstate.isModeThrough()) {
		v.x = ((int)(gstate.imm_vscx & 0xFFFF) - 0x8000) / 16.0f;
		v.y = ((int)(gstate.imm_vscy & 0xFFFF) - 0x8000) / 16.0f;
	} else {
		int offsetX = gstate.getOffsetX16();
		int offsetY = gstate.getOffsetY16();
		v.x = ((int)(gstate.imm_vscx & 0xFFFF) - offsetX) / 16.0f;
		v.y = ((int)(gstate.imm_vscy & 0xFFFF) - offsetY) / 16.0f;
	}
	v.z = gstate.imm_vscz & 0xFFFF;
	v.pos_w = 1.0f;
	v.u = getFloat24(gstate.imm_vtcs);
	v.v = getFloat24(gstate.imm_vtct);
	v.uv_w = getFloat24(gstate.imm_vtcq);
	v.color0_32 = (gstate.imm_cv & 0xFFFFFF) | (gstate.imm_ap << 24);
	// TODO: When !gstate.isModeThrough(), direct fog coefficient (0 = entirely fog), ignore fog flag (also GE_IMM_FOG.)
	v.fog = (gstate.imm_fc & 0xFF) / 255.0f;
	// TODO: Apply if gstate.isUsingSecondaryColor() && !gstate.isModeThrough(), ignore lighting flag.
	v.color1_32 = gstate.imm_scv & 0xFFFFFF;
	if (prim != GE_PRIM_KEEP_PREVIOUS) {
		immPrim_ = (GEPrimitiveType)prim;
		// Flags seem to only be respected from the first prim.
		immFlags_ = op & 0x00FFF800;
		immFirstSent_ = false;
	} else if (prim == GE_PRIM_KEEP_PREVIOUS && immPrim_ != GE_PRIM_INVALID) {
		static constexpr int flushPrimCount[] = { 1, 2, 0, 3, 0, 0, 2, 0 };
		// Instead of finding a proper point to flush, we just emit prims when we can.
		if (immCount_ == flushPrimCount[immPrim_ & 7])
			FlushImm();
	} else {
		ERROR_LOG_REPORT_ONCE(imm_draw_prim, G3D, "Immediate draw: Unexpected primitive %d at count %d", prim, immCount_);
	}
}

void GPUCommon::FlushImm() {
	if (immCount_ == 0 || immPrim_ == GE_PRIM_INVALID)
		return;

	SetDrawType(DRAW_PRIM, immPrim_);
	if (framebufferManager_)
		framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// No idea how many cycles to skip, heh.
		immCount_ = 0;
		return;
	}
	UpdateUVScaleOffset();

	bool antialias = (immFlags_ & GE_IMM_ANTIALIAS) != 0;
	bool prevAntialias = gstate.isAntiAliasEnabled();
	bool shading = (immFlags_ & GE_IMM_SHADING) != 0;
	bool prevShading = gstate.getShadeMode() == GE_SHADE_GOURAUD;
	bool cullEnable = (immFlags_ & GE_IMM_CULLENABLE) != 0;
	bool prevCullEnable = gstate.isCullEnabled();
	int cullMode = (immFlags_ & GE_IMM_CULLFACE) != 0 ? 1 : 0;
	bool texturing = (immFlags_ & GE_IMM_TEXTURE) != 0;
	bool prevTexturing = gstate.isTextureMapEnabled();
	bool fog = (immFlags_ & GE_IMM_FOG) != 0;
	bool prevFog = gstate.isFogEnabled();
	bool dither = (immFlags_ & GE_IMM_DITHER) != 0;
	bool prevDither = gstate.isDitherEnabled();

	if ((immFlags_ & GE_IMM_CLIPMASK) != 0) {
		WARN_LOG_REPORT_ONCE(geimmclipvalue, G3D, "Imm vertex used clip value, flags=%06x", immFlags_);
	}

	bool changed = texturing != prevTexturing || cullEnable != prevCullEnable || dither != prevDither;
	changed = changed || prevShading != shading || prevFog != fog;
	if (changed) {
		DispatchFlush();
		gstate.antiAliasEnable = (GE_CMD_ANTIALIASENABLE << 24) | (int)antialias;
		gstate.shademodel = (GE_CMD_SHADEMODE << 24) | (int)shading;
		gstate.cullfaceEnable = (GE_CMD_CULLFACEENABLE << 24) | (int)cullEnable;
		gstate.textureMapEnable = (GE_CMD_TEXTUREMAPENABLE << 24) | (int)texturing;
		gstate.fogEnable = (GE_CMD_FOGENABLE << 24) | (int)fog;
		gstate.ditherEnable = (GE_CMD_DITHERENABLE << 24) | (int)dither;
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_UVSCALEOFFSET | DIRTY_CULLRANGE);
	}

	drawEngineCommon_->DispatchSubmitImm(immPrim_, immBuffer_, immCount_, cullMode, immFirstSent_);
	immCount_ = 0;
	immFirstSent_ = true;

	if (changed) {
		DispatchFlush();
		gstate.antiAliasEnable = (GE_CMD_ANTIALIASENABLE << 24) | (int)prevAntialias;
		gstate.shademodel = (GE_CMD_SHADEMODE << 24) | (int)prevShading;
		gstate.cullfaceEnable = (GE_CMD_CULLFACEENABLE << 24) | (int)prevCullEnable;
		gstate.textureMapEnable = (GE_CMD_TEXTUREMAPENABLE << 24) | (int)prevTexturing;
		gstate.fogEnable = (GE_CMD_FOGENABLE << 24) | (int)prevFog;
		gstate.ditherEnable = (GE_CMD_DITHERENABLE << 24) | (int)prevDither;
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_UVSCALEOFFSET | DIRTY_CULLRANGE);
	}
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
	const u32 num = gstate.boneMatrixNumber & 0x7F;
	_dbg_assert_msg_(num + 12 <= 96, "FastLoadBoneMatrix would corrupt memory");
	const u32 mtxNum = num / 12;
	u32 uniformsToDirty = DIRTY_BONEMATRIX0 << mtxNum;
	if (num != 12 * mtxNum) {
		uniformsToDirty |= DIRTY_BONEMATRIX0 << ((mtxNum + 1) & 7);
	}

	if (!g_Config.bSoftwareSkinning) {
		if (flushOnParams_)
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
	auto s = p.Section("GPUCommon", 1, 6);
	if (!s)
		return;

	Do<int>(p, dlQueue);
	if (s >= 4) {
		DoArray(p, dls, ARRAY_SIZE(dls));
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
			Do(p, padding);
		}

		for (size_t i = 1; i < ARRAY_SIZE(dls); ++i) {
			p.DoVoid(&dls[i], DisplayList_v3_size);
			dls[i].padding = 0;
			if (hasPadding) {
				u32 padding;
				Do(p, padding);
			}
		}
	} else if (s >= 2) {
		for (size_t i = 0; i < ARRAY_SIZE(dls); ++i) {
			DisplayList_v2 oldDL;
			Do(p, oldDL);
			// Copy over everything except the last, new member (stackAddr.)
			memcpy(&dls[i], &oldDL, sizeof(DisplayList_v2));
			dls[i].stackAddr = 0;
		}
	} else {
		// Can only be in read mode here.
		for (size_t i = 0; i < ARRAY_SIZE(dls); ++i) {
			DisplayList_v1 oldDL;
			Do(p, oldDL);
			// On 32-bit, they're the same, on 64-bit oldDL is bigger.
			memcpy(&dls[i], &oldDL, sizeof(DisplayList_v1));
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
	Do(p, currentID);
	if (currentID == 0) {
		currentList = nullptr;
	} else {
		currentList = &dls[currentID];
	}
	Do(p, interruptRunning);
	Do(p, gpuState);
	Do(p, isbreak);
	Do(p, drawCompleteTicks);
	Do(p, busyTicks);

	if (s >= 5) {
		Do(p, matrixVisible.all);
	}
	if (s >= 6) {
		Do(p, edramTranslation_);
	}
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

		// Make sure the list isn't still queued since it's now completed.
		if (!dlQueue.empty()) {
			if (listid == dlQueue.front())
				PopDLQueue();
			else
				dlQueue.remove(listid);
		}
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
		_dbg_assert_msg_(false, "listID out of range: %d", listID);
		return;
	}

	Reporting::NotifyDebugger();
	dls[listID].pc = pc;
	downcount = 0;
}

void GPUCommon::ResetListStall(int listID, u32 stall) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(false, "listID out of range: %d", listID);
		return;
	}

	Reporting::NotifyDebugger();
	dls[listID].stall = stall;
	downcount = 0;
}

void GPUCommon::ResetListState(int listID, DisplayListState state) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(false, "listID out of range: %d", listID);
		return;
	}

	Reporting::NotifyDebugger();
	dls[listID].state = state;
	downcount = 0;
}

GPUDebugOp GPUCommon::DissassembleOp(u32 pc, u32 op) {
	char buffer[1024];
	u32 prev = Memory::IsValidAddress(pc - 4) ? Memory::ReadUnchecked_U32(pc - 4) : 0;
	GeDisassembleOp(pc, op, prev, buffer, sizeof(buffer));

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

	Reporting::NotifyDebugger();
	PreExecuteOp(op, diff);
	gstate.cmdmem[cmd] = op;
	ExecuteOp(op, diff);
	downcount = 0;
}

void GPUCommon::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	framebufferManager_->SetDisplayFramebuffer(framebuf, stride, format);
}

void GPUCommon::DoBlockTransfer(u32 skipDrawReason) {
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

	// For VRAM, we wrap around when outside valid memory (mirrors still work.)
	if ((srcBasePtr & 0x04800000) == 0x04800000)
		srcBasePtr &= ~0x00800000;
	if ((dstBasePtr & 0x04800000) == 0x04800000)
		dstBasePtr &= ~0x00800000;

	// Use height less one to account for width, which can be greater or less than stride.
	const uint32_t src = srcBasePtr + (srcY * srcStride + srcX) * bpp;
	const uint32_t srcSize = (height - 1) * (srcStride + width) * bpp;
	const uint32_t dst = dstBasePtr + (dstY * dstStride + dstX) * bpp;
	const uint32_t dstSize = (height - 1) * (dstStride + width) * bpp;

	bool srcDstOverlap = src + srcSize > dst && dst + dstSize > src;
	bool srcValid = Memory::IsValidRange(src, srcSize);
	bool dstValid = Memory::IsValidRange(dst, dstSize);
	bool srcWraps = Memory::IsVRAMAddress(srcBasePtr) && !srcValid;
	bool dstWraps = Memory::IsVRAMAddress(dstBasePtr) && !dstValid;

	char tag[128];
	size_t tagSize;

	// Tell the framebuffer manager to take action if possible. If it does the entire thing, let's just return.
	if (!framebufferManager_ || !framebufferManager_->NotifyBlockTransferBefore(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp, skipDrawReason)) {
		// Do the copy! (Hm, if we detect a drawn video frame (see below) then we could maybe skip this?)
		// Can use GetPointerUnchecked because we checked the addresses above. We could also avoid them
		// entirely by walking a couple of pointers...

		// Simple case: just a straight copy, no overlap or wrapping.
		if (srcStride == dstStride && (u32)width == srcStride && !srcDstOverlap && srcValid && dstValid) {
			u32 srcLineStartAddr = srcBasePtr + (srcY * srcStride + srcX) * bpp;
			u32 dstLineStartAddr = dstBasePtr + (dstY * dstStride + dstX) * bpp;
			u32 bytesToCopy = width * height * bpp;

			const u8 *srcp = Memory::GetPointer(srcLineStartAddr);
			u8 *dstp = Memory::GetPointerWrite(dstLineStartAddr);
			memcpy(dstp, srcp, bytesToCopy);

			if (MemBlockInfoDetailed(bytesToCopy)) {
				tagSize = FormatMemWriteTagAt(tag, sizeof(tag), "GPUBlockTransfer/", src, bytesToCopy);
				NotifyMemInfo(MemBlockFlags::READ, src, bytesToCopy, tag, tagSize);
				NotifyMemInfo(MemBlockFlags::WRITE, dst, bytesToCopy, tag, tagSize);
			}
		} else if ((srcDstOverlap || srcWraps || dstWraps) && (srcValid || srcWraps) && (dstValid || dstWraps)) {
			// This path means we have either src/dst overlap, OR one or both of src and dst wrap.
			// This should be uncommon so it's the slowest path.
			u32 bytesToCopy = width * bpp;
			bool notifyDetail = MemBlockInfoDetailed(srcWraps || dstWraps ? 64 : bytesToCopy);
			bool notifyAll = !notifyDetail && MemBlockInfoDetailed(srcSize, dstSize);
			if (notifyDetail || notifyAll) {
				tagSize = FormatMemWriteTagAt(tag, sizeof(tag), "GPUBlockTransfer/", src, srcSize);
			}

			auto notifyingMemmove = [&](u32 d, u32 s, u32 sz) {
				const u8 *srcp = Memory::GetPointer(s);
				u8 *dstp = Memory::GetPointerWrite(d);
				memmove(dstp, srcp, sz);

				if (notifyDetail) {
					NotifyMemInfo(MemBlockFlags::READ, s, sz, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::WRITE, d, sz, tag, tagSize);
				}
			};

			for (int y = 0; y < height; y++) {
				u32 srcLineStartAddr = srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp;
				u32 dstLineStartAddr = dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp;
				// If we already passed a wrap, we can use the quicker path.
				if ((srcLineStartAddr & 0x04800000) == 0x04800000)
					srcLineStartAddr &= ~0x00800000;
				if ((dstLineStartAddr & 0x04800000) == 0x04800000)
					dstLineStartAddr &= ~0x00800000;
				// These flags mean there's a wrap inside this line.
				bool srcLineWrap = !Memory::IsValidRange(srcLineStartAddr, bytesToCopy);
				bool dstLineWrap = !Memory::IsValidRange(dstLineStartAddr, bytesToCopy);

				if (!srcLineWrap && !dstLineWrap) {
					const u8 *srcp = Memory::GetPointer(srcLineStartAddr);
					u8 *dstp = Memory::GetPointerWrite(dstLineStartAddr);
					for (u32 i = 0; i < bytesToCopy; i += 64) {
						u32 chunk = i + 64 > bytesToCopy ? bytesToCopy - i : 64;
						memmove(dstp + i, srcp + i, chunk);
					}

					// If we're tracking detail, it's useful to have the gaps illustrated properly.
					if (notifyDetail) {
						NotifyMemInfo(MemBlockFlags::READ, srcLineStartAddr, bytesToCopy, tag, tagSize);
						NotifyMemInfo(MemBlockFlags::WRITE, dstLineStartAddr, bytesToCopy, tag, tagSize);
					}
				} else {
					// We can wrap at any point, so along with overlap this gets a bit complicated.
					// We're just going to do this the slow and easy way.
					u32 srcLinePos = srcLineStartAddr;
					u32 dstLinePos = dstLineStartAddr;
					for (u32 i = 0; i < bytesToCopy; i += 64) {
						u32 chunk = i + 64 > bytesToCopy ? bytesToCopy - i : 64;
						u32 srcValid = Memory::ValidSize(srcLinePos, chunk);
						u32 dstValid = Memory::ValidSize(dstLinePos, chunk);

						// First chunk, for which both are valid.
						u32 bothSize = std::min(srcValid, dstValid);
						if (bothSize != 0)
							notifyingMemmove(dstLinePos, srcLinePos, bothSize);

						// Now, whichever side has more valid (or the rest, if only one side must wrap.)
						u32 exclusiveSize = std::max(srcValid, dstValid) - bothSize;
						if (exclusiveSize != 0 && srcValid >= dstValid) {
							notifyingMemmove(PSP_GetVidMemBase(), srcLineStartAddr + bothSize, exclusiveSize);
						} else if (exclusiveSize != 0 && srcValid < dstValid) {
							notifyingMemmove(dstLineStartAddr + bothSize, PSP_GetVidMemBase(), exclusiveSize);
						}

						// Finally, if both src and dst wrapped, that portion.
						u32 wrappedSize = chunk - bothSize - exclusiveSize;
						if (wrappedSize != 0 && srcValid >= dstValid) {
							notifyingMemmove(PSP_GetVidMemBase() + exclusiveSize, PSP_GetVidMemBase(), wrappedSize);
						} else if (wrappedSize != 0 && srcValid < dstValid) {
							notifyingMemmove(PSP_GetVidMemBase(), PSP_GetVidMemBase() + exclusiveSize, wrappedSize);
						}

						srcLinePos += chunk;
						dstLinePos += chunk;
						if ((srcLinePos & 0x04800000) == 0x04800000)
							srcLinePos &= ~0x00800000;
						if ((dstLinePos & 0x04800000) == 0x04800000)
							dstLinePos &= ~0x00800000;
					}
				}
			}

			if (notifyAll) {
				if (srcWraps) {
					u32 validSize = Memory::ValidSize(src, srcSize);
					NotifyMemInfo(MemBlockFlags::READ, src, validSize, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::READ, PSP_GetVidMemBase(), srcSize - validSize, tag, tagSize);
				} else {
					NotifyMemInfo(MemBlockFlags::READ, src, srcSize, tag, tagSize);
				}
				if (dstWraps) {
					u32 validSize = Memory::ValidSize(dst, dstSize);
					NotifyMemInfo(MemBlockFlags::WRITE, dst, validSize, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::WRITE, PSP_GetVidMemBase(), dstSize - validSize, tag, tagSize);
				} else {
					NotifyMemInfo(MemBlockFlags::WRITE, dst, dstSize, tag, tagSize);
				}
			}
		} else if (srcValid && dstValid) {
			u32 bytesToCopy = width * bpp;
			bool notifyDetail = MemBlockInfoDetailed(bytesToCopy);
			bool notifyAll = !notifyDetail && MemBlockInfoDetailed(srcSize, dstSize);
			if (notifyDetail || notifyAll) {
				tagSize = FormatMemWriteTagAt(tag, sizeof(tag), "GPUBlockTransfer/", src, srcSize);
			}

			for (int y = 0; y < height; y++) {
				u32 srcLineStartAddr = srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp;
				u32 dstLineStartAddr = dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp;

				const u8 *srcp = Memory::GetPointer(srcLineStartAddr);
				u8 *dstp = Memory::GetPointerWrite(dstLineStartAddr);
				memcpy(dstp, srcp, bytesToCopy);

				// If we're tracking detail, it's useful to have the gaps illustrated properly.
				if (notifyDetail) {
					NotifyMemInfo(MemBlockFlags::READ, srcLineStartAddr, bytesToCopy, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::WRITE, dstLineStartAddr, bytesToCopy, tag, tagSize);
				}
			}

			if (notifyAll) {
				NotifyMemInfo(MemBlockFlags::READ, src, srcSize, tag, tagSize);
				NotifyMemInfo(MemBlockFlags::WRITE, dst, dstSize, tag, tagSize);
			}
		} else {
			// This seems to cause the GE to require a break/reset on a PSP.
			// TODO: Handle that and figure out which bytes are still copied?
			ERROR_LOG_REPORT_ONCE(invalidtransfer, G3D, "Block transfer invalid: %08x/%x -> %08x/%x, %ix%ix%i (%i,%i)->(%i,%i)", srcBasePtr, srcStride, dstBasePtr, dstStride, width, height, bpp, srcX, srcY, dstX, dstY);
		}

		if (framebufferManager_) {
			// Fixes Gran Turismo's funky text issue, since it overwrites the current texture.
			textureCache_->Invalidate(dstBasePtr + (dstY * dstStride + dstX) * bpp, height * dstStride * bpp, GPU_INVALIDATE_HINT);
			framebufferManager_->NotifyBlockTransferAfter(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp, skipDrawReason);
		}
	}

	// TODO: Correct timing appears to be 1.9, but erring a bit low since some of our other timing is inaccurate.
	cyclesExecuted += ((height * width * bpp) * 16) / 10;
}

bool GPUCommon::PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags) {
	// Track stray copies of a framebuffer in RAM. MotoGP does this.
	if (framebufferManager_->MayIntersectFramebuffer(src) || framebufferManager_->MayIntersectFramebuffer(dest)) {
		if (!framebufferManager_->NotifyFramebufferCopy(src, dest, size, flags, gstate_c.skipDrawReason)) {
			// We use matching values in PerformReadbackToMemory/PerformWriteColorFromMemory.
			// Since they're identical we don't need to copy.
			if (dest != src) {
				if (MemBlockInfoDetailed(size)) {
					char tag[128];
					size_t tagSize = FormatMemWriteTagAt(tag, sizeof(tag), "GPUMemcpy/", src, size);
					Memory::Memcpy(dest, src, size, tag, tagSize);
				} else {
					Memory::Memcpy(dest, src, size, "GPUMemcpy");
				}
			}
		}
		InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
		return true;
	}

	if (MemBlockInfoDetailed(size)) {
		char tag[128];
		size_t tagSize = FormatMemWriteTagAt(tag, sizeof(tag), "GPUMemcpy/", src, size);
		NotifyMemInfo(MemBlockFlags::READ, src, size, tag, tagSize);
		NotifyMemInfo(MemBlockFlags::WRITE, dest, size, tag, tagSize);
	}
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	if (!(flags & GPUCopyFlag::DEBUG_NOTIFIED))
		GPURecord::NotifyMemcpy(dest, src, size);
	return false;
}

bool GPUCommon::PerformMemorySet(u32 dest, u8 v, int size) {
	// This may indicate a memset, usually to 0, of a framebuffer.
	if (framebufferManager_->MayIntersectFramebuffer(dest)) {
		Memory::Memset(dest, v, size, "GPUMemset");
		if (!framebufferManager_->NotifyFramebufferCopy(dest, dest, size, GPUCopyFlag::MEMSET, gstate_c.skipDrawReason)) {
			InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
		}
		return true;
	}

	NotifyMemInfo(MemBlockFlags::WRITE, dest, size, "GPUMemset");
	// Or perhaps a texture, let's invalidate.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	GPURecord::NotifyMemset(dest, v, size);
	return false;
}

bool GPUCommon::PerformReadbackToMemory(u32 dest, int size) {
	if (Memory::IsVRAMAddress(dest)) {
		return PerformMemoryCopy(dest, dest, size, GPUCopyFlag::FORCE_DST_MEM);
	}
	return false;
}

bool GPUCommon::PerformWriteColorFromMemory(u32 dest, int size) {
	if (Memory::IsVRAMAddress(dest)) {
		GPURecord::NotifyUpload(dest, size);
		return PerformMemoryCopy(dest, dest, size, GPUCopyFlag::FORCE_SRC_MEM | GPUCopyFlag::DEBUG_NOTIFIED);
	}
	return false;
}

void GPUCommon::InvalidateCache(u32 addr, int size, GPUInvalidationType type) {
	if (size > 0)
		textureCache_->Invalidate(addr, size, type);
	else
		textureCache_->InvalidateAll(type);

	if (type != GPU_INVALIDATE_ALL && framebufferManager_->MayIntersectFramebuffer(addr)) {
		// Vempire invalidates (with writeback) after drawing, but before blitting.
		// TODO: Investigate whether we can get this to work some other way.
		if (type == GPU_INVALIDATE_SAFE) {
			framebufferManager_->UpdateFromMemory(addr, size);
		}
	}
}

void GPUCommon::PerformWriteFormattedFromMemory(u32 addr, int size, int frameWidth, GEBufferFormat format) {
	if (Memory::IsVRAMAddress(addr)) {
		framebufferManager_->PerformWriteFormattedFromMemory(addr, size, frameWidth, format);
	}
	textureCache_->NotifyWriteFormattedFromMemory(addr, size, frameWidth, format);
	InvalidateCache(addr, size, GPU_INVALIDATE_SAFE);
}

bool GPUCommon::PerformWriteStencilFromMemory(u32 dest, int size, WriteStencil flags) {
	if (framebufferManager_->MayIntersectFramebuffer(dest)) {
		framebufferManager_->PerformWriteStencilFromMemory(dest, size, flags);
		return true;
	}
	return false;
}

bool GPUCommon::GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) {
	u32 fb_address = type == GPU_DBG_FRAMEBUF_RENDER ? (gstate.getFrameBufRawAddress() | 0x04000000) : framebufferManager_->DisplayFramebufAddr();
	int fb_stride = type == GPU_DBG_FRAMEBUF_RENDER ? gstate.FrameBufStride() : framebufferManager_->DisplayFramebufStride();
	GEBufferFormat format = type == GPU_DBG_FRAMEBUF_RENDER ? gstate_c.framebufFormat : framebufferManager_->DisplayFramebufFormat();
	return framebufferManager_->GetFramebuffer(fb_address, fb_stride, format, buffer, maxRes);
}

bool GPUCommon::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress() | 0x04000000;
	int fb_stride = gstate.FrameBufStride();

	u32 z_address = gstate.getDepthBufRawAddress() | 0x04000000;
	int z_stride = gstate.DepthBufStride();

	return framebufferManager_->GetDepthbuffer(fb_address, fb_stride, z_address, z_stride, buffer);
}

bool GPUCommon::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress() | 0x04000000;
	int fb_stride = gstate.FrameBufStride();

	return framebufferManager_->GetStencilbuffer(fb_address, fb_stride, buffer);
}

bool GPUCommon::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	// framebufferManager_ can be null here when taking screens in software rendering mode.
	// TODO: Actually grab the framebuffer anyway.
	return framebufferManager_ ? framebufferManager_->GetOutputFramebuffer(buffer) : false;
}

std::vector<FramebufferInfo> GPUCommon::GetFramebufferList() const {
	return framebufferManager_->GetFramebufferList();
}

bool GPUCommon::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	UpdateUVScaleOffset();
	return drawEngineCommon_->GetCurrentSimpleVertices(count, vertices, indices);
}

bool GPUCommon::GetCurrentClut(GPUDebugBuffer &buffer) {
	return textureCache_->GetCurrentClutBuffer(buffer);
}

bool GPUCommon::GetCurrentTexture(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) {
	if (!gstate.isTextureMapEnabled()) {
		return false;
	}
	return textureCache_->GetCurrentTextureDebug(buffer, level, isFramebuffer);
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

void GPUCommon::UpdateUVScaleOffset() {
#ifdef _M_SSE
	__m128i values = _mm_slli_epi32(_mm_load_si128((const __m128i *) & gstate.texscaleu), 8);
	_mm_storeu_si128((__m128i *)&gstate_c.uv, values);
#elif PPSSPP_ARCH(ARM_NEON)
	const uint32x4_t values = vshlq_n_u32(vld1q_u32((const u32 *)&gstate.texscaleu), 8);
	vst1q_u32((u32 *)&gstate_c.uv, values);
#else
	gstate_c.uv.uScale = getFloat24(gstate.texscaleu);
	gstate_c.uv.vScale = getFloat24(gstate.texscalev);
	gstate_c.uv.uOff = getFloat24(gstate.texoffsetu);
	gstate_c.uv.vOff = getFloat24(gstate.texoffsetv);
#endif
}

size_t GPUCommon::FormatGPUStatsCommon(char *buffer, size_t size) {
	float vertexAverageCycles = gpuStats.numVertsSubmitted > 0 ? (float)gpuStats.vertexGPUCycles / (float)gpuStats.numVertsSubmitted : 0.0f;
	return snprintf(buffer, size,
		"DL processing time: %0.2f ms\n"
		"Draw calls: %d, flushes %d, clears %d (cached: %d)\n"
		"Num Tracked Vertex Arrays: %d\n"
		"Commands per call level: %i %i %i %i\n"
		"Vertices: %d cached: %d uncached: %d\n"
		"FBOs active: %d (evaluations: %d)\n"
		"Textures: %d, dec: %d, invalidated: %d, hashed: %d kB\n"
		"readbacks %d, uploads %d, depal %d\n"
		"Copies: depth %d, color %d, reint %d, blend %d, selftex %d\n"
		"GPU cycles executed: %d (%f per vertex)\n",
		gpuStats.msProcessingDisplayLists * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numClears,
		gpuStats.numCachedDrawCalls,
		gpuStats.numTrackedVertexArrays,
		gpuStats.gpuCommandsAtCallLevel[0], gpuStats.gpuCommandsAtCallLevel[1], gpuStats.gpuCommandsAtCallLevel[2], gpuStats.gpuCommandsAtCallLevel[3],
		gpuStats.numVertsSubmitted,
		gpuStats.numCachedVertsDrawn,
		gpuStats.numUncachedVertsDrawn,
		(int)framebufferManager_->NumVFBs(),
		gpuStats.numFramebufferEvaluations,
		(int)textureCache_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		gpuStats.numTextureDataBytesHashed / 1024,
		gpuStats.numReadbacks,
		gpuStats.numUploads,
		gpuStats.numDepal,
		gpuStats.numDepthCopies,
		gpuStats.numColorCopies,
		gpuStats.numReinterpretCopies,
		gpuStats.numCopiesForShaderBlend,
		gpuStats.numCopiesForSelfTex,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles
	);
}

u32 GPUCommon::CheckGPUFeatures() const {
	u32 features = 0;
	if (draw_->GetDeviceCaps().logicOpSupported) {
		features |= GPU_USE_LOGIC_OP;
	}
	if (draw_->GetDeviceCaps().anisoSupported) {
		features |= GPU_USE_ANISOTROPY;
	}
	if (draw_->GetDeviceCaps().textureNPOTFullySupported) {
		features |= GPU_USE_TEXTURE_NPOT;
	}
	if (draw_->GetDeviceCaps().dualSourceBlend) {
		if (!g_Config.bVendorBugChecksEnabled || !draw_->GetBugs().Has(Draw::Bugs::DUAL_SOURCE_BLENDING_BROKEN)) {
			features |= GPU_USE_DUALSOURCE_BLEND;
		}
	}
	if (draw_->GetDeviceCaps().blendMinMaxSupported) {
		features |= GPU_USE_BLEND_MINMAX;
	}

	if (draw_->GetDeviceCaps().clipDistanceSupported) {
		features |= GPU_USE_CLIP_DISTANCE;
	}

	if (draw_->GetDeviceCaps().cullDistanceSupported) {
		features |= GPU_USE_CULL_DISTANCE;
	}

	if (draw_->GetDeviceCaps().textureDepthSupported) {
		features |= GPU_USE_DEPTH_TEXTURE;
	}

	if (draw_->GetDeviceCaps().depthClampSupported) {
		// Some backends always do GPU_USE_ACCURATE_DEPTH, but it's required for depth clamp.
		features |= GPU_USE_DEPTH_CLAMP | GPU_USE_ACCURATE_DEPTH;
	}

	bool canClipOrCull = draw_->GetDeviceCaps().clipDistanceSupported || draw_->GetDeviceCaps().cullDistanceSupported;
	bool canDiscardVertex = draw_->GetBugs().Has(Draw::Bugs::BROKEN_NAN_IN_CONDITIONAL);
	if (canClipOrCull || canDiscardVertex) {
		// We'll dynamically use the parts that are supported, to reduce artifacts as much as possible.
		features |= GPU_USE_VS_RANGE_CULLING;
	}

	if (draw_->GetDeviceCaps().framebufferFetchSupported) {
		features |= GPU_USE_FRAMEBUFFER_FETCH;
	}

	if (draw_->GetShaderLanguageDesc().bitwiseOps) {
		features |= GPU_USE_LIGHT_UBERSHADER;
	}

	if (PSP_CoreParameter().compat.flags().ClearToRAM) {
		features |= GPU_USE_CLEAR_RAM_HACK;
	}

	// Even without depth clamp, force accurate depth on for some games that break without it.
	if (PSP_CoreParameter().compat.flags().DepthRangeHack) {
		features |= GPU_USE_ACCURATE_DEPTH;
	}

	return features;
}

u32 GPUCommon::CheckGPUFeaturesLate(u32 features) const {
	// If we already have a 16-bit depth buffer, we don't need to round.
	bool prefer24 = draw_->GetDeviceCaps().preferredDepthBufferFormat == Draw::DataFormat::D24_S8;
	bool prefer16 = draw_->GetDeviceCaps().preferredDepthBufferFormat == Draw::DataFormat::D16;
	if (!prefer16) {
		if (sawExactEqualDepth_ && (features & GPU_USE_ACCURATE_DEPTH) != 0) {
			// Exact equal tests tend to have issues unless we use the PSP's depth range.
			// We use 24-bit depth virtually everwhere, the fallback is just for safety.
			if (prefer24)
				features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
			else
				features |= GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
		} else if (!g_Config.bHighQualityDepth && (features & GPU_USE_ACCURATE_DEPTH) != 0) {
			features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
		} else if (PSP_CoreParameter().compat.flags().PixelDepthRounding) {
			if (prefer24 && (features & GPU_USE_ACCURATE_DEPTH) != 0) {
				// Here we can simulate a 16 bit depth buffer by scaling.
				// Note that the depth buffer is fixed point, not floating, so dividing by 256 is pretty good.
				features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
			} else {
				// Use fragment rounding on where available otherwise.
				features |= GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
			}
		} else if (PSP_CoreParameter().compat.flags().VertexDepthRounding) {
			features |= GPU_ROUND_DEPTH_TO_16BIT;
		}
	}

	return features;
}

void GPUCommon::UpdateMSAALevel(Draw::DrawContext *draw) {
	int level = g_Config.iMultiSampleLevel;
	if (draw && draw->GetDeviceCaps().multiSampleLevelsMask & (1 << level)) {
		msaaLevel_ = level;
	} else {
		// Didn't support the configured level, so revert to 0.
		msaaLevel_ = 0;
	}
}
