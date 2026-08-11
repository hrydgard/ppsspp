set(GPUArch)
list(APPEND GPUArch
	Common/VertexDecoderArm.cpp
	Common/VertexDecoderArm64.cpp
	Common/VertexDecoderX86.cpp
	Software/DrawPixelX86.cpp
	Software/SamplerX86.cpp
	Common/VertexDecoderRiscV.cpp
	Common/VertexDecoderLoongArch64.cpp
)

set(GPU_GLES
	GLES/StencilBufferGLES.cpp
	GLES/GPU_GLES.cpp
	GLES/GPU_GLES.h
	GLES/FragmentTestCacheGLES.cpp
	GLES/FragmentTestCacheGLES.h
	GLES/FramebufferManagerGLES.cpp
	GLES/FramebufferManagerGLES.h
	GLES/ShaderManagerGLES.cpp
	GLES/ShaderManagerGLES.h
	GLES/StateMappingGLES.cpp
	GLES/StateMappingGLES.h
	GLES/TextureCacheGLES.cpp
	GLES/TextureCacheGLES.h
	GLES/DrawEngineGLES.cpp
	GLES/DrawEngineGLES.h
)

set(GPU_VULKAN
	Vulkan/DebugVisVulkan.cpp
	Vulkan/DebugVisVulkan.h
	Vulkan/DrawEngineVulkan.cpp
	Vulkan/DrawEngineVulkan.h
	Vulkan/FramebufferManagerVulkan.cpp
	Vulkan/FramebufferManagerVulkan.h
	Vulkan/GPU_Vulkan.cpp
	Vulkan/GPU_Vulkan.h
	Vulkan/PipelineManagerVulkan.cpp
	Vulkan/PipelineManagerVulkan.h
	Vulkan/ShaderManagerVulkan.cpp
	Vulkan/ShaderManagerVulkan.h
	Vulkan/StateMappingVulkan.cpp
	Vulkan/StateMappingVulkan.h
	Vulkan/TextureCacheVulkan.cpp
	Vulkan/TextureCacheVulkan.h
	Vulkan/VulkanUtil.cpp
	Vulkan/VulkanUtil.h
)

set(GPU_D3D11
	D3D11/DrawEngineD3D11.cpp
	D3D11/DrawEngineD3D11.h
	D3D11/FramebufferManagerD3D11.cpp
	D3D11/FramebufferManagerD3D11.h
	D3D11/GPU_D3D11.cpp
	D3D11/GPU_D3D11.h
	D3D11/D3D11Util.cpp
	D3D11/D3D11Util.h
	D3D11/ShaderManagerD3D11.cpp
	D3D11/ShaderManagerD3D11.h
	D3D11/StateMappingD3D11.cpp
	D3D11/StateMappingD3D11.h
	D3D11/TextureCacheD3D11.cpp
	D3D11/TextureCacheD3D11.h
)

# We build Vulkan even on Apple to avoid annoying build differences.
set(GPU_IMPLS ${GPU_GLES} ${GPU_VULKAN})
if(WIN32)
	list(APPEND GPU_IMPLS ${GPU_D3D11})
endif()

set(GPU_SOURCES
	${GPUArch}
	${GPU_IMPLS}
	${GPU_NEON}
	Common/Draw2D.cpp
	Common/Draw2D.h
	Common/DepthBufferCommon.cpp
	Common/DepthRaster.cpp
	Common/DepthRaster.h
	Common/TextureShaderCommon.cpp
	Common/TextureShaderCommon.h
	Common/DepalettizeShaderCommon.cpp
	Common/DepalettizeShaderCommon.h
	Common/FragmentShaderGenerator.cpp
	Common/FragmentShaderGenerator.h
	Common/VertexShaderGenerator.cpp
	Common/VertexShaderGenerator.h
	Common/FramebufferManagerCommon.cpp
	Common/FramebufferManagerCommon.h
	Common/GPUDebugInterface.cpp
	Common/GPUDebugInterface.h
	Common/GPUStateUtils.cpp
	Common/GPUStateUtils.h
	Common/DrawEngineCommon.cpp
	Common/DrawEngineCommon.h
	Common/PresentationCommon.cpp
	Common/PresentationCommon.h
	Common/ReinterpretFramebuffer.cpp
	Common/ReinterpretFramebuffer.h
	Common/ShaderId.cpp
	Common/ShaderId.h
	Common/ShaderUniforms.cpp
	Common/ShaderUniforms.h
	Common/ShaderCommon.cpp
	Common/ShaderCommon.h
	Common/SplineCommon.cpp
	Common/SplineCommon.h
	Common/StencilCommon.cpp
	Common/StencilCommon.h
	Common/SoftwareTransformCommon.cpp
	Common/SoftwareTransformCommon.h
	Common/VertexDecoderCommon.cpp
	Common/VertexDecoderCommon.h
	Common/VertexDecoderHandwritten.cpp
	Common/VertexDecoderHandwritten.h
	Common/VertexReader.h
	Common/TransformCommon.cpp
	Common/TransformCommon.h
	Common/IndexGenerator.cpp
	Common/IndexGenerator.h
	Common/ImageCommon.h
	Common/TextureDecoder.cpp
	Common/TextureDecoder.h
	Common/TextureCacheCommon.cpp
	Common/TextureCacheCommon.h
	Common/TextureScalerCommon.cpp
	Common/TextureScalerCommon.h
	Common/PostShader.cpp
	Common/PostShader.h
	Common/TextureReplacer.cpp
	Common/TextureReplacer.h
	Common/ReplacedTexture.cpp
	Common/ReplacedTexture.h
	Debugger/Breakpoints.cpp
	Debugger/Breakpoints.h
	Debugger/Debugger.cpp
	Debugger/Debugger.h
	Debugger/GECommandTable.cpp
	Debugger/GECommandTable.h
	Debugger/Playback.cpp
	Debugger/Playback.h
	Debugger/Record.cpp
	Debugger/Record.h
	Debugger/RecordFormat.h
	Debugger/State.cpp
	Debugger/State.h
	Debugger/Stepping.cpp
	Debugger/Stepping.h
	ge_constants.h
	GeConstants.cpp
	GPUDefinitions.h
	GeDisasm.cpp
	GeDisasm.h
	GPU.cpp
	GPU.h
	GPUCommon.cpp
	GPUCommon.h
	GPUCommonHW.cpp
	GPUCommonHW.h
	GPUState.cpp
	GPUState.h
	GPUStateSIMDUtil.h
	Math3D.cpp
	Math3D.h
	Software/BinManager.cpp
	Software/BinManager.h
	Software/Clipper.cpp
	Software/Clipper.h
	Software/DrawPixel.cpp
	Software/DrawPixel.h
	Software/FuncId.cpp
	Software/FuncId.h
	Software/Lighting.cpp
	Software/Lighting.h
	Software/Rasterizer.cpp
	Software/Rasterizer.h
	Software/RasterizerRectangle.cpp
	Software/RasterizerRectangle.h
	Software/RasterizerRegCache.cpp
	Software/RasterizerRegCache.h
	Software/Sampler.cpp
	Software/Sampler.h
	Software/SoftGpu.cpp
	Software/SoftGpu.h
	Software/TransformUnit.cpp
	Software/TransformUnit.h
)

# GPU has a genuine two-way dependency with Core (Core/System.cpp calls GPU_Init(), while
# all of GPU/ calls back into Core for Memory/Config/CoreTiming/etc). An OBJECT library's
# object files are always included wherever it's consumed rather than being lazily pulled
# from an archive, which sidesteps the GNU ld single-pass archive-ordering problem a normal
# two-way STATIC dependency would hit. Core links this PUBLICLY so consumers of Core get it.
add_library(GPU OBJECT ${GPU_SOURCES})
target_compile_features(GPU PUBLIC cxx_std_17)

if(LIBRETRO)
	target_include_directories(GPU PRIVATE ${CMAKE_SOURCE_DIR}/libretro/libretro-common/include)
endif()

setup_target_project(GPU GPU)

# The only direct external dependencies of GPU/ itself (as opposed to Common/GPU/*, which
# already carries its own GL/Vulkan loader abstractions): raw gl*() calls in GPU/GLES, direct
# ZSTD_compress/decompress calls in GPU/Debugger/Record.cpp and Playback.cpp, and Playback.cpp
# also calls snappy_uncompress() directly.
target_link_libraries(GPU PUBLIC ${OPENGL_LIBRARIES} ${X11_LIBRARIES} Ext::Snappy)

if(USE_SYSTEM_ZSTD)
	target_include_directories(GPU PRIVATE ${ZSTD_INCLUDE_DIR})
	target_link_libraries(GPU PUBLIC ${ZSTD_LIBRARY})
else()
	target_link_libraries(GPU PUBLIC libzstd_static)
endif()
