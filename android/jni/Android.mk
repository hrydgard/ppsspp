LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

#TARGET_PLATFORM := android-8

NATIVE := ../../ext/native
SRC := ../..

include $(LOCAL_PATH)/Locals.mk

# http://software.intel.com/en-us/articles/getting-started-on-optimizing-ndk-project-for-multiple-cpu-architectures

ifeq ($(TARGET_ARCH_ABI),x86)
ARCH_FILES := \
  $(SRC)/Common/ABI.cpp \
  $(SRC)/Common/x64Emitter.cpp \
  $(SRC)/Common/CPUDetect.cpp \
  $(SRC)/Common/Thunk.cpp \
  $(SRC)/Core/MIPS/x86/CompALU.cpp \
  $(SRC)/Core/MIPS/x86/CompBranch.cpp \
  $(SRC)/Core/MIPS/x86/CompFPU.cpp \
  $(SRC)/Core/MIPS/x86/CompLoadStore.cpp \
  $(SRC)/Core/MIPS/x86/CompVFPU.cpp \
  $(SRC)/Core/MIPS/x86/CompReplace.cpp \
  $(SRC)/Core/MIPS/x86/Asm.cpp \
  $(SRC)/Core/MIPS/x86/Jit.cpp \
  $(SRC)/Core/MIPS/x86/JitSafeMem.cpp \
  $(SRC)/Core/MIPS/x86/RegCache.cpp \
  $(SRC)/Core/MIPS/x86/RegCacheFPU.cpp \
  $(SRC)/GPU/Common/VertexDecoderX86.cpp \
  $(SRC)/GPU/Software/SamplerX86.cpp
endif

ifeq ($(TARGET_ARCH_ABI),x86_64)
ARCH_FILES := \
  $(SRC)/Common/ABI.cpp \
  $(SRC)/Common/x64Emitter.cpp \
  $(SRC)/Common/CPUDetect.cpp \
  $(SRC)/Common/Thunk.cpp \
  $(SRC)/Core/MIPS/x86/CompALU.cpp \
  $(SRC)/Core/MIPS/x86/CompBranch.cpp \
  $(SRC)/Core/MIPS/x86/CompFPU.cpp \
  $(SRC)/Core/MIPS/x86/CompLoadStore.cpp \
  $(SRC)/Core/MIPS/x86/CompVFPU.cpp \
  $(SRC)/Core/MIPS/x86/CompReplace.cpp \
  $(SRC)/Core/MIPS/x86/Asm.cpp \
  $(SRC)/Core/MIPS/x86/Jit.cpp \
  $(SRC)/Core/MIPS/x86/JitSafeMem.cpp \
  $(SRC)/Core/MIPS/x86/RegCache.cpp \
  $(SRC)/Core/MIPS/x86/RegCacheFPU.cpp \
  $(SRC)/GPU/Common/VertexDecoderX86.cpp \
  $(SRC)/GPU/Software/SamplerX86.cpp
endif

ifeq ($(findstring armeabi-v7a,$(TARGET_ARCH_ABI)),armeabi-v7a)
ARCH_FILES := \
  $(SRC)/GPU/Common/TextureDecoderNEON.cpp.neon \
  $(SRC)/Core/Util/AudioFormatNEON.cpp.neon \
  $(SRC)/Common/ArmEmitter.cpp \
  $(SRC)/Common/ArmCPUDetect.cpp \
  $(SRC)/Common/ColorConvNEON.cpp.neon \
  $(SRC)/Core/MIPS/ARM/ArmCompALU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompBranch.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompLoadStore.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPUNEON.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPUNEONUtil.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompReplace.cpp \
  $(SRC)/Core/MIPS/ARM/ArmAsm.cpp \
  $(SRC)/Core/MIPS/ARM/ArmJit.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCache.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCacheFPU.cpp \
  $(SRC)/GPU/Common/VertexDecoderArm.cpp \
  $(SRC)/ext/disarm.cpp \
  ArmEmitterTest.cpp
endif

ifeq ($(findstring arm64-v8a,$(TARGET_ARCH_ABI)),arm64-v8a)
ARCH_FILES := \
  $(SRC)/GPU/Common/TextureDecoderNEON.cpp \
  $(SRC)/Core/Util/AudioFormatNEON.cpp \
  $(SRC)/Common/Arm64Emitter.cpp \
  $(SRC)/Common/ArmCPUDetect.cpp \
  $(SRC)/Common/ColorConvNEON.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64CompALU.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64CompBranch.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64CompFPU.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64CompLoadStore.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64CompVFPU.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64CompReplace.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64Asm.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64Jit.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64RegCache.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64RegCacheFPU.cpp \
  $(SRC)/Core/Util/DisArm64.cpp \
  $(SRC)/GPU/Common/VertexDecoderArm64.cpp \
  Arm64EmitterTest.cpp
endif

ifeq ($(TARGET_ARCH_ABI),armeabi)
ARCH_FILES := \
  $(SRC)/Common/ArmEmitter.cpp \
  $(SRC)/Common/ArmCPUDetect.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompALU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompBranch.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompLoadStore.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPUNEON.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPUNEONUtil.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompReplace.cpp \
  $(SRC)/Core/MIPS/ARM/ArmAsm.cpp \
  $(SRC)/Core/MIPS/ARM/ArmJit.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCache.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCacheFPU.cpp \
  $(SRC)/GPU/Common/VertexDecoderArm.cpp \
  ArmEmitterTest.cpp
endif

EGL_FILES := \
  $(SRC)/Common/GL/GLInterface/EGL.cpp \
  $(SRC)/Common/GL/GLInterface/EGLAndroid.cpp \
  $(SRC)/Common/GL/GLInterface/GLInterface.cpp

VULKAN_FILES := \
  $(SRC)/Common/Vulkan/VulkanLoader.cpp \
  $(SRC)/Common/Vulkan/VulkanContext.cpp \
  $(SRC)/Common/Vulkan/VulkanDebug.cpp \
  $(SRC)/Common/Vulkan/VulkanImage.cpp \
  $(SRC)/Common/Vulkan/VulkanMemory.cpp \
  $(SRC)/GPU/Vulkan/FragmentShaderGeneratorVulkan.cpp \
  $(SRC)/GPU/Vulkan/DrawEngineVulkan.cpp \
  $(SRC)/GPU/Vulkan/FramebufferVulkan.cpp \
  $(SRC)/GPU/Vulkan/GPU_Vulkan.cpp \
  $(SRC)/GPU/Vulkan/PipelineManagerVulkan.cpp \
  $(SRC)/GPU/Vulkan/ShaderManagerVulkan.cpp \
  $(SRC)/GPU/Vulkan/StateMappingVulkan.cpp \
  $(SRC)/GPU/Vulkan/StencilBufferVulkan.cpp \
  $(SRC)/GPU/Vulkan/TextureCacheVulkan.cpp \
  $(SRC)/GPU/Vulkan/TextureScalerVulkan.cpp \
  $(SRC)/GPU/Vulkan/DepalettizeShaderVulkan.cpp \
  $(SRC)/GPU/Vulkan/VertexShaderGeneratorVulkan.cpp \
  $(SRC)/GPU/Vulkan/VulkanUtil.cpp \
  $(SRC)/GPU/Vulkan/DebugVisVulkan.cpp
#endif

SPIRV_CROSS_FILES := \
  $(SRC)/ext/SPIRV-Cross/spirv_cfg.cpp \
  $(SRC)/ext/SPIRV-Cross/spirv_cross.cpp \
  $(SRC)/ext/SPIRV-Cross/spirv_glsl.cpp

EXEC_AND_LIB_FILES := \
  $(ARCH_FILES) \
  $(EGL_FILES) \
  $(VULKAN_FILES) \
  $(SPIRV_CROSS_FILES) \
  TestRunner.cpp \
  $(SRC)/Core/MIPS/MIPS.cpp.arm \
  $(SRC)/Core/MIPS/MIPSAnalyst.cpp \
  $(SRC)/Core/MIPS/MIPSDis.cpp \
  $(SRC)/Core/MIPS/MIPSDisVFPU.cpp \
  $(SRC)/Core/MIPS/MIPSAsm.cpp \
  $(SRC)/Core/MIPS/MIPSInt.cpp.arm \
  $(SRC)/Core/MIPS/MIPSIntVFPU.cpp.arm \
  $(SRC)/Core/MIPS/MIPSStackWalk.cpp \
  $(SRC)/Core/MIPS/MIPSTables.cpp \
  $(SRC)/Core/MIPS/MIPSVFPUUtils.cpp.arm \
  $(SRC)/Core/MIPS/MIPSCodeUtils.cpp.arm \
  $(SRC)/Core/MIPS/MIPSDebugInterface.cpp \
  $(SRC)/Core/MIPS/IR/IRFrontend.cpp \
  $(SRC)/Core/MIPS/IR/IRJit.cpp \
  $(SRC)/Core/MIPS/IR/IRCompALU.cpp \
  $(SRC)/Core/MIPS/IR/IRCompBranch.cpp \
  $(SRC)/Core/MIPS/IR/IRCompFPU.cpp \
  $(SRC)/Core/MIPS/IR/IRCompLoadStore.cpp \
  $(SRC)/Core/MIPS/IR/IRCompVFPU.cpp \
  $(SRC)/Core/MIPS/IR/IRInst.cpp \
  $(SRC)/Core/MIPS/IR/IRInterpreter.cpp \
  $(SRC)/Core/MIPS/IR/IRPassSimplify.cpp \
  $(SRC)/Core/MIPS/IR/IRRegCache.cpp \
  $(SRC)/UI/ui_atlas.cpp \
  $(SRC)/ext/libkirk/AES.c \
  $(SRC)/ext/libkirk/amctrl.c \
  $(SRC)/ext/libkirk/SHA1.c \
  $(SRC)/ext/libkirk/bn.c \
  $(SRC)/ext/libkirk/ec.c \
  $(SRC)/ext/libkirk/kirk_engine.c \
  $(SRC)/ext/sfmt19937/SFMT.c \
  $(SRC)/ext/snappy/snappy-c.cpp \
  $(SRC)/ext/snappy/snappy.cpp \
  $(SRC)/ext/udis86/decode.c \
  $(SRC)/ext/udis86/itab.c \
  $(SRC)/ext/udis86/syn-att.c \
  $(SRC)/ext/udis86/syn-intel.c \
  $(SRC)/ext/udis86/syn.c \
  $(SRC)/ext/udis86/udis86.c \
  $(SRC)/ext/xbrz/xbrz.cpp \
  $(SRC)/ext/xxhash.c \
  $(SRC)/Common/Crypto/md5.cpp \
  $(SRC)/Common/Crypto/sha1.cpp \
  $(SRC)/Common/Crypto/sha256.cpp \
  $(SRC)/Common/ChunkFile.cpp \
  $(SRC)/Common/ColorConv.cpp \
  $(SRC)/Common/KeyMap.cpp \
  $(SRC)/Common/LogManager.cpp \
  $(SRC)/Common/MemArenaAndroid.cpp \
  $(SRC)/Common/MemArenaDarwin.cpp \
  $(SRC)/Common/MemArenaWin32.cpp \
  $(SRC)/Common/MemArenaPosix.cpp \
  $(SRC)/Common/MemoryUtil.cpp \
  $(SRC)/Common/MsgHandler.cpp \
  $(SRC)/Common/FileUtil.cpp \
  $(SRC)/Common/StringUtils.cpp \
  $(SRC)/Common/ThreadPools.cpp \
  $(SRC)/Common/Timer.cpp \
  $(SRC)/Common/Misc.cpp \
  $(SRC)/GPU/Math3D.cpp \
  $(SRC)/GPU/GPU.cpp \
  $(SRC)/GPU/GPUCommon.cpp \
  $(SRC)/GPU/GPUState.cpp \
  $(SRC)/GPU/GeDisasm.cpp \
  $(SRC)/GPU/Common/DepalettizeShaderCommon.cpp \
  $(SRC)/GPU/Common/FramebufferCommon.cpp \
  $(SRC)/GPU/Common/GPUDebugInterface.cpp \
  $(SRC)/GPU/Common/IndexGenerator.cpp.arm \
  $(SRC)/GPU/Common/ShaderId.cpp.arm \
  $(SRC)/GPU/Common/GPUStateUtils.cpp.arm \
  $(SRC)/GPU/Common/SoftwareTransformCommon.cpp.arm \
  $(SRC)/GPU/Common/VertexDecoderCommon.cpp.arm \
  $(SRC)/GPU/Common/TextureCacheCommon.cpp.arm \
  $(SRC)/GPU/Common/TextureScalerCommon.cpp.arm \
  $(SRC)/GPU/Common/ShaderCommon.cpp \
  $(SRC)/GPU/Common/ShaderTranslation.cpp \
  $(SRC)/GPU/Common/StencilCommon.cpp \
  $(SRC)/GPU/Common/SplineCommon.cpp.arm \
  $(SRC)/GPU/Common/DrawEngineCommon.cpp.arm \
  $(SRC)/GPU/Common/TransformCommon.cpp.arm \
  $(SRC)/GPU/Common/TextureDecoder.cpp \
  $(SRC)/GPU/Common/PostShader.cpp \
  $(SRC)/GPU/Common/ShaderUniforms.cpp \
  $(SRC)/GPU/Debugger/Breakpoints.cpp \
  $(SRC)/GPU/Debugger/Record.cpp \
  $(SRC)/GPU/Debugger/Stepping.cpp \
  $(SRC)/GPU/GLES/FramebufferManagerGLES.cpp \
  $(SRC)/GPU/GLES/DepalettizeShaderGLES.cpp \
  $(SRC)/GPU/GLES/GPU_GLES.cpp.arm \
  $(SRC)/GPU/GLES/StencilBufferGLES.cpp.arm \
  $(SRC)/GPU/GLES/TextureCacheGLES.cpp.arm \
  $(SRC)/GPU/GLES/DrawEngineGLES.cpp.arm \
  $(SRC)/GPU/GLES/StateMappingGLES.cpp.arm \
  $(SRC)/GPU/GLES/ShaderManagerGLES.cpp.arm \
  $(SRC)/GPU/GLES/VertexShaderGeneratorGLES.cpp.arm \
  $(SRC)/GPU/GLES/FragmentShaderGeneratorGLES.cpp.arm \
  $(SRC)/GPU/GLES/FragmentTestCacheGLES.cpp.arm \
  $(SRC)/GPU/GLES/TextureScalerGLES.cpp \
  $(SRC)/GPU/Null/NullGpu.cpp \
  $(SRC)/GPU/Software/Clipper.cpp \
  $(SRC)/GPU/Software/Lighting.cpp \
  $(SRC)/GPU/Software/Rasterizer.cpp.arm \
  $(SRC)/GPU/Software/Sampler.cpp \
  $(SRC)/GPU/Software/SoftGpu.cpp \
  $(SRC)/GPU/Software/TransformUnit.cpp \
  $(SRC)/Core/ELF/ElfReader.cpp \
  $(SRC)/Core/ELF/PBPReader.cpp \
  $(SRC)/Core/ELF/PrxDecrypter.cpp \
  $(SRC)/Core/ELF/ParamSFO.cpp \
  $(SRC)/Core/HW/SimpleAudioDec.cpp \
  $(SRC)/Core/HW/AsyncIOManager.cpp \
  $(SRC)/Core/HW/MemoryStick.cpp \
  $(SRC)/Core/HW/MpegDemux.cpp.arm \
  $(SRC)/Core/HW/MediaEngine.cpp.arm \
  $(SRC)/Core/HW/SasAudio.cpp.arm \
  $(SRC)/Core/HW/SasReverb.cpp.arm \
  $(SRC)/Core/HW/StereoResampler.cpp.arm \
  $(SRC)/Core/Core.cpp \
  $(SRC)/Core/Compatibility.cpp \
  $(SRC)/Core/Config.cpp \
  $(SRC)/Core/CoreTiming.cpp \
  $(SRC)/Core/CwCheat.cpp \
  $(SRC)/Core/HDRemaster.cpp \
  $(SRC)/Core/Host.cpp \
  $(SRC)/Core/Loaders.cpp \
  $(SRC)/Core/PSPLoaders.cpp \
  $(SRC)/Core/FileLoaders/CachingFileLoader.cpp \
  $(SRC)/Core/FileLoaders/DiskCachingFileLoader.cpp \
  $(SRC)/Core/FileLoaders/HTTPFileLoader.cpp \
  $(SRC)/Core/FileLoaders/LocalFileLoader.cpp \
  $(SRC)/Core/FileLoaders/RamCachingFileLoader.cpp \
  $(SRC)/Core/FileLoaders/RetryingFileLoader.cpp \
  $(SRC)/Core/MemMap.cpp \
  $(SRC)/Core/MemMapFunctions.cpp \
  $(SRC)/Core/Reporting.cpp \
  $(SRC)/Core/Replay.cpp \
  $(SRC)/Core/SaveState.cpp \
  $(SRC)/Core/Screenshot.cpp \
  $(SRC)/Core/System.cpp \
  $(SRC)/Core/TextureReplacer.cpp \
  $(SRC)/Core/Debugger/Breakpoints.cpp \
  $(SRC)/Core/Debugger/SymbolMap.cpp \
  $(SRC)/Core/Dialog/PSPDialog.cpp \
  $(SRC)/Core/Dialog/PSPGamedataInstallDialog.cpp \
  $(SRC)/Core/Dialog/PSPMsgDialog.cpp \
  $(SRC)/Core/Dialog/PSPNetconfDialog.cpp \
  $(SRC)/Core/Dialog/PSPOskDialog.cpp \
  $(SRC)/Core/Dialog/PSPScreenshotDialog.cpp \
  $(SRC)/Core/Dialog/PSPPlaceholderDialog.cpp \
  $(SRC)/Core/Dialog/PSPSaveDialog.cpp \
  $(SRC)/Core/Dialog/SavedataParam.cpp \
  $(SRC)/Core/Font/PGF.cpp \
  $(SRC)/Core/HLE/HLEHelperThread.cpp \
  $(SRC)/Core/HLE/HLETables.cpp \
  $(SRC)/Core/HLE/ReplaceTables.cpp \
  $(SRC)/Core/HLE/HLE.cpp \
  $(SRC)/Core/HLE/KUBridge.cpp \
  $(SRC)/Core/HLE/sceAdler.cpp \
  $(SRC)/Core/HLE/sceAtrac.cpp \
  $(SRC)/Core/HLE/__sceAudio.cpp.arm \
  $(SRC)/Core/HLE/sceAudio.cpp.arm \
  $(SRC)/Core/HLE/sceAudiocodec.cpp.arm \
  $(SRC)/Core/HLE/sceAudioRouting.cpp \
  $(SRC)/Core/HLE/sceChnnlsv.cpp \
  $(SRC)/Core/HLE/sceCcc.cpp \
  $(SRC)/Core/HLE/sceCtrl.cpp.arm \
  $(SRC)/Core/HLE/sceDeflt.cpp \
  $(SRC)/Core/HLE/sceDisplay.cpp \
  $(SRC)/Core/HLE/sceDmac.cpp \
  $(SRC)/Core/HLE/sceG729.cpp \
  $(SRC)/Core/HLE/sceGe.cpp \
  $(SRC)/Core/HLE/sceFont.cpp \
  $(SRC)/Core/HLE/sceHeap.cpp \
  $(SRC)/Core/HLE/sceHprm.cpp \
  $(SRC)/Core/HLE/sceHttp.cpp \
  $(SRC)/Core/HLE/sceImpose.cpp \
  $(SRC)/Core/HLE/sceIo.cpp \
  $(SRC)/Core/HLE/sceJpeg.cpp \
  $(SRC)/Core/HLE/sceKernel.cpp \
  $(SRC)/Core/HLE/sceKernelAlarm.cpp \
  $(SRC)/Core/HLE/sceKernelEventFlag.cpp \
  $(SRC)/Core/HLE/sceKernelInterrupt.cpp \
  $(SRC)/Core/HLE/sceKernelMemory.cpp \
  $(SRC)/Core/HLE/sceKernelModule.cpp \
  $(SRC)/Core/HLE/sceKernelMutex.cpp \
  $(SRC)/Core/HLE/sceKernelMbx.cpp \
  $(SRC)/Core/HLE/sceKernelMsgPipe.cpp \
  $(SRC)/Core/HLE/sceKernelSemaphore.cpp \
  $(SRC)/Core/HLE/sceKernelThread.cpp.arm \
  $(SRC)/Core/HLE/sceKernelTime.cpp \
  $(SRC)/Core/HLE/sceKernelVTimer.cpp \
  $(SRC)/Core/HLE/sceMpeg.cpp \
  $(SRC)/Core/HLE/sceMd5.cpp \
  $(SRC)/Core/HLE/sceMp4.cpp \
  $(SRC)/Core/HLE/sceMp3.cpp \
  $(SRC)/Core/HLE/sceNet.cpp \
  $(SRC)/Core/HLE/proAdhoc.cpp \
  $(SRC)/Core/HLE/proAdhocServer.cpp \
  $(SRC)/Core/HLE/sceNetAdhoc.cpp \
  $(SRC)/Core/HLE/sceOpenPSID.cpp \
  $(SRC)/Core/HLE/sceP3da.cpp \
  $(SRC)/Core/HLE/sceMt19937.cpp \
  $(SRC)/Core/HLE/sceParseHttp.cpp \
  $(SRC)/Core/HLE/sceParseUri.cpp \
  $(SRC)/Core/HLE/scePower.cpp \
  $(SRC)/Core/HLE/sceRtc.cpp \
  $(SRC)/Core/HLE/scePsmf.cpp \
  $(SRC)/Core/HLE/sceSas.cpp \
  $(SRC)/Core/HLE/sceSfmt19937.cpp \
  $(SRC)/Core/HLE/sceSha256.cpp \
  $(SRC)/Core/HLE/sceSsl.cpp \
  $(SRC)/Core/HLE/sceUmd.cpp \
  $(SRC)/Core/HLE/sceUsb.cpp \
  $(SRC)/Core/HLE/sceUsbCam.cpp \
  $(SRC)/Core/HLE/sceUsbGps.cpp \
  $(SRC)/Core/HLE/sceUtility.cpp \
  $(SRC)/Core/HLE/sceVaudio.cpp \
  $(SRC)/Core/HLE/scePspNpDrm_user.cpp \
  $(SRC)/Core/HLE/sceGameUpdate.cpp \
  $(SRC)/Core/HLE/sceNp.cpp \
  $(SRC)/Core/HLE/scePauth.cpp \
  $(SRC)/Core/FileSystems/BlobFileSystem.cpp \
  $(SRC)/Core/FileSystems/BlockDevices.cpp \
  $(SRC)/Core/FileSystems/ISOFileSystem.cpp \
  $(SRC)/Core/FileSystems/FileSystem.cpp \
  $(SRC)/Core/FileSystems/MetaFileSystem.cpp \
  $(SRC)/Core/FileSystems/DirectoryFileSystem.cpp \
  $(SRC)/Core/FileSystems/VirtualDiscFileSystem.cpp \
  $(SRC)/Core/FileSystems/tlzrc.cpp \
  $(SRC)/Core/MIPS/JitCommon/JitCommon.cpp \
  $(SRC)/Core/MIPS/JitCommon/JitBlockCache.cpp \
  $(SRC)/Core/MIPS/JitCommon/JitState.cpp \
  $(SRC)/Core/Util/AudioFormat.cpp \
  $(SRC)/Core/Util/GameManager.cpp \
  $(SRC)/Core/Util/BlockAllocator.cpp \
  $(SRC)/Core/Util/ppge_atlas.cpp \
  $(SRC)/Core/Util/PPGeDraw.cpp \
  $(SRC)/git-version.cpp

LOCAL_MODULE := ppsspp_core
LOCAL_SRC_FILES := $(EXEC_AND_LIB_FILES)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Locals.mk
LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SRC)/ext/armips $(LOCAL_C_INCLUDES)

LIBARMIPS_FILES := \
  $(SRC)/ext/armips/Archs/ARM/Arm.cpp \
  $(SRC)/ext/armips/Archs/ARM/ArmOpcodes.cpp \
  $(SRC)/ext/armips/Archs/ARM/ArmParser.cpp \
  $(SRC)/ext/armips/Archs/ARM/ArmRelocator.cpp \
  $(SRC)/ext/armips/Archs/ARM/CArmInstruction.cpp \
  $(SRC)/ext/armips/Archs/ARM/CThumbInstruction.cpp \
  $(SRC)/ext/armips/Archs/ARM/Pool.cpp \
  $(SRC)/ext/armips/Archs/ARM/ThumbOpcodes.cpp \
  $(SRC)/ext/armips/Archs/MIPS/CMipsInstruction.cpp \
  $(SRC)/ext/armips/Archs/MIPS/Mips.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsElfFile.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsMacros.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsOpcodes.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsParser.cpp \
  $(SRC)/ext/armips/Archs/MIPS/PsxRelocator.cpp \
  $(SRC)/ext/armips/Archs/Architecture.cpp \
  $(SRC)/ext/armips/Commands/CAssemblerCommand.cpp \
  $(SRC)/ext/armips/Commands/CAssemblerLabel.cpp \
  $(SRC)/ext/armips/Commands/CDirectiveArea.cpp \
  $(SRC)/ext/armips/Commands/CDirectiveConditional.cpp \
  $(SRC)/ext/armips/Commands/CDirectiveData.cpp \
  $(SRC)/ext/armips/Commands/CDirectiveFile.cpp \
  $(SRC)/ext/armips/Commands/CDirectiveMessage.cpp \
  $(SRC)/ext/armips/Commands/CommandSequence.cpp \
  $(SRC)/ext/armips/Core/ELF/ElfFile.cpp \
  $(SRC)/ext/armips/Core/ELF/ElfRelocator.cpp \
  $(SRC)/ext/armips/Core/Assembler.cpp \
  $(SRC)/ext/armips/Core/Common.cpp \
  $(SRC)/ext/armips/Core/Expression.cpp \
  $(SRC)/ext/armips/Core/ExpressionFunctions.cpp \
  $(SRC)/ext/armips/Core/FileManager.cpp \
  $(SRC)/ext/armips/Core/Misc.cpp \
  $(SRC)/ext/armips/Core/SymbolData.cpp \
  $(SRC)/ext/armips/Core/SymbolTable.cpp \
  $(SRC)/ext/armips/Parser/DirectivesParser.cpp \
  $(SRC)/ext/armips/Parser/ExpressionParser.cpp \
  $(SRC)/ext/armips/Parser/Parser.cpp \
  $(SRC)/ext/armips/Parser/Tokenizer.cpp \
  $(SRC)/ext/armips/Util/ByteArray.cpp \
  $(SRC)/ext/armips/Util/CRC.cpp \
  $(SRC)/ext/armips/Util/EncodingTable.cpp \
  $(SRC)/ext/armips/Util/FileClasses.cpp \
  $(SRC)/ext/armips/Util/Util.cpp

LOCAL_MODULE := libarmips
LOCAL_SRC_FILES := $(LIBARMIPS_FILES)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Locals.mk
LOCAL_STATIC_LIBRARIES += ppsspp_core libarmips

# These are the files just for ppsspp_jni
LOCAL_MODULE := ppsspp_jni
LOCAL_SRC_FILES := \
  $(SRC)/android/jni/app-android.cpp \
  $(SRC)/android/jni/AndroidEGLContext.cpp \
  $(SRC)/android/jni/AndroidJavaGLContext.cpp \
  $(SRC)/android/jni/AndroidVulkanContext.cpp \
  $(SRC)/android/jni/native_audio.cpp \
  $(SRC)/android/jni/native-audio-so.cpp \
  $(SRC)/UI/BackgroundAudio.cpp \
  $(SRC)/UI/DevScreens.cpp \
  $(SRC)/UI/DisplayLayoutEditor.cpp \
  $(SRC)/UI/DisplayLayoutScreen.cpp \
  $(SRC)/UI/EmuScreen.cpp \
  $(SRC)/UI/MainScreen.cpp \
  $(SRC)/UI/MiscScreens.cpp \
  $(SRC)/UI/RemoteISOScreen.cpp \
  $(SRC)/UI/ReportScreen.cpp \
  $(SRC)/UI/PauseScreen.cpp \
  $(SRC)/UI/SavedataScreen.cpp \
  $(SRC)/UI/Store.cpp \
  $(SRC)/UI/GamepadEmu.cpp \
  $(SRC)/UI/GameInfoCache.cpp \
  $(SRC)/UI/GameScreen.cpp \
  $(SRC)/UI/ControlMappingScreen.cpp \
  $(SRC)/UI/GameSettingsScreen.cpp \
  $(SRC)/UI/TiltAnalogSettingsScreen.cpp \
  $(SRC)/UI/TiltEventProcessor.cpp \
  $(SRC)/UI/TouchControlLayoutScreen.cpp \
  $(SRC)/UI/TouchControlVisibilityScreen.cpp \
  $(SRC)/UI/CwCheatScreen.cpp \
  $(SRC)/UI/InstallZipScreen.cpp \
  $(SRC)/UI/OnScreenDisplay.cpp \
  $(SRC)/UI/ProfilerDraw.cpp \
  $(SRC)/UI/NativeApp.cpp \
  $(SRC)/UI/TextureUtil.cpp \
  $(SRC)/UI/ComboKeyMappingScreen.cpp

ifneq ($(SKIPAPP),1)
  include $(BUILD_SHARED_LIBRARY)
endif


ifeq ($(HEADLESS),1)
  include $(CLEAR_VARS)
  include $(LOCAL_PATH)/Locals.mk
  LOCAL_STATIC_LIBRARIES += ppsspp_core libarmips

  # Android 5.0 requires PIE for executables.  Only supported on 4.1+, but this is testing anyway.
  LOCAL_CFLAGS += -fPIE
  LOCAL_LDFLAGS += -fPIE -pie

  LOCAL_MODULE := ppsspp_headless
  LOCAL_SRC_FILES := \
    $(SRC)/headless/Headless.cpp \
    $(SRC)/headless/StubHost.cpp \
    $(SRC)/headless/Compare.cpp

  include $(BUILD_EXECUTABLE)
endif

ifeq ($(UNITTEST),1)
  include $(CLEAR_VARS)
  include $(LOCAL_PATH)/Locals.mk
  LOCAL_STATIC_LIBRARIES += ppsspp_core libarmips

  # Android 5.0 requires PIE for executables.  Only supported on 4.1+, but this is testing anyway.
  LOCAL_CFLAGS += -fPIE
  LOCAL_LDFLAGS += -fPIE -pie

  ifeq ($(findstring arm64-v8a,$(TARGET_ARCH_ABI)),arm64-v8a)
    TESTARMEMITTER_FILE = $(SRC)/unittest/TestArm64Emitter.cpp
  else ifeq ($(findstring armeabi-v7a,$(TARGET_ARCH_ABI)),armeabi-v7a)
    TESTARMEMITTER_FILE = $(SRC)/unittest/TestArmEmitter.cpp
  else
    TESTARMEMITTER_FILE = \
      $(SRC)/Common/ArmEmitter.cpp \
      $(SRC)/Common/Arm64Emitter.cpp \
      $(SRC)/Core/MIPS/ARM/ArmRegCacheFPU.cpp \
      $(SRC)/Core/Util/DisArm64.cpp \
      $(SRC)/ext/disarm.cpp \
      $(SRC)/unittest/TestArmEmitter.cpp \
      $(SRC)/unittest/TestArm64Emitter.cpp \
      $(SRC)/unittest/TestX64Emitter.cpp
  endif

  LOCAL_MODULE := ppsspp_unittest
  LOCAL_SRC_FILES := \
    $(SRC)/unittest/JitHarness.cpp \
    $(SRC)/unittest/TestVertexJit.cpp \
    $(TESTARMEMITTER_FILE) \
    $(SRC)/unittest/UnitTest.cpp

  include $(BUILD_EXECUTABLE)
endif

$(call import-module,libzip)
$(call import-module,native)
$(call import-module,glslang-build)

ifeq ($(ANDROID_NDK_PROFILER),1)
  $(call import-module,android-ndk-profiler)
endif

jni/$(SRC)/git-version.cpp:
	-./git-version-gen.sh
	-..\Windows\git-version-gen.cmd

.PHONY: jni/$(SRC)/git-version.cpp
