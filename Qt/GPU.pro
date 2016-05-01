QT += opengl
QT -= gui
TARGET = GPU
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

INCLUDEPATH += $$P/ $$P/ext/native
!exists( /usr/include/GL/glew.h ) {
	!contains(DEFINES, USING_GLES2): INCLUDEPATH += $$P/ext/native/ext/glew
}

win32 {
	SOURCES += $$P/Windows/OpenGLBase.cpp
	HEADERS += $$P/Windows/OpenGLBase.h

	SOURCES += $$P/GPU/Directx9/helper/*.cpp
	HEADERS += $$P/GPU/Directx9/helper/*.h

	SOURCES += $$P/GPU/Directx9/*.cpp
	HEADERS += $$P/GPU/Directx9/*.h
	INCLUDEPATH += $$P/dx9sdk/Include
}

SOURCES += $$P/GPU/GeDisasm.cpp \ # GPU
	$$P/GPU/GPU.cpp \
	$$P/GPU/GPUCommon.cpp \
	$$P/GPU/GPUState.cpp \
	$$P/GPU/Math3D.cpp \
	$$P/GPU/Null/NullGpu.cpp \
	$$P/GPU/GLES/FBO.cpp \
	$$P/GPU/GLES/DepalettizeShader.cpp \
	$$P/GPU/GLES/FragmentShaderGenerator.cpp \
	$$P/GPU/GLES/FragmentTestCache.cpp \
	$$P/GPU/GLES/Framebuffer.cpp \
	$$P/GPU/GLES/GLStateCache.cpp \
	$$P/GPU/GLES/GPU_GLES.cpp \
	$$P/GPU/GLES/ShaderManager.cpp \
	$$P/GPU/GLES/StateMapping.cpp \
	$$P/GPU/GLES/StencilBuffer.cpp \
	$$P/GPU/GLES/TextureCache.cpp \
	$$P/GPU/GLES/TextureScaler.cpp \
	$$P/GPU/GLES/DrawEngineGLES.cpp \
	$$P/GPU/GLES/VertexShaderGenerator.cpp \
	$$P/GPU/Software/*.cpp \
	$$P/GPU/Debugger/*.cpp \
	$$P/GPU/Common/DepalettizeShaderCommon.cpp \
	$$P/GPU/Common/GPUDebugInterface.cpp \
	$$P/GPU/Common/GPUStateUtils.cpp \
	$$P/GPU/Common/ShaderId.cpp \
	$$P/GPU/Common/IndexGenerator.cpp \
	$$P/GPU/Common/TextureDecoder.cpp \
	$$P/GPU/Common/TextureScalerCommon.cpp \
	$$P/GPU/Common/VertexDecoderCommon.cpp \
	$$P/GPU/Common/TextureCacheCommon.cpp \
	$$P/GPU/Common/TransformCommon.cpp \
	$$P/GPU/Common/SoftwareTransformCommon.cpp \
	$$P/GPU/Common/PostShader.cpp \
	$$P/GPU/Common/FramebufferCommon.cpp \
	$$P/GPU/Common/SplineCommon.cpp \
	$$P/GPU/Common/DrawEngineCommon.cpp \
	$$P/ext/xxhash.c \ # xxHash
	$$P/ext/xbrz/*.cpp \ # XBRZ
	$$P/Core/TextureReplacer.cpp # Bit of a hack.  Avoids a linking issue.

armv7: SOURCES += $$P/GPU/Common/TextureDecoderNEON.cpp

arm: SOURCES += $$P/GPU/Common/VertexDecoderArm.cpp
else:i86: SOURCES += $$P/GPU/Common/VertexDecoderX86.cpp
else: SOURCES += $$P/GPU/Common/VertexDecoderFake.cpp

HEADERS += $$P/GPU/GLES/*.h \
	$$P/GPU/Software/*.h \
	$$P/GPU/Debugger/*.h \
	$$P/GPU/Common/*.h \
	$$P/GPU/*.h \
	$$P/ext/xbrz/*.h
