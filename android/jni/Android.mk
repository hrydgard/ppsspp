LOCAL_PATH := $(call my-dir)

# BEGIN Native Audio Separate Library - copy paste this section to your Android.mk

include $(CLEAR_VARS)

LOCAL_MODULE := native_audio
LOCAL_CFLAGS := -O3 -fsigned-char -ffast-math -Wall -Wno-multichar -Wno-psabi
# yes, it's really CPPFLAGS for C++
LOCAL_CPPFLAGS := -std=gnu++11 -fno-rtti
NATIVE := ../../native
LOCAL_SRC_FILES := \
		$(NATIVE)/android/native-audio-so.cpp
LOCAL_LDLIBS := -lOpenSLES -llog
		
include $(BUILD_SHARED_LIBRARY)

# END Native Audio Separate Library - copy paste this section to your Android.mk

include $(CLEAR_VARS)

#TARGET_PLATFORM := android-8

LOCAL_MODULE := ppsspp_jni

NATIVE := ../../native
SRC := ../..

LOCAL_CFLAGS := -DUSE_FFMPEG -DUSE_PROFILER -DGL_GLEXT_PROTOTYPES -DUSING_GLES2 -O3 -fsigned-char -Wall -Wno-multichar -Wno-psabi -Wno-unused-variable -fno-strict-aliasing -ffast-math
# yes, it's really CPPFLAGS for C++
LOCAL_CPPFLAGS := -std=gnu++11 -fno-rtti
LOCAL_C_INCLUDES := \
  $(LOCAL_PATH)/../../Common \
  $(LOCAL_PATH)/../.. \
  $(LOCAL_PATH)/$(NATIVE)/base \
  $(LOCAL_PATH)/$(NATIVE)/ext/libzip \
  $(LOCAL_PATH)/$(NATIVE) \
  $(LOCAL_PATH)

LOCAL_STATIC_LIBRARIES := native libzip
LOCAL_LDLIBS := -lz -lGLESv2 -ldl -llog
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv7/lib/libavformat.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv7/lib/libavcodec.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv7/lib/libswresample.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv7/lib/libswscale.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv7/lib/libavutil.a
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../ffmpeg/android/armv7/include
LOCAL_CFLAGS += -DARMEABI_V7A
endif
ifeq ($(TARGET_ARCH_ABI),armeabi)
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv6/lib/libavformat.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv6/lib/libavcodec.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv6/lib/libswresample.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv6/lib/libswscale.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../../ffmpeg/android/armv6/lib/libavutil.a
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../ffmpeg/android/armv6/include
LOCAL_CFLAGS += -DARMEABI
endif

#  $(SRC)/Core/EmuThread.cpp \

# http://software.intel.com/en-us/articles/getting-started-on-optimizing-ndk-project-for-multiple-cpu-architectures


ifeq ($(TARGET_ARCH_ABI),x86) 

LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_IX86

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
  $(SRC)/Core/MIPS/x86/Asm.cpp \
  $(SRC)/Core/MIPS/x86/Jit.cpp \
  $(SRC)/Core/MIPS/x86/RegCache.cpp \
  $(SRC)/Core/MIPS/x86/RegCacheFPU.cpp
endif

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)

LOCAL_CFLAGS := $(LOCAL_CFLAGS) -DARM -DARMV7

ARCH_FILES := \
  $(SRC)/Common/ArmEmitter.cpp \
  $(SRC)/Common/ArmCPUDetect.cpp \
  $(SRC)/Common/ArmThunk.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompALU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompBranch.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompLoadStore.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmAsm.cpp \
  $(SRC)/Core/MIPS/ARM/ArmJit.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCache.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCacheFPU.cpp \
  ArmEmitterTest.cpp \

endif

ifeq ($(TARGET_ARCH_ABI),armeabi)

LOCAL_CFLAGS := $(LOCAL_CFLAGS) -DARM

ARCH_FILES := \
  $(SRC)/Common/ArmEmitter.cpp \
  $(SRC)/Common/ArmCPUDetect.cpp \
  $(SRC)/Common/ArmThunk.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompALU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompBranch.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompLoadStore.cpp \
  $(SRC)/Core/MIPS/ARM/ArmCompVFPU.cpp \
  $(SRC)/Core/MIPS/ARM/ArmAsm.cpp \
  $(SRC)/Core/MIPS/ARM/ArmJit.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCache.cpp \
  $(SRC)/Core/MIPS/ARM/ArmRegCacheFPU.cpp \
  ArmEmitterTest.cpp \

endif

LOCAL_SRC_FILES := \
  $(ARCH_FILES) \
  TestRunner.cpp \
  $(SRC)/UI/ui_atlas.cpp \
  $(SRC)/UI/NativeApp.cpp \
  $(SRC)/UI/EmuScreen.cpp \
  $(SRC)/UI/MenuScreens.cpp \
  $(SRC)/UI/UIShader.cpp \
  $(SRC)/UI/GamepadEmu.cpp \
  $(SRC)/UI/GameInfoCache.cpp \
  $(SRC)/UI/OnScreenDisplay.cpp \
  $(SRC)/UI/PluginScreen.cpp \
  $(SRC)/UI/GameScreen.cpp \
  $(SRC)/UI/GameSettingsScreen.cpp \
  $(SRC)/native/android/app-android.cpp \
  $(SRC)/ext/disarm.cpp \
  $(SRC)/ext/libkirk/AES.c \
  $(SRC)/ext/libkirk/amctrl.c \
  $(SRC)/ext/libkirk/SHA1.c \
  $(SRC)/ext/libkirk/bn.c \
  $(SRC)/ext/libkirk/ec.c \
  $(SRC)/ext/libkirk/kirk_engine.c \
  $(SRC)/ext/snappy/snappy-c.cpp \
  $(SRC)/ext/snappy/snappy.cpp \
  $(SRC)/ext/xbrz/xbrz.cpp \
  $(SRC)/Common/Crypto/md5.cpp \
  $(SRC)/Common/LogManager.cpp \
  $(SRC)/Common/MemArena.cpp \
  $(SRC)/Common/MemoryUtil.cpp \
  $(SRC)/Common/MsgHandler.cpp \
  $(SRC)/Common/FileUtil.cpp \
  $(SRC)/Common/StringUtils.cpp \
  $(SRC)/Common/Thread.cpp \
  $(SRC)/Common/ThreadPools.cpp \
  $(SRC)/Common/Timer.cpp \
  $(SRC)/Common/Misc.cpp \
  $(SRC)/Common/MathUtil.cpp \
  $(SRC)/GPU/Math3D.cpp \
  $(SRC)/GPU/GPUCommon.cpp \
  $(SRC)/GPU/GPUState.cpp \
  $(SRC)/GPU/GeDisasm.cpp \
  $(SRC)/GPU/GLES/Framebuffer.cpp \
  $(SRC)/GPU/GLES/DisplayListInterpreter.cpp.arm \
  $(SRC)/GPU/GLES/TextureCache.cpp.arm \
  $(SRC)/GPU/GLES/IndexGenerator.cpp.arm \
  $(SRC)/GPU/GLES/TransformPipeline.cpp.arm \
  $(SRC)/GPU/GLES/StateMapping.cpp.arm \
  $(SRC)/GPU/GLES/VertexDecoder.cpp.arm \
  $(SRC)/GPU/GLES/ShaderManager.cpp \
  $(SRC)/GPU/GLES/VertexShaderGenerator.cpp \
  $(SRC)/GPU/GLES/FragmentShaderGenerator.cpp \
  $(SRC)/GPU/GLES/TextureScaler.cpp \
  $(SRC)/GPU/Null/NullGpu.cpp \
  $(SRC)/Core/ELF/ElfReader.cpp \
  $(SRC)/Core/ELF/PBPReader.cpp \
  $(SRC)/Core/ELF/PrxDecrypter.cpp \
  $(SRC)/Core/ELF/ParamSFO.cpp \
  $(SRC)/Core/HW/atrac3plus.cpp \
  $(SRC)/Core/HW/MemoryStick.cpp \
  $(SRC)/Core/HW/MpegDemux.cpp.arm \
  $(SRC)/Core/HW/OMAConvert.cpp.arm \
  $(SRC)/Core/HW/MediaEngine.cpp.arm \
  $(SRC)/Core/HW/SasAudio.cpp.arm \
  $(SRC)/Core/Core.cpp \
  $(SRC)/Core/Config.cpp \
  $(SRC)/Core/CoreTiming.cpp \
  $(SRC)/Core/CPU.cpp \
  $(SRC)/Core/CwCheat.cpp \
  $(SRC)/Core/Host.cpp \
  $(SRC)/Core/Loaders.cpp \
  $(SRC)/Core/PSPLoaders.cpp \
  $(SRC)/Core/MemMap.cpp \
  $(SRC)/Core/MemMapFunctions.cpp \
  $(SRC)/Core/Reporting.cpp \
  $(SRC)/Core/SaveState.cpp \
  $(SRC)/Core/System.cpp \
  $(SRC)/Core/PSPMixer.cpp \
  $(SRC)/Core/Debugger/Breakpoints.cpp \
  $(SRC)/Core/Debugger/SymbolMap.cpp \
  $(SRC)/Core/Dialog/PSPDialog.cpp \
  $(SRC)/Core/Dialog/PSPMsgDialog.cpp \
  $(SRC)/Core/Dialog/PSPOskDialog.cpp \
  $(SRC)/Core/Dialog/PSPPlaceholderDialog.cpp \
  $(SRC)/Core/Dialog/PSPSaveDialog.cpp \
  $(SRC)/Core/Dialog/SavedataParam.cpp \
  $(SRC)/Core/Font/PGF.cpp \
  $(SRC)/Core/HLE/HLETables.cpp \
  $(SRC)/Core/HLE/HLE.cpp \
  $(SRC)/Core/HLE/sceAtrac.cpp \
  $(SRC)/Core/HLE/__sceAudio.cpp \
  $(SRC)/Core/HLE/sceAudio.cpp \
  $(SRC)/Core/HLE/sceAudiocodec.cpp \
  $(SRC)/Core/HLE/sceChnnlsv.cpp \
  $(SRC)/Core/HLE/sceCtrl.cpp \
  $(SRC)/Core/HLE/sceDeflt.cpp \
  $(SRC)/Core/HLE/sceDisplay.cpp \
  $(SRC)/Core/HLE/sceDmac.cpp \
  $(SRC)/Core/HLE/sceGe.cpp \
  $(SRC)/Core/HLE/sceFont.cpp \
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
  $(SRC)/Core/HLE/sceOpenPSID.cpp \
  $(SRC)/Core/HLE/sceP3da.cpp \
  $(SRC)/Core/HLE/sceParseHttp.cpp \
  $(SRC)/Core/HLE/sceParseUri.cpp \
  $(SRC)/Core/HLE/scePower.cpp \
  $(SRC)/Core/HLE/sceRtc.cpp \
  $(SRC)/Core/HLE/scePsmf.cpp \
  $(SRC)/Core/HLE/sceSas.cpp \
  $(SRC)/Core/HLE/sceSsl.cpp \
  $(SRC)/Core/HLE/sceUmd.cpp \
  $(SRC)/Core/HLE/sceUsb.cpp \
  $(SRC)/Core/HLE/sceUtility.cpp \
  $(SRC)/Core/HLE/sceVaudio.cpp \
  $(SRC)/Core/HLE/scePspNpDrm_user.cpp \
  $(SRC)/Core/HLE/sceGameUpdate.cpp \
  $(SRC)/Core/HLE/sceNp.cpp \
  $(SRC)/Core/HLE/scePauth.cpp \
  $(SRC)/Core/FileSystems/BlockDevices.cpp \
  $(SRC)/Core/FileSystems/ISOFileSystem.cpp \
  $(SRC)/Core/FileSystems/MetaFileSystem.cpp \
  $(SRC)/Core/FileSystems/DirectoryFileSystem.cpp \
  $(SRC)/Core/FileSystems/tlzrc.cpp \
  $(SRC)/Core/MIPS/MIPS.cpp.arm \
  $(SRC)/Core/MIPS/MIPSAnalyst.cpp \
  $(SRC)/Core/MIPS/MIPSDis.cpp \
  $(SRC)/Core/MIPS/MIPSDisVFPU.cpp \
  $(SRC)/Core/MIPS/MIPSInt.cpp.arm \
  $(SRC)/Core/MIPS/MIPSIntVFPU.cpp.arm \
  $(SRC)/Core/MIPS/MIPSTables.cpp.arm \
  $(SRC)/Core/MIPS/MIPSVFPUUtils.cpp.arm \
  $(SRC)/Core/MIPS/MIPSCodeUtils.cpp.arm \
  $(SRC)/Core/MIPS/MIPSDebugInterface.cpp \
  $(SRC)/Core/MIPS/JitCommon/JitCommon.cpp \
  $(SRC)/Core/MIPS/JitCommon/JitBlockCache.cpp \
  $(SRC)/Core/Util/BlockAllocator.cpp \
  $(SRC)/Core/Util/ppge_atlas.cpp \
  $(SRC)/Core/Util/PPGeDraw.cpp \
  $(SRC)/git-version.cpp


include $(BUILD_SHARED_LIBRARY)

$(call import-module,libzip)
$(call import-module,native)

jni/$(SRC)/git-version.cpp:
	-./git-version-gen.sh
	-..\Windows\git-version-gen.cmd

.PHONY: jni/$(SRC)/git-version.cpp
