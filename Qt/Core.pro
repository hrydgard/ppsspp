QT += opengl
QT -= gui
TARGET = Core
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

INCLUDEPATH += ../native ../Common ../

arm {
	SOURCES += ../Core/MIPS/ARM/ArmAsm.cpp \ #CoreARM
		../Core/MIPS/ARM/ArmCompALU.cpp \
		../Core/MIPS/ARM/ArmCompBranch.cpp \
		../Core/MIPS/ARM/ArmCompFPU.cpp \
		../Core/MIPS/ARM/ArmCompLoadStore.cpp \
		../Core/MIPS/ARM/ArmCompVFPU.cpp \
		../Core/MIPS/ARM/ArmJit.cpp \
		../Core/MIPS/ARM/ArmJitCache.cpp \
		../Core/MIPS/ARM/ArmRegCache.cpp \
		../ext/disarm.cpp

	HEADERS += ../Core/MIPS/ARM/ArmAsm.h \
		../Core/MIPS/ARM/ArmJit.h \
		../Core/MIPS/ARM/ArmJitCache.h \
		../Core/MIPS/ARM/ArmRegCache.h
}
x86 {
	SOURCES += ../Core/MIPS/x86/Asm.cpp \
		../Core/MIPS/x86/CompALU.cpp \
		../Core/MIPS/x86/CompBranch.cpp \
		../Core/MIPS/x86/CompFPU.cpp \
		../Core/MIPS/x86/CompLoadStore.cpp \
		../Core/MIPS/x86/CompVFPU.cpp \
		../Core/MIPS/x86/Jit.cpp \
		../Core/MIPS/x86/JitCache.cpp \
		../Core/MIPS/x86/RegCache.cpp
	HEADERS += ../Core/MIPS/x86/Asm.h \
		../Core/MIPS/x86/Jit.h \
		../Core/MIPS/x86/JitCache.h \
		../Core/MIPS/x86/RegCache.h
}

SOURCES +=	../Core/CPU.cpp \ # Core
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
	../Core/HLE/sceUmd.cpp \
	../Core/HLE/sceUsb.cpp \
	../Core/HLE/sceUtility.cpp \
	../Core/HLE/sceVaudio.cpp \
	../Core/HW/MediaEngine.cpp \
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
	../Core/SaveState.cpp \
	../Core/System.cpp \
	../Core/Util/BlockAllocator.cpp \
	../Core/Util/PPGeDraw.cpp \
	../Core/Util/ppge_atlas.cpp \ # GPU
	../GPU/GLES/DisplayListInterpreter.cpp \
	../GPU/GLES/FragmentShaderGenerator.cpp \
	../GPU/GLES/Framebuffer.cpp \
	../GPU/GLES/IndexGenerator.cpp \
	../GPU/GLES/ShaderManager.cpp \
	../GPU/GLES/StateMapping.cpp \
	../GPU/GLES/TextureCache.cpp \
	../GPU/GLES/TransformPipeline.cpp \
	../GPU/GLES/VertexDecoder.cpp \
	../GPU/GLES/VertexShaderGenerator.cpp \
	../GPU/GeDisasm.cpp \
	../GPU/GPUCommon.cpp \
	../GPU/GPUState.cpp \
	../GPU/Math3D.cpp \
	../GPU/Null/NullGpu.cpp \ # Kirk
	../ext/libkirk/AES.c \
	../ext/libkirk/SHA1.c \
	../ext/libkirk/bn.c \
	../ext/libkirk/ec.c \
	../ext/libkirk/kirk_engine.c

HEADERS +=	../Core/CPU.h \
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
	../Core/HLE/sceUmd.h \
	../Core/HLE/sceUsb.h \
	../Core/HLE/sceUtility.h \
	../Core/HLE/sceVaudio.h \
	../Core/HW/MediaEngine.h \
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
	../Core/SaveState.h \
	../Core/System.h \
	../Core/Util/BlockAllocator.h \
	../Core/Util/PPGeDraw.h \
	../Core/Util/Pool.h \
	../Core/Util/ppge_atlas.h \
	../GPU/GLES/DisplayListInterpreter.h \
	../GPU/GLES/FragmentShaderGenerator.h \
	../GPU/GLES/Framebuffer.h \
	../GPU/GLES/IndexGenerator.h \
	../GPU/GLES/ShaderManager.h \
	../GPU/GLES/StateMapping.h \
	../GPU/GLES/TextureCache.h \
	../GPU/GLES/TransformPipeline.h \
	../GPU/GLES/VertexDecoder.h \
	../GPU/GLES/VertexShaderGenerator.h \
	../GPU/GPUInterface.h \
	../GPU/GeDisasm.h \
	../GPU/GPUCommon.h \
	../GPU/GPUState.h \
	../GPU/Math3D.h \
	../GPU/Null/NullGpu.h \
	../GPU/ge_constants.h \
	../ext/libkirk/AES.h \
	../ext/libkirk/SHA1.h \
	../ext/libkirk/kirk_engine.h
