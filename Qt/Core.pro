QT += opengl
QT -= gui
TARGET = Core
TEMPLATE = lib
CONFIG += staticlib

blackberry: {
	QMAKE_CC = ntoarmv7-gcc
	QMAKE_CXX = ntoarmv7-g++
	DEFINES += "_QNX_SOURCE=1" "_C99=1"
}

INCLUDEPATH += ../native ../Common ../

SOURCES += ../Core/MIPS/ARM/Asm.cpp \ #CoreARM
			../Core/MIPS/ARM/CompALU.cpp \
			../Core/MIPS/ARM/CompBranch.cpp \
			../Core/MIPS/ARM/CompFPU.cpp \
			../Core/MIPS/ARM/CompLoadStore.cpp \
			../Core/MIPS/ARM/CompVFPU.cpp \
			../Core/MIPS/ARM/Jit.cpp \
			../Core/MIPS/ARM/JitCache.cpp \
			../Core/MIPS/ARM/RegCache.cpp \
			../Core/CPU.cpp \ # Core
			../Core/Config.cpp \
			../Core/Core.cpp \
			../Core/CoreTiming.cpp \
			../Core/Debugger/Breakpoints.cpp \
			../Core/Debugger/SymbolMap.cpp \
			../Core/Dialog/PSPDialog.cpp \
			../Core/Dialog/PSPMsgDialog.cpp \
			../Core/Dialog/PSPOskDialog.cpp \
			../Core/Dialog/PSPPlaceholderDialog.cpp \
			../Core/Dialog/PSPSaveDialog.cpp \
			../Core/Dialog/SavedataParam.cpp \
			../Core/ELF/ElfReader.cpp \
			../Core/ELF/PrxDecrypter.cpp \
			../Core/ELF/ParamSFO.cpp \
			../Core/FileSystems/BlockDevices.cpp \
			../Core/FileSystems/DirectoryFileSystem.cpp \
			../Core/FileSystems/ISOFileSystem.cpp \
			../Core/FileSystems/MetaFileSystem.cpp \
			../Core/HLE/HLE.cpp \
			../Core/HLE/HLETables.cpp \
			../Core/HLE/__sceAudio.cpp \
			../Core/HLE/sceAtrac.cpp \
			../Core/HLE/sceAudio.cpp \
			../Core/HLE/sceCtrl.cpp \
			../Core/HLE/sceDisplay.cpp \
			../Core/HLE/sceDmac.cpp \
			../Core/HLE/sceGe.cpp \
			../Core/HLE/sceFont.cpp \
			../Core/HLE/sceHprm.cpp \
			../Core/HLE/sceHttp.cpp \
			../Core/HLE/sceImpose.cpp \
			../Core/HLE/sceIo.cpp \
			../Core/HLE/sceKernel.cpp \
			../Core/HLE/sceKernelAlarm.cpp \
			../Core/HLE/sceKernelEventFlag.cpp \
			../Core/HLE/sceKernelInterrupt.cpp \
			../Core/HLE/sceKernelMbx.cpp \
			../Core/HLE/sceKernelMemory.cpp \
			../Core/HLE/sceKernelModule.cpp \
			../Core/HLE/sceKernelMsgPipe.cpp \
			../Core/HLE/sceKernelMutex.cpp \
			../Core/HLE/sceKernelSemaphore.cpp \
			../Core/HLE/sceKernelThread.cpp \
			../Core/HLE/sceKernelThread.h \
			../Core/HLE/sceKernelTime.cpp \
			../Core/HLE/sceKernelVTimer.cpp \
			../Core/HLE/sceMpeg.cpp \
			../Core/HLE/sceNet.cpp \
			../Core/HLE/sceOpenPSID.cpp \
			../Core/HLE/sceParseHttp.cpp \
			../Core/HLE/sceParseUri.cpp \
			../Core/HLE/scePower.cpp \
			../Core/HLE/scePsmf.cpp \
			../Core/HLE/sceRtc.cpp \
			../Core/HLE/sceSas.cpp \
			../Core/HLE/sceSsl.cpp \
			../Core/HLE/scesupPreAcc.cpp \
			../Core/HLE/sceUmd.cpp \
			../Core/HLE/sceUsb.cpp \
			../Core/HLE/sceUtility.cpp \
			../Core/HLE/sceVaudio.cpp \
			../Core/HW/MemoryStick.cpp \
			../Core/HW/SasAudio.cpp \
			../Core/Host.cpp \
			../Core/Loaders.cpp \
			../Core/MIPS/JitCommon/JitCommon.cpp \
			../Core/MIPS/MIPS.cpp \
			../Core/MIPS/MIPSAnalyst.cpp \
			../Core/MIPS/MIPSCodeUtils.cpp \
			../Core/MIPS/MIPSDebugInterface.cpp \
			../Core/MIPS/MIPSDis.cpp \
			../Core/MIPS/MIPSDisVFPU.cpp \
			../Core/MIPS/MIPSInt.cpp \
			../Core/MIPS/MIPSIntVFPU.cpp \
			../Core/MIPS/MIPSTables.cpp \
			../Core/MIPS/MIPSVFPUUtils.cpp \
			../Core/MemMap.cpp \
			../Core/MemMapFunctions.cpp \
			../Core/PSPLoaders.cpp \
			../Core/PSPMixer.cpp \
			../Core/System.cpp \
			../Core/Util/BlockAllocator.cpp \
			../Core/Util/PPGeDraw.cpp \
			../Core/Util/ppge_atlas.cpp \ # GPU
			../GPU/GLES/DisplayListInterpreter.cpp \
			../GPU/GLES/FragmentShaderGenerator.cpp \
			../GPU/GLES/Framebuffer.cpp \
			../GPU/GLES/ShaderManager.cpp \
			../GPU/GLES/StateMapping.cpp \
			../GPU/GLES/TextureCache.cpp \
			../GPU/GLES/TransformPipeline.cpp \
			../GPU/GLES/VertexDecoder.cpp \
			../GPU/GLES/VertexShaderGenerator.cpp \
			../GPU/GPUState.cpp \
			../GPU/Math3D.cpp \
			../GPU/Null/NullGpu.cpp \ # Kirk
			../ext/libkirk/AES.c \
			../ext/libkirk/SHA1.c \
			../ext/libkirk/bn.c \
			../ext/libkirk/ec.c \
			../ext/libkirk/kirk_engine.c

HEADERS += ../Core/MIPS/ARM/Asm.h \
			../Core/MIPS/ARM/Jit.h \
			../Core/MIPS/ARM/JitCache.h \
			../Core/MIPS/ARM/RegCache.h \
			../Core/CPU.h \
			../Core/Config.h \
			../Core/Core.h \
			../Core/CoreParameter.h \
			../Core/CoreTiming.h \
			../Core/Debugger/Breakpoints.h \
			../Core/Debugger/DebugInterface.h \
			../Core/Debugger/SymbolMap.h \
			../Core/Dialog/PSPDialog.h \
			../Core/Dialog/PSPMsgDialog.h \
			../Core/Dialog/PSPOskDialog.h \
			../Core/Dialog/PSPPlaceholderDialog.h \
			../Core/Dialog/PSPSaveDialog.h \
			../Core/Dialog/SavedataParam.h \
			../Core/ELF/ElfReader.h \
			../Core/ELF/ElfTypes.h \
			../Core/ELF/PrxDecrypter.h \
			../Core/ELF/ParamSFO.h \
			../Core/FileSystems/BlockDevices.h \
			../Core/FileSystems/DirectoryFileSystem.h \
			../Core/FileSystems/FileSystem.h \
			../Core/FileSystems/ISOFileSystem.h \
			../Core/FileSystems/MetaFileSystem.h \
			../Core/HLE/FunctionWrappers.h \
			../Core/HLE/HLE.h \
			../Core/HLE/HLETables.h \
			../Core/HLE/__sceAudio.h \
			../Core/HLE/sceAtrac.h \
			../Core/HLE/sceAudio.h \
			../Core/HLE/sceCtrl.h \
			../Core/HLE/sceDisplay.h \
			../Core/HLE/sceDmac.h \
			../Core/HLE/sceGe.h \
			../Core/HLE/sceFont.h \
			../Core/HLE/sceHprm.h \
			../Core/HLE/sceHttp.h \
			../Core/HLE/sceImpose.h \
			../Core/HLE/sceIo.h \
			../Core/HLE/sceKernel.h \
			../Core/HLE/sceKernelAlarm.h \
			../Core/HLE/sceKernelEventFlag.h \
			../Core/HLE/sceKernelInterrupt.h \
			../Core/HLE/sceKernelMbx.h \
			../Core/HLE/sceKernelMemory.h \
			../Core/HLE/sceKernelModule.h \
			../Core/HLE/sceKernelMsgPipe.h \
			../Core/HLE/sceKernelMutex.h \
			../Core/HLE/sceKernelSemaphore.h \
			../Core/HLE/sceMpeg.h \
			../Core/HLE/sceNet.h \
			../Core/HLE/sceOpenPSID.h \
			../Core/HLE/sceParseHttp.h \
			../Core/HLE/sceParseUri.h \
			../Core/HLE/scePower.h \
			../Core/HLE/scePsmf.h \
			../Core/HLE/sceRtc.h \
			../Core/HLE/sceSas.h \
			../Core/HLE/sceSsl.h \
			../Core/HLE/scesupPreAcc.h \
			../Core/HLE/sceUmd.h \
			../Core/HLE/sceUsb.h \
			../Core/HLE/sceUtility.h \
			../Core/HLE/sceVaudio.h \
			../Core/HW/MemoryStick.h \
			../Core/HW/SasAudio.h \
			../Core/Host.h \
			../Core/Loaders.h \
			../Core/MIPS/JitCommon/JitCommon.h \
			../Core/MIPS/MIPS.h \
			../Core/MIPS/MIPSAnalyst.h \
			../Core/HLE/sceKernelTime.h \
			../Core/HLE/sceKernelVTimer.h \
			../Core/MIPS/MIPSCodeUtils.h \
			../Core/MIPS/MIPSDebugInterface.h \
			../Core/MIPS/MIPSDis.h \
			../Core/MIPS/MIPSDisVFPU.h \
			../Core/MIPS/MIPSInt.h \
			../Core/MIPS/MIPSIntVFPU.h \
			../Core/MIPS/MIPSTables.h \
			../Core/MIPS/MIPSVFPUUtils.h \
			../Core/MemMap.h \
			../Core/PSPLoaders.h \
			../Core/PSPMixer.h \
			../Core/System.h \
			../Core/Util/BlockAllocator.h \
			../Core/Util/PPGeDraw.h \
			../Core/Util/Pool.h \
			../Core/Util/ppge_atlas.h \
			../GPU/GLES/DisplayListInterpreter.h \
			../GPU/GLES/FragmentShaderGenerator.h \
			../GPU/GLES/Framebuffer.h \
			../GPU/GLES/ShaderManager.h \
			../GPU/GLES/StateMapping.h \
			../GPU/GLES/TextureCache.h \
			../GPU/GLES/TransformPipeline.h \
			../GPU/GLES/VertexDecoder.h \
			../GPU/GLES/VertexShaderGenerator.h \
			../GPU/GPUInterface.h \
			../GPU/GPUState.h \
			../GPU/Math3D.h \
			../GPU/Null/NullGpu.h \
			../GPU/ge_constants.h \
			../ext/libkirk/AES.h \
			../ext/libkirk/SHA1.h \
			../ext/libkirk/kirk_engine.h

QMAKE_CXXFLAGS += -std=c++0x -Wno-unused-function -Wno-unused-variable -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-unused-parameter
DEFINES += ARM USING_GLES2
blackberry: DEFINES += BLACKBERRY BLACKBERRY10
symbian: {
	QMAKE_CXXFLAGS += -march=armv6 -mfpu=vfp -mfloat-abi=softfp -marm -Wno-parentheses -Wno-comment
	DEFINES += SYMBIAN
}
