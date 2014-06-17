QT += opengl
QT -= gui
TARGET = GPU
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

INCLUDEPATH += $$P/ $$P/native
!contains(DEFINES, USING_GLES2): INCLUDEPATH += $$P/native/ext/glew

win32 {
	SOURCES += $$P/Windows/OpenGLBase.cpp
	HEADERS += $$P/Windows/OpenGLBase.h

	SOURCES += $$P/GPU/Directx9/helper/*.cpp
	HEADERS += $$P/GPU/Directx9/helper/*.h

	SOURCES += $$P/GPU/Directx9/*.cpp
	HEADERS += $$P/GPU/Directx9/*.h
	INCLUDEPATH += $$P/dx9sdk/Include
}

SOURCES += 	$$P/GPU/GeDisasm.cpp \ # GPU
	$$P/GPU/GPUCommon.cpp \
	$$P/GPU/GPUState.cpp \
	$$P/GPU/Math3D.cpp \
	$$P/GPU/Null/NullGpu.cpp \
	$$P/GPU/GLES/DepalettizeShader.cpp \
	$$P/GPU/GLES/FragmentShaderGenerator.cpp \
	$$P/GPU/GLES/Framebuffer.cpp \
	$$P/GPU/GLES/GLES_GPU.cpp \
	$$P/GPU/GLES/ShaderManager.cpp \
	$$P/GPU/GLES/SoftwareTransform.cpp \
	$$P/GPU/GLES/Spline.cpp \
	$$P/GPU/GLES/StateMapping.cpp \
	$$P/GPU/GLES/StencilBuffer.cpp \
	$$P/GPU/GLES/TextureCache.cpp \
	$$P/GPU/GLES/TextureScaler.cpp \
	$$P/GPU/GLES/TransformPipeline.cpp \
	$$P/GPU/GLES/VertexDecoder.cpp \
	$$P/GPU/GLES/VertexShaderGenerator.cpp \
	$$P/GPU/Software/*.cpp \
	$$P/GPU/Debugger/*.cpp \
	$$P/GPU/Common/IndexGenerator.cpp \
	$$P/GPU/Common/TextureDecoder.cpp \
	$$P/GPU/Common/VertexDecoderCommon.cpp \
	$$P/GPU/Common/TransformCommon.cpp \
	$$P/GPU/Common/PostShader.cpp \
	$$P/ext/xxhash.c \ # xxHash
	$$P/ext/xbrz/*.cpp # XBRZ

armv7: SOURCES += $$P/GPU/Common/TextureDecoderNEON.cpp

arm: SOURCES += $$P/GPU/GLES/VertexDecoderArm.cpp
else: SOURCES += $$P/GPU/GLES/VertexDecoderX86.cpp

HEADERS += 	$$P/GPU/GLES/*.h \
	$$P/GPU/Software/*.h \
	$$P/GPU/Debugger/*.h \
	$$P/GPU/Common/*.h \
	$$P/GPU/*.h \
	$$P/ext/xbrz/*.h
