set(SRCS
	GPUState.cpp
	Math3D.cpp
	GLES/DisplayListInterpreter.cpp
	GLES/FragmentShaderGenerator.cpp
	GLES/Framebuffer.cpp
	GLES/ShaderManager.cpp
	GLES/TextureCache.cpp
	GLES/TransformPipeline.cpp
	GLES/VertexDecoder.cpp
	GLES/VertexShaderGenerator.cpp
)

set(SRCS ${SRCS})

add_library(gpu STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

