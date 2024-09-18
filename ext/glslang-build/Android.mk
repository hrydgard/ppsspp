# Basic Android.mk for glslang in PPSSPP

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libglslang-build
LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES := \
    ../glslang/glslang/GenericCodeGen/CodeGen.cpp \
    ../glslang/glslang/GenericCodeGen/Link.cpp \
    ../glslang/glslang/MachineIndependent/attribute.cpp \
    ../glslang/glslang/MachineIndependent/Constant.cpp \
    ../glslang/glslang/MachineIndependent/glslang_tab.cpp \
    ../glslang/glslang/MachineIndependent/InfoSink.cpp \
    ../glslang/glslang/MachineIndependent/Initialize.cpp \
    ../glslang/glslang/MachineIndependent/Intermediate.cpp \
    ../glslang/glslang/MachineIndependent/intermOut.cpp \
    ../glslang/glslang/MachineIndependent/IntermTraverse.cpp \
    ../glslang/glslang/MachineIndependent/iomapper.cpp \
    ../glslang/glslang/MachineIndependent/limits.cpp \
    ../glslang/glslang/MachineIndependent/linkValidate.cpp \
    ../glslang/glslang/MachineIndependent/parseConst.cpp \
    ../glslang/glslang/MachineIndependent/ParseContextBase.cpp \
    ../glslang/glslang/MachineIndependent/ParseHelper.cpp \
    ../glslang/glslang/MachineIndependent/PoolAlloc.cpp \
    ../glslang/glslang/MachineIndependent/propagateNoContraction.cpp \
    ../glslang/glslang/MachineIndependent/reflection.cpp \
    ../glslang/glslang/MachineIndependent/RemoveTree.cpp \
    ../glslang/glslang/MachineIndependent/Scan.cpp \
    ../glslang/glslang/MachineIndependent/ShaderLang.cpp \
    ../glslang/glslang/MachineIndependent/SpirvIntrinsics.cpp \
    ../glslang/glslang/MachineIndependent/SymbolTable.cpp \
    ../glslang/glslang/MachineIndependent/Versions.cpp \
    ../glslang/glslang/MachineIndependent/preprocessor/Pp.cpp \
    ../glslang/glslang/MachineIndependent/preprocessor/PpAtom.cpp \
    ../glslang/glslang/MachineIndependent/preprocessor/PpContext.cpp \
    ../glslang/glslang/MachineIndependent/preprocessor/PpScanner.cpp \
    ../glslang/glslang/MachineIndependent/preprocessor/PpTokens.cpp \
    ../glslang/glslang/OSDependent/Unix/ossource.cpp \
    ../glslang/glslang/HLSL/hlslAttributes.cpp \
    ../glslang/glslang/HLSL/hlslGrammar.cpp \
    ../glslang/glslang/HLSL/hlslOpMap.cpp \
    ../glslang/glslang/HLSL/hlslParseables.cpp \
    ../glslang/glslang/HLSL/hlslParseHelper.cpp \
    ../glslang/glslang/HLSL/hlslScanContext.cpp \
    ../glslang/glslang/HLSL/hlslTokenStream.cpp \
    ../glslang/SPIRV/disassemble.cpp \
    ../glslang/SPIRV/doc.cpp \
    ../glslang/SPIRV/GlslangToSpv.cpp \
    ../glslang/SPIRV/Logger.cpp \
    ../glslang/SPIRV/InReadableOrder.cpp \
    ../glslang/SPIRV/SpvBuilder.cpp \
    ../glslang/SPIRV/SPVRemapper.cpp \
    ../glslang/SPIRV/SpvPostProcess.cpp \
    ../glslang/SPIRV/SpvTools.cpp \
    ../glslang/OGLCompilersDLL/InitializeDll.cpp


LOCAL_CFLAGS := -O3 -fsigned-char -fno-strict-aliasing -Wall -Wno-multichar -D__STDC_CONSTANT_MACROS -DENABLE_HLSL
LOCAL_CPPFLAGS := -fno-exceptions -std=gnu++11 -fno-rtti -Wno-reorder
# Note: LOCAL_PATH is the directory this file is in.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/.. $(LOCAL_PATH)/../libzip $(LOCAL_PATH)/../glslang ..

ifeq ($(findstring armeabi-v7a,$(TARGET_ARCH_ABI)),armeabi-v7a)
LOCAL_CFLAGS := $(LOCAL_CFLAGS)
else ifeq ($(TARGET_ARCH_ABI),armeabi)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -march=armv6
else ifeq ($(TARGET_ARCH_ABI),x86)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_IX86
else ifeq ($(TARGET_ARCH_ABI),x86_64)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_X64
endif

include $(BUILD_STATIC_LIBRARY)
