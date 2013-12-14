QT += opengl
QT -= gui
TARGET = Core
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

INCLUDEPATH += $$P/native $$P/Core/MIPS $$P/ $$P/ext/xbrz

arm {
	SOURCES += $$P/Core/MIPS/ARM/*.cpp \ #CoreARM
		$$P/ext/disarm.cpp
	HEADERS += $$P/Core/MIPS/ARM/*.h
}
x86 {
	SOURCES += $$P/Core/MIPS/x86/*.cpp
	HEADERS += $$P/Core/MIPS/x86/*.h
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

SOURCES += $$P/Core/*.cpp \ # Core
	$$P/Core/Debugger/*.cpp \
	$$P/Core/Dialog/*.cpp \
	$$P/Core/ELF/*.cpp \
	$$P/Core/FileSystems/*.cpp \
	$$P/Core/Font/*.cpp \
	$$P/Core/HLE/*.cpp \
	$$P/Core/HW/*.cpp \
	$$P/Core/MIPS/*.cpp \
	$$P/Core/MIPS/JitCommon/*.cpp \
	$$P/Core/Util/*.cpp \
	$$P/GPU/GeDisasm.cpp \ # GPU
	$$P/GPU/GPUCommon.cpp \
	$$P/GPU/GPUState.cpp \
	$$P/GPU/Math3D.cpp \
	$$P/GPU/Null/NullGpu.cpp \
	$$P/GPU/GLES/FragmentShaderGenerator.cpp \
	$$P/GPU/GLES/Framebuffer.cpp \
	$$P/GPU/GLES/GLES_GPU.cpp \
	$$P/GPU/GLES/ShaderManager.cpp \
	$$P/GPU/GLES/SoftwareTransform.cpp \
	$$P/GPU/GLES/Spline.cpp \
	$$P/GPU/GLES/StateMapping.cpp \
	$$P/GPU/GLES/TextureCache.cpp \
	$$P/GPU/GLES/TextureScaler.cpp \
	$$P/GPU/GLES/TransformPipeline.cpp \
	$$P/GPU/GLES/VertexDecoder.cpp \
	$$P/GPU/GLES/VertexShaderGenerator.cpp \
	$$P/GPU/Software/*.cpp \
	$$P/GPU/Common/IndexGenerator.cpp \
	$$P/GPU/Common/TextureDecoder.cpp \
	$$P/GPU/Common/VertexDecoderCommon.cpp \
	$$P/GPU/Common/PostShader.cpp \
	$$P/ext/libkirk/*.c \ # Kirk
	$$P/ext/xxhash.c \ # xxHash
	$$P/ext/xbrz/*.cpp # XBRZ

!x86:!symbian: SOURCES += $$P/GPU/Common/TextureDecoderNEON.cpp

arm: SOURCES += $$P/GPU/GLES/VertexDecoderArm.cpp
else:SOURCES += $$P/GPU/GLES/VertexDecoderX86.cpp

HEADERS += $$P/Core/*.h \
	$$P/Core/Debugger/*.h \
	$$P/Core/Dialog/*.h \
	$$P/Core/ELF/*.h \
	$$P/Core/FileSystems/*.h \
	$$P/Core/Font/*.h \
	$$P/Core/HLE/*.h \
	$$P/Core/HW/*.h \
	$$P/Core/MIPS/*.h \
	$$P/Core/MIPS/JitCommon/*.h \
	$$P/Core/Util/*.h \
	$$P/GPU/GLES/*.h \
	$$P/GPU/Software/*.h \
	$$P/GPU/Common/*.h \
	$$P/GPU/*.h \
	$$P/ext/libkirk/*.h \
	$$P/ext/xbrz/*.h

win32: INCLUDEPATH += $$P/ffmpeg/WindowsInclude

