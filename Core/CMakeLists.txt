set(CoreExtra)
set(aemu_postoffice
	${CMAKE_SOURCE_DIR}/ext/aemu_postoffice/client/postoffice.c
	${CMAKE_SOURCE_DIR}/ext/aemu_postoffice/client/postoffice_mem_stdc.c
	${CMAKE_SOURCE_DIR}/ext/aemu_postoffice/client/mutex_impl_cpp.cpp
	${CMAKE_SOURCE_DIR}/ext/aemu_postoffice/client/delay_impl_cpp.cpp
	${CMAKE_SOURCE_DIR}/ext/aemu_postoffice/client/log_impl_ppsspp.cpp
)

if(WIN32)
	list(APPEND aemu_postoffice
		${CMAKE_SOURCE_DIR}/ext/aemu_postoffice/client/sock_impl_windows.c
	)
endif()

if(LINUX OR ANDROID OR MACOSX OR IOS OR BSD)
	list(APPEND aemu_postoffice
		${CMAKE_SOURCE_DIR}/ext/aemu_postoffice/client/sock_impl_linux.c
	)
endif()

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
		AVIDump.cpp
		AVIDump.h
		WaveFile.cpp
		WaveFile.h
	)
endif()

if((MACOSX OR IOS) AND NOT LIBRETRO)
	list(APPEND CoreExtra
		Util/DarwinFileSystemServices.mm
		Util/DarwinFileSystemServices.h)
endif()

set(CmdLine)
if(NOT LIBRETRO)
	list(APPEND CmdLine
		CmdLine.cpp
		CmdLine.h
	)
endif()

# Files that libretro/Makefile.common doesn't build for the libretro core (no HTTP/WebSocket
# debugger, no standalone-app EmuThread/tilt/asm-debugger/game-download machinery there) - keep
# this in sync with Makefile.common so the CMake-based libretro build (used on Linux/Mac) matches
# what the Windows libretro build (driven by the Makefile) actually links.
set(CoreNonLibretro)
if(NOT LIBRETRO)
	list(APPEND CoreNonLibretro
		WebServer.cpp
		WebServer.h
		EmuThread.cpp
		EmuThread.h
		TiltEventProcessor.cpp
		TiltEventProcessor.h
		Debugger/DisassemblyManager.cpp
		Debugger/DisassemblyManager.h
		Debugger/WebSocket.cpp
		Debugger/WebSocket.h
		Debugger/WebSocket/BreakpointSubscriber.cpp
		Debugger/WebSocket/BreakpointSubscriber.h
		Debugger/WebSocket/CPUCoreSubscriber.cpp
		Debugger/WebSocket/CPUCoreSubscriber.h
		Debugger/WebSocket/DisasmSubscriber.cpp
		Debugger/WebSocket/DisasmSubscriber.h
		Debugger/WebSocket/GameBroadcaster.cpp
		Debugger/WebSocket/GameBroadcaster.h
		Debugger/WebSocket/GameSubscriber.cpp
		Debugger/WebSocket/GameSubscriber.h
		Debugger/WebSocket/ClientConfigSubscriber.cpp
		Debugger/WebSocket/ClientConfigSubscriber.h
		Debugger/WebSocket/GPUBufferSubscriber.cpp
		Debugger/WebSocket/GPUBufferSubscriber.h
		Debugger/WebSocket/GPUDisasmSubscriber.cpp
		Debugger/WebSocket/GPUDisasmSubscriber.h
		Debugger/WebSocket/GPURecordSubscriber.cpp
		Debugger/WebSocket/GPURecordSubscriber.h
		Debugger/WebSocket/GPUStatsSubscriber.cpp
		Debugger/WebSocket/GPUStatsSubscriber.h
		Debugger/WebSocket/HLEKernelObjectSubscriber.cpp
		Debugger/WebSocket/HLEKernelObjectSubscriber.h
		Debugger/WebSocket/HLESubscriber.cpp
		Debugger/WebSocket/HLESubscriber.h
		Debugger/WebSocket/InputBroadcaster.cpp
		Debugger/WebSocket/InputBroadcaster.h
		Debugger/WebSocket/InputSubscriber.cpp
		Debugger/WebSocket/InputSubscriber.h
		Debugger/WebSocket/LogBroadcaster.cpp
		Debugger/WebSocket/LogBroadcaster.h
		Debugger/WebSocket/LogConfigSubscriber.cpp
		Debugger/WebSocket/LogConfigSubscriber.h
		Debugger/WebSocket/MemoryInfoSubscriber.cpp
		Debugger/WebSocket/MemoryInfoSubscriber.h
		Debugger/WebSocket/MemorySubscriber.cpp
		Debugger/WebSocket/MemorySubscriber.h
		Debugger/WebSocket/ReplaySubscriber.cpp
		Debugger/WebSocket/ReplaySubscriber.h
		Debugger/WebSocket/SteppingBroadcaster.cpp
		Debugger/WebSocket/SteppingBroadcaster.h
		Debugger/WebSocket/SteppingSubscriber.cpp
		Debugger/WebSocket/SteppingSubscriber.h
		Debugger/WebSocket/WebSocketUtils.cpp
		Debugger/WebSocket/WebSocketUtils.h
		Dialog/PSPPlaceholderDialog.cpp
		Dialog/PSPPlaceholderDialog.h
		HW/GranularMixer.cpp
		HW/GranularMixer.h
		HW/StereoResampler.cpp
		HW/StereoResampler.h
		MIPS/MIPSAsm.cpp
		MIPS/MIPSAsm.h
		MIPS/fake/FakeJit.cpp
		MIPS/fake/FakeJit.h
		Util/GameManager.cpp
		Util/GameManager.h
		Util/VideoPlayer.cpp
		Util/VideoPlayer.h
	)
endif()

# miniUPnPc integration (MiniUPnPc supposed to works on any POSIX system, not sure if some of these are redundant/not needed tho)
#
# The add_compile_definitions()/target_link_libraries(Core ...) split below looks awkward, but it's
# required: add_compile_definitions() only affects targets created *after* the call, and
# target_link_libraries(Core ...) requires the Core target to already exist. So the defines have to
# land before add_library(Core STATIC ...) further up this file, while linking miniupnpc into Core
# has to happen after it. Previously this whole block sat after add_library(Core STATIC ...), which
# meant Core/Util/PortManager.cpp (built as part of that same add_library call) never actually saw
# WITH_UPNP or any of the other defines below - UPnP support was silently compiled out everywhere.
if(USE_MINIUPNPC)
	if(USE_SYSTEM_MINIUPNPC)
		find_package(MINIUPNPC REQUIRED)
		add_compile_definitions(WITH_UPNP)
	else()
		set (MINIUPNPC_VERSION 2.2) # used by miniupnpcstrings.h.cmake
		set (MINIUPNPC_API_VERSION 18)
		option(UPNPC_BUILD_STATIC "Build static library" TRUE)
		option(NO_GETADDRINFO "Define NO_GETADDRINFO" FALSE)
		mark_as_advanced(NO_GETADDRINFO)
		if (NO_GETADDRINFO)
			add_compile_definitions(NO_GETADDRINFO)
		endif()

		if (NOT WIN32)
			add_compile_definitions (MINIUPNPC_SET_SOCKET_TIMEOUT)
			add_compile_definitions (_BSD_SOURCE _DEFAULT_SOURCE _POSIX_C_SOURCE=200112L)
		endif()
		if (MACOSX)
			add_compile_definitions (_DARWIN_C_SOURCE)
		endif()
		if(WIN32)
			add_compile_definitions(WIN32 MINIUPNP_EXPORTS)
		else()
			add_compile_options(-fPIC)
		endif()

		add_compile_definitions(WITH_UPNP MINIUPNP_STATICLIB)
		set(MINIUPNP_DIR "${CMAKE_SOURCE_DIR}/ext/miniupnp/miniupnpc")
		include_directories(${CMAKE_CURRENT_BINARY_DIR})
		include_directories(${CMAKE_SOURCE_DIR}/ext/miniupnp/miniupnpc/src)
		include_directories(${CMAKE_SOURCE_DIR}/ext/miniupnp/miniupnpc/include)
		configure_file(${MINIUPNP_DIR}/miniupnpcstrings.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/miniupnpcstrings.h) # by default miniupnp repo doesn't contains miniupnpcstrings.h and need to be generated
		set(MINIUPNPC_SOURCES
				# the needed bits of miniupnpc (no python module, no tests, no cli)
			${MINIUPNP_DIR}/src/addr_is_reserved.c
			${MINIUPNP_DIR}/src/connecthostport.c
			${MINIUPNP_DIR}/src/igd_desc_parse.c
			${MINIUPNP_DIR}/src/minisoap.c
			${MINIUPNP_DIR}/src/minissdpc.c
			${MINIUPNP_DIR}/src/miniupnpc.c
				#${MINIUPNP_DIR}/miniupnpcmodule.c
			${MINIUPNP_DIR}/src/miniwget.c
			${MINIUPNP_DIR}/src/minixml.c
			${MINIUPNP_DIR}/src/portlistingparse.c
			${MINIUPNP_DIR}/src/receivedata.c
				#${MINIUPNP_DIR}/upnpc.c # causing an error due to already existing _main()
			${MINIUPNP_DIR}/src/upnpcommands.c
			${MINIUPNP_DIR}/src/upnpdev.c
			${MINIUPNP_DIR}/src/upnperrors.c
			${MINIUPNP_DIR}/src/upnpreplyparse.c
			${CMAKE_CURRENT_BINARY_DIR}/miniupnpcstrings.h
		)
		if (NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "AmigaOS")
			#set(MINIUPNPC_SOURCES ${MINIUPNPC_SOURCES} minissdpc.c) # causing an error due to duplication in MINIUPNPC_SOURCES?
		endif()
		if (WIN32)
			set_source_files_properties(${MINIUPNPC_SOURCES} PROPERTIES  COMPILE_DEFINITIONS "MINIUPNP_STATICLIB;MINIUPNP_EXPORTS")
			list(PREPEND LDLIBS ws2_32 iphlpapi)
			#elseif (CMAKE_SYSTEM_NAME STREQUAL "Solaris")
			#  find_library (SOCKET_LIBRARY NAMES socket)
			#  find_library (NSL_LIBRARY NAMES nsl)
			#  find_library (RESOLV_LIBRARY NAMES resolv)
			#  set (LDLIBS ${SOCKET_LIBRARY} ${NSL_LIBRARY} ${RESOLV_LIBRARY} ${LDLIBS})
		endif()
	endif()
endif()

add_library(Core STATIC
	${CoreExtra}
	${CmdLine}
	${CoreNonLibretro}
	Config.cpp
	Config.h
	ConfigSettings.cpp
	ConfigSettings.h
	ConfigValues.h
	ControlMapper.cpp
	ControlMapper.h
	Core.cpp
	Core.h
	Compatibility.cpp
	Compatibility.h
	CoreParameter.h
	CoreTiming.cpp
	CoreTiming.h
	CwCheat.cpp
	CwCheat.h
	FrameTiming.cpp
	FrameTiming.h
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
	RetroAchievements.h
	RetroAchievements.cpp
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
	Dialog/PSPNpSigninDialog.h
	Dialog/PSPOskDialog.cpp
	Dialog/PSPOskDialog.h
	Dialog/PSPOskConstants.cpp
	Dialog/PSPOskConstants.h
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
	HLE/sceMpeg.cpp
	HLE/sceMpeg.h
	HLE/sceNet.cpp
	HLE/sceNet.h
	HLE/sceNet_lib.cpp
	HLE/sceNet_lib.h
	HLE/NetAdhocCommon.cpp
	HLE/NetAdhocCommon.h
	HLE/sceNetAdhoc.cpp
	HLE/sceNetAdhoc.h
	${aemu_postoffice}
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
	MIPS/MIPSVFPUFallbacks.cpp
	MIPS/MIPSVFPUFallbacks.h
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
	Reporting.cpp
	Reporting.h
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
	Util/MemStick.cpp
	Util/MemStick.h
	Util/GameDB.cpp
	Util/GameDB.h
	Util/PathUtil.cpp
	Util/PathUtil.h
	Util/PortManager.cpp
	Util/PortManager.h
	Util/BlockAllocator.cpp
	Util/BlockAllocator.h
	Util/PPGeDraw.cpp
	Util/PPGeDraw.h
	Util/PSARUnpack.cpp
	Util/PSARUnpack.h
	Util/RecentFiles.cpp
	Util/RecentFiles.h
	${CMAKE_SOURCE_DIR}/ext/disarm.cpp
	${CMAKE_SOURCE_DIR}/ext/disarm.h
	${CMAKE_SOURCE_DIR}/ext/riscv-disas.cpp
	${CMAKE_SOURCE_DIR}/ext/riscv-disas.h
	${CMAKE_SOURCE_DIR}/ext/loongarch-disasm.cpp
	${CMAKE_SOURCE_DIR}/ext/loongarch-disasm.h
	${CMAKE_BINARY_DIR}/git-version.cpp
)

if((MACOSX OR IOS) AND NOT LIBRETRO)
	set_source_files_properties(Util/DarwinFileSystemServices.mm
		PROPERTIES COMPILE_FLAGS -fobjc-arc)
endif()

if(LIBRETRO)
	target_include_directories(Core PRIVATE ${CMAKE_SOURCE_DIR}/libretro/libretro-common/include)
endif()

set(CoreExtraLibs)
if(ANDROID)
	list(APPEND CoreExtraLibs android)
	if(X86_64)
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-Bsymbolic")
	endif()
endif()

list(APPEND CoreExtraLibs armips)

target_link_libraries(Core GPU Common chdr kirk cityhash sfmt19937 xbrz xxhash rcheevos minimp3 at3_standalone lua
	${CoreExtraLibs} ${CMAKE_DL_LIBS})

# Winsock
if(WIN32)
	target_link_libraries(Core ws2_32)
endif()

if(NOT HTTPS_NOT_AVAILABLE)
	target_link_libraries(Core naett)
endif()

target_compile_features(Core PUBLIC cxx_std_17)

if(FFmpeg_FOUND)
	target_compile_definitions(Core PRIVATE USE_FFMPEG=1)
	if (HAVE_LIBAVCODEC_CONST_AVCODEC)
		target_compile_definitions(Core PRIVATE HAVE_LIBAVCODEC_CONST_AVCODEC=1)
	endif()
	set_target_properties(Core PROPERTIES NO_SYSTEM_FROM_IMPORTED true)
	target_include_directories(Core BEFORE PUBLIC ${FFmpeg_INCLUDE_avcodec} ${FFmpeg_INCLUDE_avformat})
	target_link_libraries(Core
		FFmpeg::avcodec
		FFmpeg::avformat
		FFmpeg::avutil
		FFmpeg::swresample
		FFmpeg::swscale
		${ZLIB_LIBRARY}
	)
elseif(BUILD_BUNDLED_FFMPEG)
	target_compile_definitions(Core PRIVATE USE_FFMPEG=1)
	set_target_properties(Core PROPERTIES NO_SYSTEM_FROM_IMPORTED true)
	target_link_libraries(Core
		avcodec
		avformat
		avutil
		swresample
		swscale
		${ZLIB_LIBRARY}
	)
endif()

# Discord integration
if(USE_DISCORD AND NOT IOS AND NOT LIBRETRO)
	target_link_libraries(Core discord-rpc)
endif()

# The other half of the miniUPnPc integration above: that half sets up the compile definitions
# (which had to happen before add_library(Core STATIC ...)), this half builds/links the library
# (which needs the Core target to already exist).
if(USE_MINIUPNPC)
	if(USE_SYSTEM_MINIUPNPC)
		target_include_directories(Core PRIVATE ${MINIUPNP_INCLUDE_DIR})
		target_link_libraries(Core ${MINIUPNP_LIBRARY})
	elseif(UPNPC_BUILD_STATIC)
		add_library(miniupnpc STATIC ${MINIUPNPC_SOURCES})
		target_link_libraries(Core miniupnpc ${LDLIBS})
		set(UPNPC_LIBRARY miniupnpc)
		if (MSVC)
			# Suppress noise warnings
			target_compile_definitions(miniupnpc PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
		endif()
		if (WIN32)
			target_link_libraries(miniupnpc ws2_32)
		endif()
	endif()
endif()

setup_target_project(Core Core)

# Generate git-version at build time.
add_custom_target(GitVersion DEPENDS something_that_never_exists)

set(WIN_VERSION_CMD "")
if (WIN32)
	set(WIN_VERSION_CMD COMMAND ${CMAKE_SOURCE_DIR}/Windows/git-version-gen.cmd PPSSPPWindows)
endif()

add_custom_command(OUTPUT something_that_never_exists
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
		-DOUTPUT_DIR=${CMAKE_BINARY_DIR}
		-P ${CMAKE_SOURCE_DIR}/git-version.cmake
	${WIN_VERSION_CMD})

set_source_files_properties(${CMAKE_BINARY_DIR}/git-version.cpp
	PROPERTIES GENERATED TRUE
	SKIP_AUTOMOC ON)
add_dependencies(Core GitVersion)
