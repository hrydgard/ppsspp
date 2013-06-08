set(SRCS
  Debugger/Breakpoints.cpp
  Debugger/SymbolMap.cpp
  Dialog/PSPDialog.cpp
  Dialog/PSPMsgDialog.cpp
  Dialog/PSPPlaceholderDialog.cpp
  Dialog/PSPSaveDialog.cpp
  Dialog/SavedataParam.cpp
  Dialog/PSPOskDialog.cpp
  MIPS/MIPS.cpp
  MIPS/MIPSAnalyst.cpp
  MIPS/MIPSCodeUtils.cpp
  MIPS/MIPSDebugInterface.cpp
  MIPS/MIPSDis.cpp
  MIPS/MIPSDisVFPU.cpp
  MIPS/MIPSInt.cpp
  MIPS/MIPSIntVFPU.cpp
  MIPS/MIPSTables.cpp
  MIPS/MIPSVFPUUtils.cpp
  MIPS/JitCommon/JitCommon.cpp
  ELF/ElfReader.cpp
  ELF/ParamSFO.cpp
  ELF/PrxDecrypter.cpp
  Font/PGF.cpp
  HLE/HLE.cpp
  HLE/HLETables.cpp
  HLE/sceAtrac.cpp
  HLE/__sceAudio.cpp
  HLE/sceAudio.cpp
  HLE/sceChnnlsv.cpp
  HLE/sceCtrl.cpp
  HLE/sceDeflt.cpp
  HLE/sceDisplay.cpp
  HLE/sceDmac.cpp
  HLE/sceGe.cpp
  HLE/sceFont.cpp
  HLE/sceHprm.cpp
  HLE/sceHttp.cpp
  HLE/sceImpose.cpp
  HLE/sceIo.cpp
  HLE/sceJpeg.cpp
  HLE/sceKernel.cpp
  HLE/sceKernelAlarm.cpp
  HLE/sceKernelEventFlag.cpp
  HLE/sceKernelInterrupt.cpp
  HLE/sceKernelMemory.cpp
  HLE/sceKernelModule.cpp
  HLE/sceKernelMutex.cpp
  HLE/sceKernelMbx.cpp
  HLE/sceKernelMsgPipe.cpp
  HLE/sceKernelSemaphore.cpp
  HLE/sceKernelThread.cpp
  HLE/sceKernelTime.cpp
  HLE/sceKernelVTimer.cpp
  HLE/sceMpeg.cpp
  HLE/sceNet.cpp
  HLE/sceNp.cpp
  HLE/sceP3da.cpp
  HLE/sceMd5.cpp
  HLE/sceMp4.cpp
  HLE/sceMp3.cpp
  HLE/scePauth.cpp
  HLE/scePower.cpp
  HLE/scePsmf.cpp
  HLE/sceRtc.cpp
  HLE/sceSas.cpp
  HLE/sceUmd.cpp
  HLE/sceUsb.cpp
  HLE/sceUtility.cpp
  HLE/sceParseUri.cpp
  HLE/sceSsl.cpp
  HLE/sceParseUri.cpp
  HLE/sceParseHttp.cpp
  HLE/sceVaudio.cpp
  HLE/sceAudiocodec.cpp
  HW/MemoryStick.cpp
  HW/MediaEngine.cpp
  HW/SasAudio.cpp
  FileSystems/BlockDevices.cpp
  FileSystems/ISOFileSystem.cpp
  FileSystems/DirectoryFileSystem.cpp
  FileSystems/MetaFileSystem.cpp
  Util/BlockAllocator.cpp
  Util/ppge_atlas.cpp
  Util/PPGeDraw.cpp
  CPU.cpp
  CoreTiming.cpp
  Config.cpp
  Loaders.cpp
  Host.cpp
  MemMap.cpp
  MemMapFunctions.cpp
  PSPLoaders.cpp
  PSPMixer.cpp
  System.cpp
  Core.cpp
  ../git-version.cpp
)

if(ARM)
	set(SRCS ${SRCS} MIPS/ARM/Asm.cpp
					 MIPS/ARM/CompALU.cpp
					 MIPS/ARM/CompBranch.cpp
					 MIPS/ARM/CompLoadStore.cpp
					 MIPS/ARM/CompFPU.cpp
					 MIPS/ARM/Jit.cpp
					 MIPS/ARM/JitCache.cpp
					 MIPS/ARM/RegCache.cpp
	)
else()
	set(SRCS ${SRCS} MIPS/x86/Asm.cpp
					 MIPS/x86/CompALU.cpp
					 MIPS/x86/CompBranch.cpp
					 MIPS/x86/CompLoadStore.cpp
					 MIPS/x86/CompFPU.cpp
					 MIPS/x86/Jit.cpp
					 MIPS/x86/JitCache.cpp
					 MIPS/x86/RegCache.cpp
	)
endif(ARM)

set(SRCS ${SRCS})

add_library(core STATIC ${SRCS})
target_link_libraries(core general common)
target_link_libraries(core general base)

# Generate git-version.cpp at build time.
add_custom_target(GitVersion ALL
	DEPENDS something_that_never_exists)
add_custom_command(OUTPUT something_that_never_exists
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
		-P ${CMAKE_CURRENT_SOURCE_DIR}/../git-version.cmake)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../git-version.cpp
	PROPERTIES GENERATED TRUE)
add_dependencies(core GitVersion)

if(UNIX)
  add_definitions(-fPIC)
  add_definitions(-std=gnu++0x)
endif(UNIX)
