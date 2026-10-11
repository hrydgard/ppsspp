set(CoreExtra)

list(APPEND CoreExtra
	MIPS/IR/IRAnalysis.cpp
	MIPS/IR/IRAnalysis.h
	MIPS/IR/IRCompALU.cpp
	MIPS/IR/IRCompBranch.cpp
	MIPS/IR/IRCompFPU.cpp
	MIPS/IR/IRCompLoadStore.cpp
	MIPS/IR/IRCompVFPU.cpp
	MIPS/IR/IRFrontend.cpp
	MIPS/IR/IRFrontend.h
	MIPS/IR/IRInst.cpp
	MIPS/IR/IRInst.h
	MIPS/IR/IRInterpreter.cpp
	MIPS/IR/IRInterpreter.h
	MIPS/IR/IRJit.cpp
	MIPS/IR/IRJit.h
	MIPS/IR/IRNativeCommon.cpp
	MIPS/IR/IRNativeCommon.h
	MIPS/IR/IRPassSimplify.cpp
	MIPS/IR/IRPassSimplify.h
	MIPS/IR/IRRegCache.cpp
	MIPS/IR/IRRegCache.h
)

list(APPEND CoreExtra
	MIPS/ARM/ArmAsm.cpp
	MIPS/ARM/ArmCompALU.cpp
	MIPS/ARM/ArmCompBranch.cpp
	MIPS/ARM/ArmCompFPU.cpp
	MIPS/ARM/ArmCompLoadStore.cpp
	MIPS/ARM/ArmCompVFPU.cpp
	MIPS/ARM/ArmCompReplace.cpp
	MIPS/ARM/ArmJit.cpp
	MIPS/ARM/ArmJit.h
	MIPS/ARM/ArmRegCache.cpp
	MIPS/ARM/ArmRegCache.h
	MIPS/ARM/ArmRegCacheFPU.cpp
	MIPS/ARM/ArmRegCacheFPU.h
)

list(APPEND CoreExtra
	MIPS/ARM64/Arm64Asm.cpp
	MIPS/ARM64/Arm64CompALU.cpp
	MIPS/ARM64/Arm64CompBranch.cpp
	MIPS/ARM64/Arm64CompFPU.cpp
	MIPS/ARM64/Arm64CompLoadStore.cpp
	MIPS/ARM64/Arm64CompVFPU.cpp
	MIPS/ARM64/Arm64CompReplace.cpp
	MIPS/ARM64/Arm64Jit.cpp
	MIPS/ARM64/Arm64Jit.h
	MIPS/ARM64/Arm64RegCache.cpp
	MIPS/ARM64/Arm64RegCache.h
	MIPS/ARM64/Arm64RegCacheFPU.cpp
	MIPS/ARM64/Arm64RegCacheFPU.h
	MIPS/ARM64/Arm64IRAsm.cpp
	MIPS/ARM64/Arm64IRCompALU.cpp
	MIPS/ARM64/Arm64IRCompBranch.cpp
	MIPS/ARM64/Arm64IRCompFPU.cpp
	MIPS/ARM64/Arm64IRCompLoadStore.cpp
	MIPS/ARM64/Arm64IRCompSystem.cpp
	MIPS/ARM64/Arm64IRCompVec.cpp
	MIPS/ARM64/Arm64IRJit.cpp
	MIPS/ARM64/Arm64IRJit.h
	MIPS/ARM64/Arm64IRRegCache.cpp
	MIPS/ARM64/Arm64IRRegCache.h
	Util/DisArm64.cpp
)

list(APPEND CoreExtra
	MIPS/x86/Asm.cpp
	MIPS/x86/CompALU.cpp
	MIPS/x86/CompBranch.cpp
	MIPS/x86/CompFPU.cpp
	MIPS/x86/CompLoadStore.cpp
	MIPS/x86/CompVFPU.cpp
	MIPS/x86/CompReplace.cpp
	MIPS/x86/Jit.cpp
	MIPS/x86/Jit.h
	MIPS/x86/JitSafeMem.cpp
	MIPS/x86/JitSafeMem.h
	MIPS/x86/RegCache.cpp
	MIPS/x86/RegCache.h
	MIPS/x86/RegCacheFPU.cpp
	MIPS/x86/RegCacheFPU.h
	MIPS/x86/X64IRAsm.cpp
	MIPS/x86/X64IRCompALU.cpp
	MIPS/x86/X64IRCompBranch.cpp
	MIPS/x86/X64IRCompFPU.cpp
	MIPS/x86/X64IRCompLoadStore.cpp
	MIPS/x86/X64IRCompSystem.cpp
	MIPS/x86/X64IRCompVec.cpp
	MIPS/x86/X64IRJit.cpp
	MIPS/x86/X64IRJit.h
	MIPS/x86/X64IRRegCache.cpp
	MIPS/x86/X64IRRegCache.h
)

list(APPEND CoreExtra
	MIPS/RiscV/RiscVAsm.cpp
	MIPS/RiscV/RiscVCompALU.cpp
	MIPS/RiscV/RiscVCompBranch.cpp
	MIPS/RiscV/RiscVCompFPU.cpp
	MIPS/RiscV/RiscVCompLoadStore.cpp
	MIPS/RiscV/RiscVCompSystem.cpp
	MIPS/RiscV/RiscVCompVec.cpp
	MIPS/RiscV/RiscVJit.cpp
	MIPS/RiscV/RiscVJit.h
	MIPS/RiscV/RiscVRegCache.cpp
	MIPS/RiscV/RiscVRegCache.h
)

list(APPEND CoreExtra
	MIPS/LoongArch64/LoongArch64Asm.cpp
	MIPS/LoongArch64/LoongArch64CompALU.cpp
	MIPS/LoongArch64/LoongArch64CompBranch.cpp
	MIPS/LoongArch64/LoongArch64CompFPU.cpp
	MIPS/LoongArch64/LoongArch64CompLoadStore.cpp
	MIPS/LoongArch64/LoongArch64CompSystem.cpp
	MIPS/LoongArch64/LoongArch64CompVec.cpp
	MIPS/LoongArch64/LoongArch64Jit.cpp
	MIPS/LoongArch64/LoongArch64Jit.h
	MIPS/LoongArch64/LoongArch64RegCache.cpp
	MIPS/LoongArch64/LoongArch64RegCache.h
)

if(NOT MOBILE_DEVICE)
	list(APPEND CoreExtra
	)
endif()


add_library(Core STATIC
	${CoreExtra}

	Config.cpp
	Config.h
	ConfigSettings.cpp
	ConfigSettings.h
	ConfigValues.h
	Core.cpp
	Core.h
	Compatibility.cpp
	Compatibility.h
	CoreParameter.h
	CoreTiming.cpp
	CoreTiming.h
	CwCheat.cpp
	CwCheat.h
	HDRemaster.cpp
	HDRemaster.h
	Instance.cpp
	Instance.h
	KeyMap.cpp
	KeyMap.h
	KeyMapDefaults.cpp
	KeyMapDefaults.h
	LuaContext.cpp
	LuaContext.h
	Debugger/Breakpoints.cpp
	Debugger/Breakpoints.h
	Debugger/DebugInterface.h
	Debugger/LineInfo.cpp
	Debugger/LineInfo.h
	Debugger/MemBlockInfo.cpp
	Debugger/MemBlockInfo.h
	Debugger/SymbolMap.cpp
	Debugger/SymbolMap.h
	Debugger/Watch.h
	Dialog/PSPDialog.cpp
	Dialog/PSPDialog.h
	Dialog/PSPGamedataInstallDialog.cpp
	Dialog/PSPGamedataInstallDialog.h
	Dialog/PSPMsgDialog.cpp
	Dialog/PSPMsgDialog.h
	Dialog/PSPNetconfDialog.cpp
	Dialog/PSPNetconfDialog.h
	Dialog/PSPNpSigninDialog.cpp
	Dialog/PSPHtmlViewerDialog.cpp
	Dialog/PSPNpSigninDialog.h
	Dialog/PSPHtmlViewerDialog.h
	Dialog/PSPOskDialog.cpp
	Dialog/PSPOskDialog.h
	Dialog/PSPOskConstants.cpp
	Dialog/PSPOskConstants.h
	Dialog/PSPPlaceholderDialog.cpp
	Dialog/PSPPlaceholderDialog.h
	Dialog/PSPSaveDialog.cpp
	Dialog/PSPSaveDialog.h
	Dialog/PSPScreenshotDialog.cpp
	Dialog/PSPScreenshotDialog.h
	Dialog/SavedataParam.cpp
	Dialog/SavedataParam.h
	ELF/ElfReader.cpp
	ELF/ElfReader.h
	ELF/PSPElfTypes.h
	ELF/PBPReader.cpp
	ELF/PBPReader.h
	ELF/PrxDecrypter.cpp
	ELF/PrxDecrypter.h
	ELF/ParamSFO.cpp
	ELF/ParamSFO.h
	FFMPEGCompat.h
	FileSystems/tlzrc.cpp
	FileSystems/BlobFileSystem.cpp
	FileSystems/BlobFileSystem.h
	FileSystems/BlockDevices.cpp
	FileSystems/BlockDevices.h
	FileSystems/DirectoryFileSystem.cpp
	FileSystems/DirectoryFileSystem.h
	FileSystems/FileSystem.h
	FileSystems/FileSystem.cpp
	FileSystems/ISOFileSystem.cpp
	FileSystems/ISOFileSystem.h
	FileSystems/MetaFileSystem.cpp
	FileSystems/MetaFileSystem.h
	FileSystems/VirtualDiscFileSystem.cpp
	FileSystems/VirtualDiscFileSystem.h
	Font/PGF.cpp
	Font/PGF.h
	HLE/FunctionWrappers.h
	HLE/HLE.cpp
	HLE/HLE.h
	HLE/HLEUtil.h
	HLE/ReplaceTables.cpp
	HLE/ReplaceTables.h
	HLE/HLEHelperThread.cpp
	HLE/HLEHelperThread.h
	HLE/HLETables.cpp
	HLE/HLETables.h
	HLE/KernelWaitHelpers.h
	HLE/PSPThreadContext.h
	HLE/KUBridge.h
	HLE/KUBridge.cpp
	HLE/Plugins.h
	HLE/Plugins.cpp
	HLE/ThreadQueueList.h
	HLE/__sceAudio.cpp
	HLE/__sceAudio.h
	HLE/sceAdler.cpp
	HLE/sceAdler.h
	HLE/AtracBase.h
	HLE/AtracCtx.cpp
	HLE/AtracCtx.h
	HLE/AtracCtx2.cpp
	HLE/AtracCtx2.h
	HLE/NetInetConstants.cpp
	HLE/NetInetConstants.h
	HLE/SocketManager.cpp
	HLE/SocketManager.h
	HLE/sceAtrac.cpp
	HLE/sceAtrac.h
	HLE/sceAudio.cpp
	HLE/sceAudiocodec.cpp
	HLE/sceAudiocodec.h
	HLE/sceAudioRouting.cpp
	HLE/sceAudioRouting.h
	HLE/sceAudio.h
	HLE/sceCcc.h
	HLE/sceCcc.cpp
	HLE/sceChkreg.cpp
	HLE/sceChkreg.h
	HLE/sceChnnlsv.cpp
	HLE/sceChnnlsv.h
	HLE/sceCtrl.cpp
	HLE/sceCtrl.h
	HLE/sceDeflt.cpp
	HLE/sceDeflt.h
	HLE/sceDisplay.cpp
	HLE/sceDisplay.h
	HLE/sceDmac.cpp
	HLE/sceDmac.h
	HLE/sceG729.cpp
	HLE/sceG729.h
	HLE/sceGameUpdate.cpp
	HLE/sceGameUpdate.h
	HLE/sceGe.cpp
	HLE/sceGe.h
	HLE/sceFont.cpp
	HLE/sceFont.h
	HLE/sceHeap.cpp
	HLE/sceHeap.h
	HLE/sceHprm.cpp
	HLE/sceHprm.h
	HLE/sceHttp.cpp
	HLE/sceHttp.h
	HLE/sceImpose.cpp
	HLE/sceImpose.h
	HLE/sceIo.cpp
	HLE/sceIo.h
	HLE/sceJpeg.cpp
	HLE/sceJpeg.h
	HLE/sceKernel.cpp
	HLE/sceKernel.h
	HLE/sceKernelAlarm.cpp
	HLE/sceKernelAlarm.h
	HLE/sceKernelEventFlag.cpp
	HLE/sceKernelEventFlag.h
	HLE/sceKernelHeap.cpp
	HLE/sceKernelHeap.h
	HLE/sceKernelInterrupt.cpp
	HLE/sceKernelInterrupt.h
	HLE/sceKernelMbx.cpp
	HLE/sceKernelMbx.h
	HLE/sceKernelMemory.cpp
	HLE/sceKernelMemory.h
	HLE/sceKernelModule.cpp
	HLE/sceKernelModule.h
	HLE/sceKernelMsgPipe.cpp
	HLE/sceKernelMsgPipe.h
	HLE/sceKernelMutex.cpp
	HLE/sceKernelMutex.h
	HLE/sceKernelSemaphore.cpp
	HLE/sceKernelSemaphore.h
	HLE/sceKernelThread.cpp
	HLE/sceKernelThread.h
	HLE/sceKernelTime.cpp
	HLE/sceKernelTime.h
	HLE/sceKernelVTimer.cpp
	HLE/sceKernelVTimer.h
	HLE/sceVideocodec.cpp
	HLE/sceVideocodec.h
	HLE/sceMpeg.cpp
	HLE/sceMpeg.h
	HLE/sceMpegbase.cpp
	HLE/sceMpegbase.h
	HLE/sceNet.cpp
	HLE/sceNet.h
	HLE/sceNet_lib.cpp
	HLE/sceNet_lib.h
	HLE/NetAdhocCommon.cpp
	HLE/NetAdhocCommon.h
	HLE/sceNetAdhoc.cpp
	HLE/sceNetAdhoc.h
	HLE/sceNetAdhocMatching.cpp
	HLE/sceNetAdhocMatching.h
	HLE/sceNetInet.cpp
	HLE/sceNetInet.h
	HLE/sceNetApctl.cpp
	HLE/sceNetApctl.h
	HLE/sceNetResolver.cpp
	HLE/sceNetResolver.h
	HLE/proAdhoc.h
	HLE/proAdhoc.cpp
	HLE/proAdhocServer.h
	HLE/proAdhocServer.cpp
	HLE/sceOpenPSID.cpp
	HLE/sceOpenPSID.h
	HLE/sceResmgr.cpp
	HLE/sceResmgr.h
	HLE/sceP3da.cpp
	HLE/sceP3da.h
	HLE/sceMt19937.cpp
	HLE/sceMt19937.h
	HLE/sceMd5.cpp
	HLE/sceMd5.h
	HLE/sceAac.cpp
	HLE/sceAac.h
	HLE/sceMp4.cpp
	HLE/sceMp4.h
	HLE/sceMp3.cpp
	HLE/sceMp3.h
	HLE/sceParseHttp.cpp
	HLE/sceParseHttp.h
	HLE/sceParseUri.cpp
	HLE/sceParseUri.h
	HLE/scePower.cpp
	HLE/scePower.h
	HLE/scePsmf.cpp
	HLE/scePsmf.h
	HLE/sceReg.cpp
	HLE/sceReg.h
	HLE/sceRtc.cpp
	HLE/sceRtc.h
	HLE/sceSas.cpp
	HLE/sceSas.h
	HLE/sceSfmt19937.cpp
	HLE/sceSfmt19937.h
	HLE/sceSha256.cpp
	HLE/sceSha256.h
	HLE/sceSircs.cpp
	HLE/sceSircs.h
	HLE/sceSsl.cpp
	HLE/sceSsl.h
	HLE/sceUmd.cpp
	HLE/sceUmd.h
	HLE/sceUsb.cpp
	HLE/sceUsb.h
	HLE/sceUsbAcc.cpp
	HLE/sceUsbAcc.h
	HLE/sceUsbCam.cpp
	HLE/sceUsbCam.h
	HLE/sceUsbGps.cpp
	HLE/sceUsbGps.h
	HLE/sceUsbMic.cpp
	HLE/sceUsbMic.h
	HLE/sceUtility.cpp
	HLE/sceUtility.h
	HLE/sceVaudio.cpp
	HLE/sceVaudio.h
	HLE/sceVshBridge.cpp
	HLE/sceVshBridge.h
	HLE/scePspNpDrm_user.cpp
	HLE/scePspNpDrm_user.h
	HLE/sceNp.cpp
	HLE/sceNp.h
	HLE/sceNp2.cpp
	HLE/sceNp2.h
	HLE/scePauth.cpp
	HLE/scePauth.h
	HW/SimpleAudioDec.cpp
	HW/SimpleAudioDec.h
	HW/Atrac3Standalone.cpp
	HW/Atrac3Standalone.h
	HW/SimpleAudioDec.h
	HW/AsyncIOManager.cpp
	HW/AsyncIOManager.h
	HW/BufferQueue.cpp
	HW/BufferQueue.h
	HW/Camera.cpp
	HW/Camera.h
	HW/Display.cpp
	HW/Display.h
	HW/GpioMMIO.cpp
	HW/GpioMMIO.h
	HW/AvcDecoder.cpp
	HW/AvcDecoder.h
	HW/MediaEngine.cpp
	HW/MediaEngine.h
	HW/MpegDemux.cpp
	HW/MpegDemux.h
	HW/MemoryStick.cpp
	HW/MemoryStick.h
	HW/SasAudio.cpp
	HW/SasAudio.h
	HW/SasReverb.cpp
	HW/SasReverb.h
	Loaders.cpp
	Loaders.h
	FileLoaders/CachingFileLoader.cpp
	FileLoaders/CachingFileLoader.h
	FileLoaders/DiskCachingFileLoader.cpp
	FileLoaders/DiskCachingFileLoader.h
	FileLoaders/HTTPFileLoader.cpp
	FileLoaders/HTTPFileLoader.h
	FileLoaders/LocalFileLoader.cpp
	FileLoaders/LocalFileLoader.h
	FileLoaders/RamCachingFileLoader.cpp
	FileLoaders/RamCachingFileLoader.h
	FileLoaders/RetryingFileLoader.cpp
	FileLoaders/RetryingFileLoader.h
	FileLoaders/ZipFileLoader.cpp
	FileLoaders/ZipFileLoader.h
	MIPS/MIPS.cpp
	MIPS/MIPS.h
	MIPS/MIPSAnalyst.cpp
	MIPS/MIPSAnalyst.h
	MIPS/MIPSCodeUtils.cpp
	MIPS/MIPSCodeUtils.h
	MIPS/MIPSDebugInterface.cpp
	MIPS/MIPSDebugInterface.h
	MIPS/MIPSDis.cpp
	MIPS/MIPSDis.h
	MIPS/MIPSDisVFPU.cpp
	MIPS/MIPSDisVFPU.h
	MIPS/Interpreter.cpp
	MIPS/Interpreter.h
	MIPS/InterpreterDispatch.cpp
	MIPS/InterpreterDispatch.h
	MIPS/InterpreterVFPU.cpp
	MIPS/InterpreterVFPU.h
	MIPS/MIPSStackWalk.cpp
	MIPS/MIPSStackWalk.h
	MIPS/MIPSTables.cpp
	MIPS/MIPSTables.h
	MIPS/MIPSVFPUUtils.cpp
	MIPS/MIPSVFPUUtils.h
	MIPS/MIPSTracer.cpp
	MIPS/MIPSTracer.h
	MIPS/JitCommon/JitCommon.cpp
	MIPS/JitCommon/JitCommon.h
	MIPS/JitCommon/JitBlockCache.cpp
	MIPS/JitCommon/JitBlockCache.h
	MIPS/JitCommon/JitState.cpp
	MIPS/JitCommon/JitState.h
	Util/DisArm64.h
	Util/DisArm64.cpp
	MemFault.cpp
	MemFault.h
	MemMap.cpp
	MemMap.h
	MemMapFunctions.cpp
	MemMapHelpers.h
	PSPLoaders.cpp
	PSPLoaders.h
	Replay.cpp
	Replay.h
	SaveState.cpp
	SaveState.h
	SaveStateRewind.cpp
	SaveStateRewind.h
	Screenshot.cpp
	Screenshot.h
	System.cpp
	System.h
	Util/AtracTrack.cpp
	Util/AtracTrack.h
	Util/AudioFormat.cpp
	Util/AudioFormat.h
	Util/GameDB.cpp
	Util/GameDB.h
	Util/PathUtil.cpp
	Util/PathUtil.h
	Util/BlockAllocator.cpp
	Util/BlockAllocator.h
	Util/PPGeDraw.cpp
	Util/PPGeDraw.h
	Util/KL4E.cpp
	Util/KL4E.h
	Util/PSARUnpack.cpp
	Util/PSARUnpack.h
	Util/PkgUnpack.cpp
	Util/PkgUnpack.h
	${CMAKE_SOURCE_DIR}/ext/disarm.cpp
	${CMAKE_SOURCE_DIR}/ext/disarm.h
	${CMAKE_SOURCE_DIR}/ext/riscv-disas.cpp
	${CMAKE_SOURCE_DIR}/ext/riscv-disas.h
	${CMAKE_SOURCE_DIR}/ext/loongarch-disasm.cpp
	${CMAKE_SOURCE_DIR}/ext/loongarch-disasm.h
	${CMAKE_BINARY_DIR}/git-version.cpp
)


set(CoreExtraLibs)
if(ANDROID)
	list(APPEND CoreExtraLibs android)
	if(X86_64)
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-Bsymbolic")
	endif()
endif()

list(APPEND CoreExtraLibs armips)

target_link_libraries(Core GPU Common libretro-common kirk cityhash sfmt19937 xbrz xxhash at3_standalone lua
	aemu_postoffice_client ${CoreExtraLibs} ${CMAKE_DL_LIBS})

# Winsock
if(WIN32)
	target_link_libraries(Core ws2_32)
endif()

if(NOT HTTPS_NOT_AVAILABLE)
	target_link_libraries(Core naett)
endif()

target_compile_features(Core PUBLIC cxx_std_17)

if(USE_FFMPEG)
	target_compile_definitions(Core PRIVATE USE_FFMPEG=1)
	set_target_properties(Core PROPERTIES NO_SYSTEM_FROM_IMPORTED true)
	target_link_libraries(Core
		avcodec
		avformat
		avutil
		swresample
		swscale
	)
endif()

setup_target_project(Core Core)

# Generate git-version at build time.
add_custom_target(GitVersion DEPENDS something_that_never_exists)

add_custom_command(OUTPUT something_that_never_exists
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
		-DOUTPUT_DIR=${CMAKE_BINARY_DIR}
		-P ${CMAKE_SOURCE_DIR}/git-version.cmake)

set_source_files_properties(${CMAKE_BINARY_DIR}/git-version.cpp
	PROPERTIES GENERATED TRUE
	SKIP_AUTOMOC ON)
add_dependencies(Core GitVersion)

# No fused multiply-adds in CPU emulation unless written explicitly, so its float results are the
# same on every CPU. Elsewhere (the vertex decoder, say) they're fine. MSVC doesn't fuse by default.
if(NOT MSVC)
	get_target_property(CoreCPUSources Core SOURCES)
	list(FILTER CoreCPUSources INCLUDE REGEX "^MIPS/")
	set_source_files_properties(${CoreCPUSources} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
