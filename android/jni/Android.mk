LOCAL_PATH := $(call my-dir)
SRC := ../..

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Locals.mk

LOCAL_CFLAGS += -D_7ZIP_ST -D__SWITCH__

LZMA_FILES := \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Alloc.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Bcj2.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Bcj2Enc.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Bra.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Bra86.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/CpuArch.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Delta.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/LzFind.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/LzFindOpt.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/LzmaDec.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/LzmaEnc.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Lzma86Dec.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Lzma86Enc.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/LzmaLib.c \
	$(SRC)/ext/libchdr/deps/lzma-22.01/src/Sort.c

CHDR_FILES := \
	${LZMA_FILES} \
	$(SRC)/ext/libchdr/src/libchdr_bitstream.c \
	$(SRC)/ext/libchdr/src/libchdr_cdrom.c \
	$(SRC)/ext/libchdr/src/libchdr_chd.c \
	$(SRC)/ext/libchdr/src/libchdr_flac.c \
	$(SRC)/ext/libchdr/src/libchdr_huffman.c

LOCAL_MODULE := libchdr
LOCAL_SRC_FILES := $(CHDR_FILES)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Locals.mk

LOCAL_CFLAGS += -DSTACK_LINE_READER_BUFFER_SIZE=1024 -DHAVE_DLFCN_H -DRC_DISABLE_LUA -D_7ZIP_ST

# http://software.intel.com/en-us/articles/getting-started-on-optimizing-ndk-project-for-multiple-cpu-architectures

ifeq ($(TARGET_ARCH_ABI),x86)
ARCH_FILES := \
  $(SRC)/Common/ABI.cpp \
  $(SRC)/Common/x64Emitter.cpp \
  $(SRC)/Common/x64Analyzer.cpp \
  $(SRC)/Common/Thunk.cpp
else ifeq ($(TARGET_ARCH_ABI),x86_64)
ARCH_FILES := \
  $(SRC)/Common/ABI.cpp \
  $(SRC)/Common/x64Emitter.cpp \
  $(SRC)/Common/x64Analyzer.cpp \
  $(SRC)/Common/Thunk.cpp
else ifeq ($(findstring armeabi-v7a,$(TARGET_ARCH_ABI)),armeabi-v7a)
ARCH_FILES := \
  $(SRC)/Common/ArmEmitter.cpp \
  $(SRC)/ext/disarm.cpp \
  $(SRC)/ext/libpng17/arm/arm_init.c \
  $(SRC)/ext/libpng17/arm/filter_neon_intrinsics.c \
  $(SRC)/ext/libpng17/arm/filter_neon.S.neon
else ifeq ($(findstring arm64-v8a,$(TARGET_ARCH_ABI)),arm64-v8a)
ARCH_FILES := \
  $(SRC)/Common/Arm64Emitter.cpp \
  $(SRC)/ext/libpng17/arm/arm_init.c \
  $(SRC)/ext/libpng17/arm/filter_neon_intrinsics.c
endif

NATIVE_FILES :=\
  $(SRC)/Common/GPU/OpenGL/gl3stub.c \
  $(SRC)/Common/GPU/OpenGL/thin3d_gl.cpp \
  $(SRC)/Common/GPU/OpenGL/GLDebugLog.cpp \
  $(SRC)/Common/GPU/OpenGL/GLSLProgram.cpp \
  $(SRC)/Common/GPU/OpenGL/GLFeatures.cpp \
  $(SRC)/Common/GPU/OpenGL/GLFrameData.cpp \
  $(SRC)/Common/GPU/OpenGL/GLMemory.cpp \
  $(SRC)/Common/GPU/OpenGL/GLRenderManager.cpp \
  $(SRC)/Common/GPU/OpenGL/GLQueueRunner.cpp \
  $(SRC)/Common/GPU/OpenGL/DataFormatGL.cpp

VULKAN_FILES := \
  $(SRC)/Common/GPU/Vulkan/thin3d_vulkan.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanQueueRunner.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanRenderManager.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanFrameData.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanLoader.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanContext.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanDebug.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanImage.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanFramebuffer.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanMemory.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanDescSet.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanProfiler.cpp \
  $(SRC)/Common/GPU/Vulkan/VulkanBarrier.cpp

VMA_FILES := \
  $(SRC)/ext/vma/vk_mem_alloc.cpp

SPIRV_CROSS_FILES := \
  $(SRC)/ext/SPIRV-Cross/spirv_cfg.cpp \
  $(SRC)/ext/SPIRV-Cross/spirv_cross.cpp \
  $(SRC)/ext/SPIRV-Cross/spirv_cross_util.cpp \
  $(SRC)/ext/SPIRV-Cross/spirv_glsl.cpp \
  $(SRC)/ext/SPIRV-Cross/spirv_parser.cpp \
  $(SRC)/ext/SPIRV-Cross/spirv_cross_parsed_ir.cpp

NAETT_FILES := \
  ${SRC}/ext/naett/naett.c

MINIMP3_FILES := \
    ${SRC}/ext/minimp3/minimp3.cpp

AT3_STANDALONE_FILES := \
	${SRC}/ext/at3_standalone/atrac.cpp \
	${SRC}/ext/at3_standalone/atrac3.cpp \
	${SRC}/ext/at3_standalone/atrac3plus.cpp \
	${SRC}/ext/at3_standalone/atrac3plusdec.cpp \
	${SRC}/ext/at3_standalone/atrac3plusdsp.cpp \
	${SRC}/ext/at3_standalone/get_bits.cpp \
	${SRC}/ext/at3_standalone/compat.cpp \
	${SRC}/ext/at3_standalone/fft.cpp \
	${SRC}/ext/at3_standalone/mem.cpp

RCHEEVOS_FILES := \
  ${SRC}/ext/rcheevos/src/rapi/rc_api_common.c \
  ${SRC}/ext/rcheevos/src/rapi/rc_api_editor.c \
  ${SRC}/ext/rcheevos/src/rapi/rc_api_info.c \
  ${SRC}/ext/rcheevos/src/rapi/rc_api_runtime.c \
  ${SRC}/ext/rcheevos/src/rapi/rc_api_user.c \
  ${SRC}/ext/rcheevos/src/rcheevos/alloc.c \
  ${SRC}/ext/rcheevos/src/rcheevos/condition.c \
  ${SRC}/ext/rcheevos/src/rcheevos/condset.c \
  ${SRC}/ext/rcheevos/src/rcheevos/consoleinfo.c \
  ${SRC}/ext/rcheevos/src/rcheevos/format.c \
  ${SRC}/ext/rcheevos/src/rcheevos/lboard.c \
  ${SRC}/ext/rcheevos/src/rcheevos/memref.c \
  ${SRC}/ext/rcheevos/src/rcheevos/operand.c \
  ${SRC}/ext/rcheevos/src/rc_client.c \
  ${SRC}/ext/rcheevos/src/rc_util.c \
  ${SRC}/ext/rcheevos/src/rc_compat.c \
  ${SRC}/ext/rcheevos/src/rcheevos/rc_validate.c \
  ${SRC}/ext/rcheevos/src/rcheevos/richpresence.c \
  ${SRC}/ext/rcheevos/src/rcheevos/runtime.c \
  ${SRC}/ext/rcheevos/src/rcheevos/runtime_progress.c \
  ${SRC}/ext/rcheevos/src/rcheevos/trigger.c \
  ${SRC}/ext/rcheevos/src/rcheevos/value.c \
  ${SRC}/ext/rcheevos/src/rhash/md5.c

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
	ADRENOTOOLS_FILES := \
	  ${SRC}/ext/libadrenotools/src/driver.cpp \
	  ${SRC}/ext/libadrenotools/src/hook/hook_impl.cpp \
	  ${SRC}/ext/libadrenotools/src/hook/file_redirect_hook.c \
	  ${SRC}/ext/libadrenotools/src/hook/gsl_alloc_hook.c \
	  ${SRC}/ext/libadrenotools/src/hook/main_hook.c \
	  ${SRC}/ext/libadrenotools/lib/linkernsbypass/android_linker_ns.cpp \
	  ${SRC}/ext/libadrenotools/lib/linkernsbypass/elf_soname_patcher.cpp
endif

VR_FILES := \
  $(SRC)/Common/VR/OpenXRLoader.cpp \
  $(SRC)/Common/VR/PPSSPPVR.cpp \
  $(SRC)/Common/VR/VRBase.cpp \
  $(SRC)/Common/VR/VRFramebuffer.cpp \
  $(SRC)/Common/VR/VRInput.cpp \
  $(SRC)/Common/VR/VRMath.cpp \
  $(SRC)/Common/VR/VRRenderer.cpp

EXT_FILES := \
  $(SRC)/ext/cityhash/city.cpp \
  $(SRC)/ext/libpng17/png.c \
  $(SRC)/ext/libpng17/pngerror.c \
  $(SRC)/ext/libpng17/pngget.c \
  $(SRC)/ext/libpng17/pngmem.c \
  $(SRC)/ext/libpng17/pngpread.c \
  $(SRC)/ext/libpng17/pngread.c \
  $(SRC)/ext/libpng17/pngrio.c \
  $(SRC)/ext/libpng17/pngrtran.c \
  $(SRC)/ext/libpng17/pngrutil.c \
  $(SRC)/ext/libpng17/pngset.c \
  $(SRC)/ext/libpng17/pngtrans.c \
  $(SRC)/ext/libpng17/pngwio.c \
  $(SRC)/ext/libpng17/pngwrite.c \
  $(SRC)/ext/libpng17/pngwtran.c \
  $(SRC)/ext/libpng17/pngwutil.c \
  $(SRC)/ext/basis_universal/basisu_transcoder.cpp \
  $(SRC)/ext/jpge/jpgd.cpp \
  $(SRC)/ext/jpge/jpge.cpp \
  $(SRC)/ext/sha1/sha1.cpp \
  $(SRC)/ext/gason/gason.cpp \
  $(SRC)/ext/libkirk/AES.c \
  $(SRC)/ext/libkirk/amctrl.c \
  $(SRC)/ext/libkirk/SHA1.c \
  $(SRC)/ext/libkirk/bn.c \
  $(SRC)/ext/libkirk/ec.c \
  $(SRC)/ext/libkirk/kirk_engine.c \
  $(SRC)/ext/sfmt19937/SFMT.c \
  $(SRC)/ext/snappy/snappy-c.cpp \
  $(SRC)/ext/snappy/snappy-sinksource.cpp \
  $(SRC)/ext/snappy/snappy-stubs-internal.cpp \
  $(SRC)/ext/snappy/snappy.cpp \
  $(SRC)/ext/udis86/decode.c \
  $(SRC)/ext/udis86/itab.c \
  $(SRC)/ext/udis86/syn-att.c \
  $(SRC)/ext/udis86/syn-intel.c \
  $(SRC)/ext/udis86/syn.c \
  $(SRC)/ext/udis86/udis86.c \
  $(SRC)/ext/xbrz/xbrz.cpp \
  $(SRC)/ext/cpu_features/src/filesystem.c \
  $(SRC)/ext/cpu_features/src/hwcaps.c \
  $(SRC)/ext/cpu_features/src/impl_aarch64_linux_or_android.c \
  $(SRC)/ext/cpu_features/src/impl_arm_linux_or_android.c \
  $(SRC)/ext/cpu_features/src/impl_mips_linux_or_android.c \
  $(SRC)/ext/cpu_features/src/impl_ppc_linux.c \
  $(SRC)/ext/cpu_features/src/impl_riscv_linux.c \
  $(SRC)/ext/cpu_features/src/impl_s390x_linux.c \
  $(SRC)/ext/cpu_features/src/impl_x86_freebsd.c \
  $(SRC)/ext/cpu_features/src/impl_x86_linux_or_android.c \
  $(SRC)/ext/cpu_features/src/impl_x86_macos.c \
  $(SRC)/ext/cpu_features/src/impl_x86_windows.c \
  $(SRC)/ext/cpu_features/src/stack_line_reader.c \
  $(SRC)/ext/cpu_features/src/string_view.c

EXEC_AND_LIB_FILES := \
  $(ARCH_FILES) \
  $(VULKAN_FILES) \
  $(VR_FILES) \
  $(VMA_FILES) \
  $(SPIRV_CROSS_FILES) \
  $(RCHEEVOS_FILES) \
  $(NAETT_FILES) \
  $(MINIMP3_FILES) \
  $(AT3_STANDALONE_FILES) \
  $(EXT_FILES) \
  $(NATIVE_FILES) \
  $(SRC)/Common/Buffer.cpp \
  $(SRC)/Common/Crypto/md5.cpp \
  $(SRC)/Common/Crypto/sha1.cpp \
  $(SRC)/Common/Crypto/sha256.cpp \
  $(SRC)/Common/Data/Color/RGBAUtil.cpp \
  $(SRC)/Common/Data/Convert/ColorConv.cpp \
  $(SRC)/Common/Data/Convert/SmallDataConvert.cpp \
  $(SRC)/Common/Data/Encoding/Base64.cpp \
  $(SRC)/Common/Data/Encoding/Compression.cpp \
  $(SRC)/Common/Data/Encoding/Utf8.cpp \
  $(SRC)/Common/Data/Format/RIFF.cpp \
  $(SRC)/Common/Data/Format/IniFile.cpp \
  $(SRC)/Common/Data/Format/JSONReader.cpp \
  $(SRC)/Common/Data/Format/JSONWriter.cpp \
  $(SRC)/Common/Data/Format/DDSLoad.cpp \
  $(SRC)/Common/Data/Format/DDSLoad.h \
  $(SRC)/Common/Data/Format/PNGLoad.cpp \
  $(SRC)/Common/Data/Format/PNGLoad.h \
  $(SRC)/Common/Data/Format/ZIMLoad.cpp \
  $(SRC)/Common/Data/Format/ZIMLoad.h \
  $(SRC)/Common/Data/Format/ZIMSave.cpp \
  $(SRC)/Common/Data/Format/ZIMSave.h \
  $(SRC)/Common/Data/Hash/Hash.cpp \
  $(SRC)/Common/Data/Text/I18n.cpp \
  $(SRC)/Common/Data/Text/Parsers.cpp \
  $(SRC)/Common/Data/Text/WrapText.cpp \
  $(SRC)/Common/File/AndroidStorage.cpp \
  $(SRC)/Common/File/AndroidContentURI.cpp \
  $(SRC)/Common/File/VFS/VFS.cpp \
  $(SRC)/Common/File/VFS/ZipFileReader.cpp \
  $(SRC)/Common/File/VFS/DirectoryReader.cpp \
  $(SRC)/Common/File/DiskFree.cpp \
  $(SRC)/Common/File/Path.cpp \
  $(SRC)/Common/File/PathBrowser.cpp \
  $(SRC)/Common/File/FileUtil.cpp \
  $(SRC)/Common/File/DirListing.cpp \
  $(SRC)/Common/File/FileDescriptor.cpp \
  $(SRC)/Common/GPU/thin3d.cpp \
  $(SRC)/Common/GPU/GPUBackendCommon.cpp \
  $(SRC)/Common/GPU/Shader.cpp \
  $(SRC)/Common/GPU/ShaderWriter.cpp \
  $(SRC)/Common/GPU/ShaderTranslation.cpp \
  $(SRC)/Common/Render/ManagedTexture.cpp \
  $(SRC)/Common/Render/DrawBuffer.cpp \
  $(SRC)/Common/Render/TextureAtlas.cpp \
  $(SRC)/Common/Render/Text/draw_text.cpp \
  $(SRC)/Common/Render/Text/draw_text_android.cpp \
  $(SRC)/Common/Input/GestureDetector.cpp \
  $(SRC)/Common/Input/InputState.cpp \
  $(SRC)/Common/Math/fast/fast_matrix.c \
  $(SRC)/Common/Math/math_util.cpp \
  $(SRC)/Common/Math/Statistics.cpp \
  $(SRC)/Common/Math/curves.cpp \
  $(SRC)/Common/Math/expression_parser.cpp \
  $(SRC)/Common/Math/lin/vec3.cpp.arm \
  $(SRC)/Common/Math/lin/matrix4x4.cpp.arm \
  $(SRC)/Common/Net/HTTPClient.cpp \
  $(SRC)/Common/Net/HTTPHeaders.cpp \
  $(SRC)/Common/Net/HTTPRequest.cpp \
  $(SRC)/Common/Net/HTTPNaettRequest.cpp \
  $(SRC)/Common/Net/HTTPServer.cpp \
  $(SRC)/Common/Net/NetBuffer.cpp \
  $(SRC)/Common/Net/Resolve.cpp \
  $(SRC)/Common/Net/Sinks.cpp \
  $(SRC)/Common/Net/URL.cpp \
  $(SRC)/Common/Net/WebsocketServer.cpp \
  $(SRC)/Common/Profiler/Profiler.cpp \
  $(SRC)/Common/System/Display.cpp \
  $(SRC)/Common/System/Request.cpp \
  $(SRC)/Common/System/OSD.cpp \
  $(SRC)/Common/Thread/ThreadUtil.cpp \
  $(SRC)/Common/Thread/ThreadManager.cpp \
  $(SRC)/Common/Thread/ParallelLoop.cpp \
  $(SRC)/Common/UI/AsyncImageFileView.cpp \
  $(SRC)/Common/UI/Root.cpp \
  $(SRC)/Common/UI/Screen.cpp \
  $(SRC)/Common/UI/UI.cpp \
  $(SRC)/Common/UI/Context.cpp \
  $(SRC)/Common/UI/UIScreen.cpp \
  $(SRC)/Common/UI/Tween.cpp \
  $(SRC)/Common/UI/IconCache.cpp \
  $(SRC)/Common/UI/View.cpp \
  $(SRC)/Common/UI/ViewGroup.cpp \
  $(SRC)/Common/UI/ScrollView.cpp \
  $(SRC)/Common/UI/PopupScreens.cpp \
  $(SRC)/Common/Serialize/Serializer.cpp \
  $(SRC)/Common/ArmCPUDetect.cpp \
  $(SRC)/Common/CPUDetect.cpp \
  $(SRC)/Common/ExceptionHandlerSetup.cpp \
  $(SRC)/Common/FakeCPUDetect.cpp \
  $(SRC)/Common/Log.cpp \
  $(SRC)/Common/Log/LogManager.cpp \
  $(SRC)/Common/LogReporting.cpp \
  $(SRC)/Common/MemArenaAndroid.cpp \
  $(SRC)/Common/MemArenaDarwin.cpp \
  $(SRC)/Common/MemArenaWin32.cpp \
  $(SRC)/Common/MemArenaPosix.cpp \
  $(SRC)/Common/MemoryUtil.cpp \
  $(SRC)/Common/MipsCPUDetect.cpp \
  $(SRC)/Common/StringUtils.cpp \
  $(SRC)/Common/SysError.cpp \
  $(SRC)/Common/TimeUtil.cpp

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    EXEC_AND_LIB_FILES += $(ADRENOTOOLS_FILES)
endif

LOCAL_MODULE := ppsspp_common
LOCAL_SRC_FILES := $(EXEC_AND_LIB_FILES)
include $(BUILD_STATIC_LIBRARY)

# Next up, Core, GPU, and other core parts shared by headless.
include $(CLEAR_VARS)
include $(LOCAL_PATH)/Locals.mk
LOCAL_WHOLE_STATIC_LIBRARIES += ppsspp_common libchdr

ifeq ($(TARGET_ARCH_ABI),x86_64)
ARCH_FILES := \
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
  $(SRC)/Core/MIPS/x86/X64IRAsm.cpp \
  $(SRC)/Core/MIPS/x86/X64IRCompALU.cpp \
  $(SRC)/Core/MIPS/x86/X64IRCompBranch.cpp \
  $(SRC)/Core/MIPS/x86/X64IRCompFPU.cpp \
  $(SRC)/Core/MIPS/x86/X64IRCompLoadStore.cpp \
  $(SRC)/Core/MIPS/x86/X64IRCompSystem.cpp \
  $(SRC)/Core/MIPS/x86/X64IRCompVec.cpp \
  $(SRC)/Core/MIPS/x86/X64IRJit.cpp \
  $(SRC)/Core/MIPS/x86/X64IRRegCache.cpp \
  $(SRC)/GPU/Common/VertexDecoderX86.cpp \
  $(SRC)/GPU/Software/DrawPixelX86.cpp \
  $(SRC)/GPU/Software/SamplerX86.cpp
else ifeq ($(findstring armeabi-v7a,$(TARGET_ARCH_ABI)),armeabi-v7a)
ARCH_FILES := \
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
else ifeq ($(findstring arm64-v8a,$(TARGET_ARCH_ABI)),arm64-v8a)
ARCH_FILES := \
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
  $(SRC)/Core/MIPS/ARM64/Arm64IRAsm.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRCompALU.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRCompBranch.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRCompFPU.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRCompLoadStore.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRCompSystem.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRCompVec.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRJit.cpp \
  $(SRC)/Core/MIPS/ARM64/Arm64IRRegCache.cpp \
  $(SRC)/Core/Util/DisArm64.cpp \
  $(SRC)/GPU/Common/VertexDecoderArm64.cpp \
  Arm64EmitterTest.cpp
endif

GPU_VULKAN_FILES := \
  $(SRC)/GPU/Vulkan/DrawEngineVulkan.cpp \
  $(SRC)/GPU/Vulkan/FramebufferManagerVulkan.cpp \
  $(SRC)/GPU/Vulkan/GPU_Vulkan.cpp \
  $(SRC)/GPU/Vulkan/PipelineManagerVulkan.cpp \
  $(SRC)/GPU/Vulkan/ShaderManagerVulkan.cpp \
  $(SRC)/GPU/Vulkan/StateMappingVulkan.cpp \
  $(SRC)/GPU/Vulkan/TextureCacheVulkan.cpp \
  $(SRC)/GPU/Vulkan/VulkanUtil.cpp \
  $(SRC)/GPU/Vulkan/DebugVisVulkan.cpp

EXEC_AND_LIB_FILES := \
  $(ARCH_FILES) \
  $(GPU_VULKAN_FILES) \
  $(SRC)/ext/xxhash.c \
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
  $(SRC)/Core/MIPS/MIPSVFPUFallbacks.cpp.arm \
  $(SRC)/Core/MIPS/MIPSCodeUtils.cpp.arm \
  $(SRC)/Core/MIPS/MIPSDebugInterface.cpp \
  $(SRC)/Core/MIPS/MIPSTracer.cpp \
  $(SRC)/Core/MIPS/IR/IRAnalysis.cpp \
  $(SRC)/Core/MIPS/IR/IRFrontend.cpp \
  $(SRC)/Core/MIPS/IR/IRJit.cpp \
  $(SRC)/Core/MIPS/IR/IRCompALU.cpp \
  $(SRC)/Core/MIPS/IR/IRCompBranch.cpp \
  $(SRC)/Core/MIPS/IR/IRCompFPU.cpp \
  $(SRC)/Core/MIPS/IR/IRCompLoadStore.cpp \
  $(SRC)/Core/MIPS/IR/IRCompVFPU.cpp \
  $(SRC)/Core/MIPS/IR/IRInst.cpp \
  $(SRC)/Core/MIPS/IR/IRInterpreter.cpp \
  $(SRC)/Core/MIPS/IR/IRNativeCommon.cpp \
  $(SRC)/Core/MIPS/IR/IRPassSimplify.cpp \
  $(SRC)/Core/MIPS/IR/IRRegCache.cpp \
  $(SRC)/GPU/Math3D.cpp \
  $(SRC)/GPU/GPU.cpp \
  $(SRC)/GPU/GPUCommon.cpp \
  $(SRC)/GPU/GPUCommonHW.cpp \
  $(SRC)/GPU/GPUState.cpp \
  $(SRC)/GPU/GeConstants.cpp \
  $(SRC)/GPU/GeDisasm.cpp \
  $(SRC)/GPU/Common/Draw2D.cpp \
  $(SRC)/GPU/Common/TextureShaderCommon.cpp \
  $(SRC)/GPU/Common/DepalettizeShaderCommon.cpp \
  $(SRC)/GPU/Common/FragmentShaderGenerator.cpp \
  $(SRC)/GPU/Common/FramebufferManagerCommon.cpp \
  $(SRC)/GPU/Common/PresentationCommon.cpp \
  $(SRC)/GPU/Common/GPUDebugInterface.cpp \
  $(SRC)/GPU/Common/IndexGenerator.cpp.arm \
  $(SRC)/GPU/Common/ShaderId.cpp.arm \
  $(SRC)/GPU/Common/GPUStateUtils.cpp.arm \
  $(SRC)/GPU/Common/SoftwareTransformCommon.cpp.arm \
  $(SRC)/GPU/Common/ReinterpretFramebuffer.cpp \
  $(SRC)/GPU/Common/DepthBufferCommon.cpp \
  $(SRC)/GPU/Common/VertexDecoderCommon.cpp.arm \
  $(SRC)/GPU/Common/VertexDecoderHandwritten.cpp.arm \
  $(SRC)/GPU/Common/TextureCacheCommon.cpp.arm \
  $(SRC)/GPU/Common/TextureScalerCommon.cpp.arm \
  $(SRC)/GPU/Common/ShaderCommon.cpp \
  $(SRC)/GPU/Common/StencilCommon.cpp \
  $(SRC)/GPU/Common/SplineCommon.cpp.arm \
  $(SRC)/GPU/Common/DrawEngineCommon.cpp.arm \
  $(SRC)/GPU/Common/TransformCommon.cpp.arm \
  $(SRC)/GPU/Common/TextureDecoder.cpp \
  $(SRC)/GPU/Common/PostShader.cpp \
  $(SRC)/GPU/Common/ShaderUniforms.cpp \
  $(SRC)/GPU/Common/VertexShaderGenerator.cpp \
  $(SRC)/GPU/Common/GeometryShaderGenerator.cpp \
  $(SRC)/GPU/Common/TextureReplacer.cpp \
  $(SRC)/GPU/Common/ReplacedTexture.cpp \
  $(SRC)/GPU/Debugger/Breakpoints.cpp \
  $(SRC)/GPU/Debugger/Debugger.cpp \
  $(SRC)/GPU/Debugger/GECommandTable.cpp \
  $(SRC)/GPU/Debugger/Playback.cpp \
  $(SRC)/GPU/Debugger/Record.cpp \
  $(SRC)/GPU/Debugger/Stepping.cpp \
  $(SRC)/GPU/GLES/FramebufferManagerGLES.cpp \
  $(SRC)/GPU/GLES/StencilBufferGLES.cpp \
  $(SRC)/GPU/GLES/GPU_GLES.cpp.arm \
  $(SRC)/GPU/GLES/TextureCacheGLES.cpp.arm \
  $(SRC)/GPU/GLES/DrawEngineGLES.cpp.arm \
  $(SRC)/GPU/GLES/StateMappingGLES.cpp.arm \
  $(SRC)/GPU/GLES/ShaderManagerGLES.cpp.arm \
  $(SRC)/GPU/GLES/FragmentTestCacheGLES.cpp.arm \
  $(SRC)/GPU/Software/BinManager.cpp \
  $(SRC)/GPU/Software/Clipper.cpp \
  $(SRC)/GPU/Software/DrawPixel.cpp.arm \
  $(SRC)/GPU/Software/FuncId.cpp \
  $(SRC)/GPU/Software/Lighting.cpp \
  $(SRC)/GPU/Software/Rasterizer.cpp.arm \
  $(SRC)/GPU/Software/RasterizerRectangle.cpp.arm \
  $(SRC)/GPU/Software/RasterizerRegCache.cpp \
  $(SRC)/GPU/Software/Sampler.cpp \
  $(SRC)/GPU/Software/SoftGpu.cpp \
  $(SRC)/GPU/Software/TransformUnit.cpp \
  $(SRC)/Core/ELF/ElfReader.cpp \
  $(SRC)/Core/ELF/PBPReader.cpp \
  $(SRC)/Core/ELF/PrxDecrypter.cpp \
  $(SRC)/Core/ELF/ParamSFO.cpp \
  $(SRC)/Core/HW/SimpleAudioDec.cpp \
  $(SRC)/Core/HW/Atrac3Standalone.cpp \
  $(SRC)/Core/HW/AsyncIOManager.cpp \
  $(SRC)/Core/HW/BufferQueue.cpp \
  $(SRC)/Core/HW/Camera.cpp \
  $(SRC)/Core/HW/Display.cpp \
  $(SRC)/Core/HW/MemoryStick.cpp \
  $(SRC)/Core/HW/MpegDemux.cpp.arm \
  $(SRC)/Core/HW/MediaEngine.cpp.arm \
  $(SRC)/Core/HW/SasAudio.cpp.arm \
  $(SRC)/Core/HW/SasReverb.cpp.arm \
  $(SRC)/Core/HW/StereoResampler.cpp.arm \
  $(SRC)/Core/ControlMapper.cpp \
  $(SRC)/Core/Core.cpp \
  $(SRC)/Core/Compatibility.cpp \
  $(SRC)/Core/Config.cpp \
  $(SRC)/Core/ConfigSettings.cpp \
  $(SRC)/Core/CoreTiming.cpp \
  $(SRC)/Core/CwCheat.cpp \
  $(SRC)/Core/FrameTiming.cpp \
  $(SRC)/Core/HDRemaster.cpp \
  $(SRC)/Core/Instance.cpp \
  $(SRC)/Core/KeyMap.cpp \
  $(SRC)/Core/KeyMapDefaults.cpp \
  $(SRC)/Core/Loaders.cpp \
  $(SRC)/Core/PSPLoaders.cpp \
  $(SRC)/Core/FileLoaders/CachingFileLoader.cpp \
  $(SRC)/Core/FileLoaders/DiskCachingFileLoader.cpp \
  $(SRC)/Core/FileLoaders/HTTPFileLoader.cpp \
  $(SRC)/Core/FileLoaders/LocalFileLoader.cpp \
  $(SRC)/Core/FileLoaders/RamCachingFileLoader.cpp \
  $(SRC)/Core/FileLoaders/RetryingFileLoader.cpp \
  $(SRC)/Core/MemFault.cpp \
  $(SRC)/Core/MemMap.cpp \
  $(SRC)/Core/MemMapFunctions.cpp \
  $(SRC)/Core/Reporting.cpp \
  $(SRC)/Core/Replay.cpp \
  $(SRC)/Core/RetroAchievements.cpp \
  $(SRC)/Core/SaveState.cpp \
  $(SRC)/Core/Screenshot.cpp \
  $(SRC)/Core/System.cpp \
  $(SRC)/Core/TiltEventProcessor.cpp \
  $(SRC)/Core/ThreadPools.cpp \
  $(SRC)/Core/WebServer.cpp \
  $(SRC)/Core/Debugger/Breakpoints.cpp \
  $(SRC)/Core/Debugger/DisassemblyManager.cpp \
  $(SRC)/Core/Debugger/MemBlockInfo.cpp \
  $(SRC)/Core/Debugger/SymbolMap.cpp \
  $(SRC)/Core/Debugger/WebSocket.cpp \
  $(SRC)/Core/Debugger/WebSocket/BreakpointSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/CPUCoreSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/ClientConfigSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/DisasmSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/GameBroadcaster.cpp \
  $(SRC)/Core/Debugger/WebSocket/GameSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/GPUBufferSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/GPURecordSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/GPUStatsSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/HLESubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/InputBroadcaster.cpp \
  $(SRC)/Core/Debugger/WebSocket/InputSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/LogBroadcaster.cpp \
  $(SRC)/Core/Debugger/WebSocket/MemorySubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/MemoryInfoSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/ReplaySubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/SteppingBroadcaster.cpp \
  $(SRC)/Core/Debugger/WebSocket/SteppingSubscriber.cpp \
  $(SRC)/Core/Debugger/WebSocket/WebSocketUtils.cpp \
  $(SRC)/Core/Dialog/PSPDialog.cpp \
  $(SRC)/Core/Dialog/PSPGamedataInstallDialog.cpp \
  $(SRC)/Core/Dialog/PSPMsgDialog.cpp \
  $(SRC)/Core/Dialog/PSPNetconfDialog.cpp \
  $(SRC)/Core/Dialog/PSPNpSigninDialog.cpp \
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
  $(SRC)/Core/HLE/Plugins.cpp \
  $(SRC)/Core/HLE/sceAdler.cpp \
  $(SRC)/Core/HLE/sceAtrac.cpp \
  $(SRC)/Core/HLE/AtracCtx.cpp \
  $(SRC)/Core/HLE/AtracCtx2.cpp \
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
  $(SRC)/Core/HLE/sceKernelHeap.cpp \
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
  $(SRC)/Core/HLE/sceSircs.cpp \
  $(SRC)/Core/HLE/sceSsl.cpp \
  $(SRC)/Core/HLE/sceUmd.cpp \
  $(SRC)/Core/HLE/sceUsb.cpp \
  $(SRC)/Core/HLE/sceUsbAcc.cpp \
  $(SRC)/Core/HLE/sceUsbCam.cpp \
  $(SRC)/Core/HLE/sceUsbGps.cpp \
  $(SRC)/Core/HLE/sceUsbMic.cpp \
  $(SRC)/Core/HLE/sceUtility.cpp \
  $(SRC)/Core/HLE/sceVaudio.cpp \
  $(SRC)/Core/HLE/scePspNpDrm_user.cpp \
  $(SRC)/Core/HLE/sceGameUpdate.cpp \
  $(SRC)/Core/HLE/sceNp.cpp \
  $(SRC)/Core/HLE/sceNp2.cpp \
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
  $(SRC)/Core/Util/MemStick.cpp \
  $(SRC)/Core/Util/PortManager.cpp \
  $(SRC)/Core/Util/GameDB.cpp \
  $(SRC)/Core/Util/GameManager.cpp \
  $(SRC)/Core/Util/BlockAllocator.cpp \
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
  $(SRC)/ext/armips/Archs/ARM/ArmElfRelocator.cpp \
  $(SRC)/ext/armips/Archs/ARM/ArmExpressionFunctions.cpp \
  $(SRC)/ext/armips/Archs/ARM/CArmInstruction.cpp \
  $(SRC)/ext/armips/Archs/ARM/CThumbInstruction.cpp \
  $(SRC)/ext/armips/Archs/ARM/Pool.cpp \
  $(SRC)/ext/armips/Archs/ARM/ThumbOpcodes.cpp \
  $(SRC)/ext/armips/Archs/MIPS/CMipsInstruction.cpp \
  $(SRC)/ext/armips/Archs/MIPS/Mips.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsElfFile.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsElfRelocator.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsExpressionFunctions.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsMacros.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsOpcodes.cpp \
  $(SRC)/ext/armips/Archs/MIPS/MipsParser.cpp \
  $(SRC)/ext/armips/Archs/MIPS/PsxRelocator.cpp \
  $(SRC)/ext/armips/Archs/SuperH/CShInstruction.cpp \
  $(SRC)/ext/armips/Archs/SuperH/ShElfRelocator.cpp \
  $(SRC)/ext/armips/Archs/SuperH/ShExpressionFunctions.cpp \
  $(SRC)/ext/armips/Archs/SuperH/ShOpcodes.cpp \
  $(SRC)/ext/armips/Archs/SuperH/ShParser.cpp \
  $(SRC)/ext/armips/Archs/SuperH/SuperH.cpp \
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
  $(SRC)/ext/armips/Core/Allocations.cpp \
  $(SRC)/ext/armips/Core/Assembler.cpp \
  $(SRC)/ext/armips/Core/Common.cpp \
  $(SRC)/ext/armips/Core/Expression.cpp \
  $(SRC)/ext/armips/Core/ExpressionFunctionHandler.cpp \
  $(SRC)/ext/armips/Core/ExpressionFunctions.cpp \
  $(SRC)/ext/armips/Core/FileManager.cpp \
  $(SRC)/ext/armips/Core/Misc.cpp \
  $(SRC)/ext/armips/Core/SymbolData.cpp \
  $(SRC)/ext/armips/Core/SymbolTable.cpp \
  $(SRC)/ext/armips/Core/Types.cpp \
  $(SRC)/ext/armips/Parser/DirectivesParser.cpp \
  $(SRC)/ext/armips/Parser/ExpressionParser.cpp \
  $(SRC)/ext/armips/Parser/Parser.cpp \
  $(SRC)/ext/armips/Parser/Tokenizer.cpp \
  $(SRC)/ext/armips/Util/ByteArray.cpp \
  $(SRC)/ext/armips/Util/CRC.cpp \
  $(SRC)/ext/armips/Util/EncodingTable.cpp \
  $(SRC)/ext/armips/Util/FileClasses.cpp \
  $(SRC)/ext/armips/Util/FileSystem.cpp \
  $(SRC)/ext/armips/Util/Util.cpp

LOCAL_MODULE := libarmips
LOCAL_SRC_FILES := $(LIBARMIPS_FILES)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Locals.mk

LIBZSTD_FILES := \
  $(SRC)/ext/zstd/lib/common/debug.c \
  $(SRC)/ext/zstd/lib/common/entropy_common.c \
  $(SRC)/ext/zstd/lib/common/error_private.c \
  $(SRC)/ext/zstd/lib/common/fse_decompress.c \
  $(SRC)/ext/zstd/lib/common/pool.c \
  $(SRC)/ext/zstd/lib/common/threading.c \
  $(SRC)/ext/zstd/lib/common/xxhash.c \
  $(SRC)/ext/zstd/lib/common/zstd_common.c \
  $(SRC)/ext/zstd/lib/compress/fse_compress.c \
  $(SRC)/ext/zstd/lib/compress/hist.c \
  $(SRC)/ext/zstd/lib/compress/huf_compress.c \
  $(SRC)/ext/zstd/lib/compress/zstd_compress.c \
  $(SRC)/ext/zstd/lib/compress/zstd_compress_literals.c \
  $(SRC)/ext/zstd/lib/compress/zstd_compress_sequences.c \
  $(SRC)/ext/zstd/lib/compress/zstd_compress_superblock.c \
  $(SRC)/ext/zstd/lib/compress/zstd_double_fast.c \
  $(SRC)/ext/zstd/lib/compress/zstd_fast.c \
  $(SRC)/ext/zstd/lib/compress/zstd_lazy.c \
  $(SRC)/ext/zstd/lib/compress/zstd_ldm.c \
  $(SRC)/ext/zstd/lib/compress/zstd_opt.c \
  $(SRC)/ext/zstd/lib/compress/zstdmt_compress.c \
  $(SRC)/ext/zstd/lib/decompress/huf_decompress.c \
  $(SRC)/ext/zstd/lib/decompress/zstd_ddict.c \
  $(SRC)/ext/zstd/lib/decompress/zstd_decompress.c \
  $(SRC)/ext/zstd/lib/decompress/zstd_decompress_block.c \
  $(SRC)/ext/zstd/lib/dictBuilder/cover.c \
  $(SRC)/ext/zstd/lib/dictBuilder/divsufsort.c \
  $(SRC)/ext/zstd/lib/dictBuilder/fastcover.c \
  $(SRC)/ext/zstd/lib/dictBuilder/zdict.c

ifeq ($(TARGET_ARCH_ABI),x86_64)
LIBZSTD_FILES += $(SRC)/ext/zstd/lib/decompress/huf_decompress_amd64.S
endif

LOCAL_MODULE := libzstd
LOCAL_SRC_FILES := $(LIBZSTD_FILES)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Locals.mk
LOCAL_STATIC_LIBRARIES += ppsspp_common ppsspp_core libarmips libzstd

# These are the files just for ppsspp_jni
LOCAL_MODULE := ppsspp_jni
LOCAL_SRC_FILES := \
  $(SRC)/android/jni/app-android.cpp \
  $(SRC)/android/jni/AndroidJavaGLContext.cpp \
  $(SRC)/android/jni/AndroidVulkanContext.cpp \
  $(SRC)/android/jni/AndroidAudio.cpp \
  $(SRC)/android/jni/OpenSLContext.cpp \
  $(SRC)/UI/AudioCommon.cpp \
  $(SRC)/UI/BackgroundAudio.cpp \
  $(SRC)/UI/DiscordIntegration.cpp \
  $(SRC)/UI/ChatScreen.cpp \
  $(SRC)/UI/DebugOverlay.cpp \
  $(SRC)/UI/DevScreens.cpp \
  $(SRC)/UI/DriverManagerScreen.cpp \
  $(SRC)/UI/DisplayLayoutScreen.cpp \
  $(SRC)/UI/EmuScreen.cpp \
  $(SRC)/UI/MainScreen.cpp \
  $(SRC)/UI/TabbedDialogScreen.cpp \
  $(SRC)/UI/MemStickScreen.cpp \
  $(SRC)/UI/MiscScreens.cpp \
  $(SRC)/UI/RemoteISOScreen.cpp \
  $(SRC)/UI/ReportScreen.cpp \
  $(SRC)/UI/PauseScreen.cpp \
  $(SRC)/UI/SavedataScreen.cpp \
  $(SRC)/UI/Store.cpp \
  $(SRC)/UI/GamepadEmu.cpp \
  $(SRC)/UI/JoystickHistoryView.cpp \
  $(SRC)/UI/GameInfoCache.cpp \
  $(SRC)/UI/GameScreen.cpp \
  $(SRC)/UI/ControlMappingScreen.cpp \
  $(SRC)/UI/GameSettingsScreen.cpp \
  $(SRC)/UI/GPUDriverTestScreen.cpp \
  $(SRC)/UI/TiltAnalogSettingsScreen.cpp \
  $(SRC)/UI/TouchControlLayoutScreen.cpp \
  $(SRC)/UI/TouchControlVisibilityScreen.cpp \
  $(SRC)/UI/CwCheatScreen.cpp \
  $(SRC)/UI/InstallZipScreen.cpp \
  $(SRC)/UI/JitCompareScreen.cpp \
  $(SRC)/UI/OnScreenDisplay.cpp \
  $(SRC)/UI/ProfilerDraw.cpp \
  $(SRC)/UI/NativeApp.cpp \
  $(SRC)/UI/Theme.cpp \
  $(SRC)/UI/CustomButtonMappingScreen.cpp \
  $(SRC)/UI/RetroAchievementScreens.cpp

ifneq ($(SKIPAPP),1)
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(HEADLESS),1)
  include $(CLEAR_VARS)
  include $(LOCAL_PATH)/Locals.mk
  LOCAL_STATIC_LIBRARIES += ppsspp_common ppsspp_core libarmips libzstd

  # Android 5.0 requires PIE for executables.  Only supported on 4.1+, but this is testing anyway.
  LOCAL_CFLAGS += -fPIE
  LOCAL_LDFLAGS += -fPIE -pie

  LOCAL_MODULE := ppsspp_headless
  LOCAL_SRC_FILES := \
    $(SRC)/headless/Headless.cpp \
    $(SRC)/headless/HeadlessHost.cpp \
    $(SRC)/headless/Compare.cpp

  include $(BUILD_EXECUTABLE)
endif

ifeq ($(OPENXR),1)
  LOCAL_CFLAGS += -DOPENXR
endif

ifeq ($(UNITTEST),1)
  include $(CLEAR_VARS)
  include $(LOCAL_PATH)/Locals.mk
  LOCAL_STATIC_LIBRARIES += ppsspp_common ppsspp_core libarmips libzstd

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
      $(SRC)/Common/RiscVEmitter.cpp \
      $(SRC)/Core/MIPS/ARM/ArmRegCacheFPU.cpp \
      $(SRC)/Core/Util/DisArm64.cpp \
      $(SRC)/ext/disarm.cpp \
      $(SRC)/ext/riscv-disas.cpp \
      $(SRC)/unittest/TestArmEmitter.cpp \
      $(SRC)/unittest/TestArm64Emitter.cpp \
      $(SRC)/unittest/TestRiscVEmitter.cpp \
      $(SRC)/unittest/TestX64Emitter.cpp
  endif

  LOCAL_MODULE := ppsspp_unittest
  LOCAL_SRC_FILES := \
    $(SRC)/unittest/JitHarness.cpp \
    $(SRC)/unittest/TestIRPassSimplify.cpp \
    $(SRC)/unittest/TestShaderGenerators.cpp \
    $(SRC)/unittest/TestSoftwareGPUJit.cpp \
    $(SRC)/unittest/TestThreadManager.cpp \
    $(SRC)/unittest/TestVertexJit.cpp \
    $(SRC)/unittest/TestVFS.cpp \
    $(TESTARMEMITTER_FILE) \
    $(SRC)/unittest/UnitTest.cpp

  include $(BUILD_EXECUTABLE)
endif

$(call import-module,libzip)
$(call import-module,glslang-build)
$(call import-module,miniupnp-build)

jni/$(SRC)/git-version.cpp:
	-./git-version-gen.sh
	-..\Windows\git-version-gen.cmd

.PHONY: jni/$(SRC)/git-version.cpp
