set(SRCS
	GPUCommon.cpp
	GPUState.cpp
	Math3D.cpp
	GLES/DisplayListInterpreter.cpp
	GLES/FragmentShaderGenerator.cpp
	GLES/Framebuffer.cpp
	GLES/IndexGenerator.cpp
	GLES/ShaderManager.cpp
	GLES/StateMapping.cpp
	GLES/TextureCache.cpp
	GLES/TextureScaler.cpp
	GLES/TransformPipeline.cpp
	GLES/VertexDecoder.cpp
	GLES/VertexShaderGenerator.cpp
	Null/NullGpu.cpp
)

set(SRCS ${SRCS})

add_library(gpu STATIC ${SRCS})
target_link_libraries(gpu general gfx_es2)
target_link_libraries(gpu general lin)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

