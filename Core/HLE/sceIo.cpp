// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <cstdlib>
#include <set>
#include <thread>
#include <memory>

#include "Common/Thread/ThreadUtil.h"
#include "Common/Profiler/Profiler.h"
#include "Common/TimeUtil.h"

#include "Common/File/FileUtil.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Serialize/SerializeSet.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMapHelpers.h"
#include "Core/System.h"
#include "Core/HDRemaster.h"
#include "Core/SaveState.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceUmd.h"
#include "Core/HW/Display.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HW/MemoryStick.h"
#include "Core/HW/AsyncIOManager.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"

#include "Core/FileSystems/FileSystem.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"

extern "C" {
#include "ext/libkirk/amctrl.h"
};

#include "Core/HLE/sceIo.h"
#include "Core/HLE/sceRtc.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/KernelWaitHelpers.h"

// For headless screenshots.
#include "Core/HLE/sceDisplay.h"
// For EMULATOR_DEVCTL__GET_SCALE
#include "System/Display.h"
// For EMULATOR_DEVCTL__GET_AXIS/VKEY
#include "Core/HLE/Plugins.h"
#include "Input/KeyCodes.h"

static const int ERROR_ERRNO_IO_ERROR                     = 0x80010005;
static const int ERROR_MEMSTICK_DEVCTL_BAD_PARAMS         = 0x80220081;
static const int ERROR_MEMSTICK_DEVCTL_TOO_MANY_CALLBACKS = 0x80220082;
static const int ERROR_PGD_INVALID_HEADER                 = 0x80510204;
/*

TODO: async io is missing features!

flash0: - fat access - system file volume
flash1: - fat access - configuration file volume
flashfat#: this too

lflash: - block access - entire flash
fatms: memstick
isofs: fat access - umd
disc0: fat access - umd
ms0: - fat access - memcard
umd: - block access - umd
irda?: - (?=0..9) block access - infra-red port (doesnt support seeking, maybe send/recieve data from port tho)
mscm0: - block access - memstick cm??
umd00: block access - umd
umd01: block access - umd
*/

#define PSP_O_RDONLY        0x0001
#define PSP_O_WRONLY        0x0002
#define PSP_O_RDWR          0x0003
#define PSP_O_NBLOCK        0x0010
#define PSP_O_APPEND        0x0100
#define PSP_O_CREAT         0x0200
#define PSP_O_TRUNC         0x0400
#define PSP_O_EXCL          0x0800
#define PSP_O_NOWAIT        0x8000
#define PSP_O_NPDRM         0x40000000

// chstat
#define SCE_CST_MODE    0x0001
#define SCE_CST_ATTR    0x0002
#define SCE_CST_SIZE    0x0004
#define SCE_CST_CT      0x0008
#define SCE_CST_AT      0x0010
#define SCE_CST_MT      0x0020
#define SCE_CST_PRVT    0x0040

typedef s32 SceMode;
typedef s64 SceOff;
typedef u64 SceIores;

const int PSP_COUNT_FDS = 64;
// TODO: stdin/stdout/stderr are special values aliased to 0?
const int PSP_MIN_FD = 3;
const int PSP_STDOUT = 1;
const int PSP_STDERR = 2;
const int PSP_STDIN = 0;
static int asyncNotifyEvent = -1;
static int syncNotifyEvent = -1;
static SceUID fds[PSP_COUNT_FDS];

static std::vector<SceUID> memStickCallbacks;
static std::vector<SceUID> memStickFatCallbacks;
static MemStickState lastMemStickState;
static MemStickFatState lastMemStickFatState;

static AsyncIOManager ioManager;
static bool ioManagerThreadEnabled = false;
static std::thread *ioManagerThread;

// TODO: Is it better to just put all on the thread?
// Let's try. (was 256)
const int IO_THREAD_MIN_DATA_SIZE = 0;

#define SCE_STM_FDIR 0x1000
#define SCE_STM_FREG 0x2000
#define SCE_STM_FLNK 0x4000

enum {
	TYPE_DIR  = 0x10,
	TYPE_FILE = 0x20
};

struct SceIoStat {
	SceMode_le st_mode;
	u32_le st_attr;
	SceOff_le st_size;
	ScePspDateTime st_c_time;
	ScePspDateTime st_a_time;
	ScePspDateTime st_m_time;
	u32_le st_private[6];
};

struct SceIoDirEnt {
	SceIoStat d_stat;
	char d_name[256];
	u32_le d_private;
};

enum class IoAsyncOp {
	NONE,
	OPEN,
	CLOSE,
	READ,
	WRITE,
	SEEK,
	IOCTL,
};

struct IoAsyncParams {
	IoAsyncOp op = IoAsyncOp::NONE;
	int priority = -1;
	union {
		struct {
			u32 filenameAddr;
			int flags;
			int mode;
		} open;
		struct {
			u32 addr;
			u32 size;
		} std;
		struct {
			s64 pos;
			int whence;
		} seek;
		struct {
			u32 cmd;
			u32 inAddr;
			u32 inSize;
			u32 outAddr;
			u32 outSize;
		} ioctl;
	};
};

static IoAsyncParams asyncParams[PSP_COUNT_FDS];
static HLEHelperThread *asyncThreads[PSP_COUNT_FDS]{};
static int asyncDefaultPriority = -1;

class FileNode : public KernelObject {
public:
	FileNode() {}
	~FileNode() {
		if (handle != -1)
			pspFileSystem.CloseFile(handle);
		pgd_close(pgdInfo);
	}
	const char *GetName() override { return fullpath.c_str(); }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "OpenFile"; }
	void GetQuickInfo(char *buf, int bufSize) override {
		snprintf(buf, bufSize, "Seekpos: %08x", (u32)pspFileSystem.GetSeekPos(handle));
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_File; }
	int GetIDType() const override { return PPSSPP_KERNEL_TMID_File; }

	bool asyncBusy() {
		return pendingAsyncResult || hasAsyncResult;
	}

	const PSPFileInfo &FileInfo() {
		if (!infoReady) {
			info = pspFileSystem.GetFileInfo(fullpath);
			if (!info.exists) {
				ERROR_LOG(Log::IO, "File %s no longer exists when reading info", fullpath.c_str());
			}
			infoReady = true;
		}
		return info;
	}

	void DoState(PointerWrap &p) override {
		auto s = p.Section("FileNode", 1, 3);
		if (!s)
			return;

		Do(p, fullpath);
		Do(p, handle);
		Do(p, callbackID);
		Do(p, callbackArg);
		Do(p, asyncResult);
		Do(p, hasAsyncResult);
		Do(p, pendingAsyncResult);
		Do(p, sectorBlockMode);
		Do(p, closePending);
		Do(p, info);
		Do(p, openMode);
		if (p.mode == p.MODE_READ) {
			infoReady = info.exists;
		}

		Do(p, npdrm);
		Do(p, pgd_offset);
		bool hasPGD = pgdInfo != NULL;
		Do(p, hasPGD);
		if (hasPGD) {
			if (p.mode == p.MODE_READ) {
				pgdInfo = (PGD_DESC*) malloc(sizeof(PGD_DESC));
			}
			if (pgdInfo)
				p.DoVoid(pgdInfo, sizeof(PGD_DESC));
			if (p.mode == p.MODE_READ && pgdInfo) {
				pgdInfo->block_buf = (u8 *)malloc(pgdInfo->block_size * 2);
			}
		}

		Do(p, waitingThreads);
		if (s >= 2) {
			Do(p, waitingSyncThreads);
		}
		if (s >= 3) {
			Do(p, isTTY);
		}
		Do(p, pausedWaits);
	}

	std::string fullpath;
	u32 handle;

	u32 callbackID = 0;
	u32 callbackArg = 0;

	s64 asyncResult = 0;
	bool hasAsyncResult = false;
	bool pendingAsyncResult = false;

	bool sectorBlockMode = false;
	// TODO: Use an enum instead?
	bool closePending = false;

	bool infoReady = false;
	PSPFileInfo info;
	u32 openMode = 0;

	u32 npdrm = 0;
	u32 pgd_offset = 0;
	PGD_DESC *pgdInfo = nullptr;

	std::vector<SceUID> waitingThreads;
	std::vector<SceUID> waitingSyncThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	// Value is actually meaningless but kept for consistency with other wait types.
	std::map<SceUID, u64> pausedWaits;

	bool isTTY = false;
};

/******************************************************************************/



/******************************************************************************/

u64 __IoCompleteAsyncIO(FileNode *f);

static void IoAsyncCleanupThread(int fd) {
	if (asyncThreads[fd]) {
		if (!asyncThreads[fd]->Stopped()) {
			asyncThreads[fd]->Terminate();
		}
		delete asyncThreads[fd];
		asyncThreads[fd] = nullptr;
	}
}

static int GetIOTimingMethod() {
	if (PSP_CoreParameter().compat.flags().ForceUMDDelay) {
		return IOTIMING_REALISTIC;
	} else {
		return g_Config.iIOTimingMethod;
	}
}

static void TellFsThreadEnded (SceUID threadID) {
	pspFileSystem.ThreadEnded(threadID);
}

static FileNode *__IoGetFd(int fd, u32 &error) {
	if (fd < 0 || fd >= PSP_COUNT_FDS) {
		error = SCE_KERNEL_ERROR_BADF;
		return NULL;
	}

	return kernelObjects.Get<FileNode>(fds[fd], error);
}

static int __IoAllocFd(FileNode *f) {
	// The PSP takes the lowest available id after stderr/etc.
	for (int possible = PSP_MIN_FD; possible < PSP_COUNT_FDS; ++possible) {
		if (fds[possible] == 0) {
			fds[possible] = f->GetUID();
			return possible;
		}
	}

	// Bugger, out of fds...
	return SCE_KERNEL_ERROR_MFILE;
}

static void __IoFreeFd(int fd, u32 &error) {
	if (fd == PSP_STDIN || fd == PSP_STDERR || fd == PSP_STDOUT) {
		error = SCE_KERNEL_ERROR_ILLEGAL_PERM;
	} else if (fd < PSP_MIN_FD || fd >= PSP_COUNT_FDS) {
		error = SCE_KERNEL_ERROR_BADF;
	} else {
		FileNode *f = __IoGetFd(fd, error);
		if (f) {
			// If there are pending results, don't allow closing.
			if (ioManager.HasOperation(f->handle)) {
				error = SCE_KERNEL_ERROR_ASYNC_BUSY;
				return;
			}

			// Wake anyone waiting on the file before closing it.
			for (size_t i = 0; i < f->waitingThreads.size(); ++i) {
				HLEKernel::ResumeFromWait(f->waitingThreads[i], WAITTYPE_ASYNCIO, f->GetUID(), (int)SCE_KERNEL_ERROR_WAIT_DELETE);
			}

			CoreTiming::UnscheduleEvent(asyncNotifyEvent, fd);
			for (size_t i = 0; i < f->waitingSyncThreads.size(); ++i) {
				CoreTiming::UnscheduleEvent(syncNotifyEvent, ((u64)f->waitingSyncThreads[i] << 32) | fd);
			}

			PROFILE_THIS_SCOPE("io_rw");

			// Discard any pending results.
			AsyncIOResult managerResult;
			ioManager.WaitResult(f->handle, managerResult);

			IoAsyncCleanupThread(fd);
		}
		error = kernelObjects.Destroy<FileNode>(fds[fd]);
		fds[fd] = 0;
	}
}

// Async IO seems to work roughly like this:
// 1. Game calls SceIo*Async() to start the process.
// 2. This runs a thread with a customizable priority.
// 3. The operation runs, which takes an inconsistent amount of time from UMD.
// 4. Once done (regardless of other syscalls), the fd-registered callback is notified.
// 5. The game can find out via *CB() or sceKernelCheckCallback().
// 6. At this point, the fd is STILL not usable.
// 7. One must call sceIoWaitAsync / sceIoWaitAsyncCB / sceIoPollAsync / possibly sceIoGetAsyncStat.
// 8. Finally, the fd is usable (or closed via sceIoCloseAsync.)  Presumably the io thread has joined now.

// TODO: Closed files are a bit special: until the fd is reused (?), the async result is still available.
// Clearly a buffer is used, it doesn't seem like they are actually kernel objects.

// For now, let's at least delay the callback notification.
static void __IoAsyncNotify(u64 userdata, int cyclesLate) {
	int fd = (int) userdata;

	u32 error;
	FileNode *f = __IoGetFd(fd, error);
	if (!f) {
		ERROR_LOG_REPORT(Log::sceIo, "__IoAsyncNotify: file no longer exists?");
		return;
	}

	int ioTimingMethod = GetIOTimingMethod();
	if (ioTimingMethod == IOTIMING_HOST) {
		// Not all async operations actually queue up.  Maybe should separate them?
		if (!ioManager.HasResult(f->handle) && ioManager.HasOperation(f->handle)) {
			// Try again in another 0.5ms until the IO completes on the host.
			CoreTiming::ScheduleEvent(usToCycles(500) - cyclesLate, asyncNotifyEvent, userdata);
			return;
		}
		__IoCompleteAsyncIO(f);
	} else if (ioTimingMethod == IOTIMING_REALISTIC) {
		u64 finishTicks = __IoCompleteAsyncIO(f);
		if (finishTicks > CoreTiming::GetTicks()) {
			// Reschedule for later, since we now know how long it ought to take.
			CoreTiming::ScheduleEvent(finishTicks - CoreTiming::GetTicks(), asyncNotifyEvent, userdata);
			return;
		}
	} else {
		__IoCompleteAsyncIO(f);
	}

	if (f->waitingThreads.empty()) {
		return;
	}

	SceUID threadID = f->waitingThreads.front();
	f->waitingThreads.erase(f->waitingThreads.begin());

	u32 address = __KernelGetWaitValue(threadID, error);
	if (HLEKernel::VerifyWait(threadID, WAITTYPE_ASYNCIO, f->GetUID())) {
		HLEKernel::ResumeFromWait(threadID, WAITTYPE_ASYNCIO, f->GetUID(), 0);
		// Someone woke up, so it's no longer got one.
		f->hasAsyncResult = false;

		if (Memory::IsValidAddress(address)) {
			Memory::Write_U64((u64) f->asyncResult, address);
		}

		// If this was a sceIoCloseAsync, we should close it at this point.
		if (f->closePending) {
			__IoFreeFd(fd, error);
		}
	}
}

static void __IoSyncNotify(u64 userdata, int cyclesLate) {
	PROFILE_THIS_SCOPE("io_rw");

	SceUID threadID = userdata >> 32;
	int fd = (int) (userdata & 0xFFFFFFFF);

	s64 result = -1;
	u32 error;
	FileNode *f = __IoGetFd(fd, error);
	if (!f) {
		ERROR_LOG_REPORT(Log::sceIo, "__IoSyncNotify: file no longer exists?");
		return;
	}

	int ioTimingMethod = GetIOTimingMethod();
	if (ioTimingMethod == IOTIMING_HOST) {
		if (!ioManager.HasResult(f->handle)) {
			// Try again in another 0.5ms until the IO completes on the host.
			CoreTiming::ScheduleEvent(usToCycles(500) - cyclesLate, syncNotifyEvent, userdata);
			return;
		}
	} else if (ioTimingMethod == IOTIMING_REALISTIC) {
		u64 finishTicks = ioManager.ResultFinishTicks(f->handle);
		if (finishTicks > CoreTiming::GetTicks()) {
			// Reschedule for later when the result should finish.
			CoreTiming::ScheduleEvent(finishTicks - CoreTiming::GetTicks(), syncNotifyEvent, userdata);
			return;
		}
	}

	f->pendingAsyncResult = false;
	f->hasAsyncResult = false;

	AsyncIOResult managerResult;
	if (ioManager.WaitResult(f->handle, managerResult)) {
		result = managerResult.result;
	} else {
		ERROR_LOG(Log::sceIo, "Unable to complete IO operation on %s", f->GetName());
	}

	f->pendingAsyncResult = false;
	f->hasAsyncResult = false;

	HLEKernel::ResumeFromWait(threadID, WAITTYPE_IO, fd, result);
	f->waitingSyncThreads.erase(std::remove(f->waitingSyncThreads.begin(), f->waitingSyncThreads.end(), threadID), f->waitingSyncThreads.end());
}

static void __IoAsyncBeginCallback(SceUID threadID, SceUID prevCallbackId) {
	auto result = HLEKernel::WaitBeginCallback<FileNode, WAITTYPE_ASYNCIO, SceUID>(threadID, prevCallbackId, -1);
	if (result == HLEKernel::WAIT_CB_SUCCESS) {
		DEBUG_LOG(Log::sceIo, "sceIoWaitAsync: Suspending wait for callback");
	} else if (result == HLEKernel::WAIT_CB_BAD_WAIT_ID) {
		WARN_LOG_REPORT(Log::sceIo, "sceIoWaitAsync: beginning callback with bad wait id?");
	}
}

static bool __IoCheckAsyncWait(FileNode *f, SceUID threadID, u32 &error, int result, bool &wokeThreads)
{
	int fd = -1;
	for (int i = 0; i < (int)ARRAY_SIZE(fds); ++i) {
		if (fds[i] == f->GetUID()) {
			fd = i;
			break;
		}
	}
	if (fd == -1) {
		ERROR_LOG_REPORT(Log::sceIo, "__IoCheckAsyncWait: could not find io handle");
		return true;
	}

	if (!HLEKernel::VerifyWait(threadID, WAITTYPE_ASYNCIO, f->GetUID())) {
		return true;
	}

	// If result is an error code, we're just letting it go.
	if (result == 0) {
		if (f->pendingAsyncResult || !f->hasAsyncResult) {
			return false;
		}

		u32 address = __KernelGetWaitValue(threadID, error);
		Memory::Write_U64((u64) f->asyncResult, address);
		f->hasAsyncResult = false;

		if (f->closePending) {
			__IoFreeFd(fd, error);
		}
	}

	__KernelResumeThreadFromWait(threadID, result);
	wokeThreads = true;
	return true;
}

static void __IoAsyncEndCallback(SceUID threadID, SceUID prevCallbackId) {
	auto result = HLEKernel::WaitEndCallback<FileNode, WAITTYPE_ASYNCIO, SceUID>(threadID, prevCallbackId, -1, __IoCheckAsyncWait);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT) {
		DEBUG_LOG(Log::sceIo, "sceKernelWaitEventFlagCB: Resuming lock wait for callback");
	}
}

static void __IoManagerThread() {
	SetCurrentThreadName("IO");
	AndroidJNIThreadContext jniContext;
	while (ioManagerThreadEnabled && coreState != CORE_BOOT_ERROR && coreState != CORE_RUNTIME_ERROR && coreState != CORE_POWERDOWN) {
		ioManager.RunEventsUntil(CoreTiming::GetTicks() + msToCycles(1000));
	}
}

static void __IoWakeManager(CoreLifecycle stage) {
	// Ping the thread so that it knows to check coreState.
	if (stage == CoreLifecycle::STOPPING) {
		ioManagerThreadEnabled = false;
		ioManager.FinishEventLoop();
	}
}

static void __IoVblank() {
	// We update memstick status here just to avoid possible thread safety issues.
	// It doesn't actually need to be on a vblank.

	// This will only change status if g_Config was changed.
	MemoryStick_SetState(g_Config.bMemStickInserted ? PSP_MEMORYSTICK_STATE_INSERTED : PSP_MEMORYSTICK_STATE_NOT_INSERTED);

	MemStickState newState = MemoryStick_State();
	MemStickFatState newFatState = MemoryStick_FatState();

	// First, the fat callbacks, these are easy.
	if (lastMemStickFatState != newFatState) {
		int notifyMsg = 0;
		if (newFatState == PSP_FAT_MEMORYSTICK_STATE_ASSIGNED) {
			notifyMsg = 1;
		} else if (newFatState == PSP_FAT_MEMORYSTICK_STATE_UNASSIGNED) {
			notifyMsg = 2;
		}
		if (notifyMsg != 0) {
			for (SceUID cbId : memStickFatCallbacks) {
				__KernelNotifyCallback(cbId, notifyMsg);
			}
		}
	}

	// Next, the controller notifies mounting (fat) too.
	if (lastMemStickState != newState || lastMemStickFatState != newFatState) {
		int notifyMsg = 0;
		if (newState == PSP_MEMORYSTICK_STATE_INSERTED && newFatState == PSP_FAT_MEMORYSTICK_STATE_ASSIGNED) {
			notifyMsg = 1;
		} else if (newState == PSP_MEMORYSTICK_STATE_INSERTED && newFatState == PSP_FAT_MEMORYSTICK_STATE_UNASSIGNED) {
			// Still mounting (1 will come later.)
			notifyMsg = 4;
		} else if (newState == PSP_MEMORYSTICK_STATE_NOT_INSERTED) {
			notifyMsg = 2;
		}
		if (notifyMsg != 0) {
			for (SceUID cbId : memStickCallbacks) {
				__KernelNotifyCallback(cbId, notifyMsg);
			}
		}
	}

	lastMemStickState = newState;
	lastMemStickFatState = newFatState;
}

void __IoInit() {
	asyncNotifyEvent = CoreTiming::RegisterEvent("IoAsyncNotify", __IoAsyncNotify);
	syncNotifyEvent = CoreTiming::RegisterEvent("IoSyncNotify", __IoSyncNotify);

	// TODO(scoped): This won't work if memStickDirectory points at the contents of /PSP...
#if defined(USING_WIN_UI) || defined(APPLE)
	auto flash0System = std::make_shared<DirectoryFileSystem>(&pspFileSystem, g_Config.flash0Directory, FileSystemFlags::FLASH);
#else
	auto flash0System = std::make_shared<VFSFileSystem>(&pspFileSystem, "flash0");
#endif
	FileSystemFlags memstickFlags = FileSystemFlags::SIMULATE_FAT32 | FileSystemFlags::CARD;

	Path pspDir = GetSysDirectory(DIRECTORY_PSP);
	if (pspDir == g_Config.memStickDirectory) {
		// Initially tried to do this with dual mounts, but failed due to save state compatibility issues.
		INFO_LOG(Log::sceIo, "Enabling /PSP compatibility mode");
		memstickFlags |= FileSystemFlags::STRIP_PSP;
	}

	auto memstickSystem = std::make_shared<DirectoryFileSystem>(&pspFileSystem, g_Config.memStickDirectory, memstickFlags);

	pspFileSystem.Mount("ms0:", memstickSystem);
	pspFileSystem.Mount("fatms0:", memstickSystem);
	pspFileSystem.Mount("fatms:", memstickSystem);
	pspFileSystem.Mount("pfat0:", memstickSystem);

	pspFileSystem.Mount("flash0:", flash0System);

	if (g_RemasterMode) {
		const std::string gameId = g_paramSFO.GetDiscID();
		const Path exdataPath = GetSysDirectory(DIRECTORY_EXDATA) / gameId;
		if (File::Exists(exdataPath)) {
			auto exdataSystem = std::make_shared<DirectoryFileSystem>(&pspFileSystem, exdataPath, FileSystemFlags::SIMULATE_FAT32 | FileSystemFlags::CARD);
			pspFileSystem.Mount("exdata0:", exdataSystem);
			INFO_LOG(Log::sceIo, "Mounted exdata/%s/ under memstick for exdata0:/", gameId.c_str());
		} else {
			INFO_LOG(Log::sceIo, "Did not find exdata/%s/ under memstick for exdata0:/", gameId.c_str());
		}
	}
	
	__KernelListenThreadEnd(&TellFsThreadEnded);

	memset(fds, 0, sizeof(fds));

	ioManagerThreadEnabled = true;
	ioManager.SetThreadEnabled(true);
	Core_ListenLifecycle(&__IoWakeManager);
	ioManagerThread = new std::thread(&__IoManagerThread);

	__KernelRegisterWaitTypeFuncs(WAITTYPE_ASYNCIO, __IoAsyncBeginCallback, __IoAsyncEndCallback);

	MemoryStick_Init();
	lastMemStickState = MemoryStick_State();
	lastMemStickFatState = MemoryStick_FatState();
	__DisplayListenVblank(__IoVblank);
}

void __IoDoState(PointerWrap &p) {
	auto s = p.Section("sceIo", 1, 5);
	if (!s)
		return;

	ioManager.DoState(p);
	DoArray(p, fds, ARRAY_SIZE(fds));
	Do(p, asyncNotifyEvent);
	CoreTiming::RestoreRegisterEvent(asyncNotifyEvent, "IoAsyncNotify", __IoAsyncNotify);
	Do(p, syncNotifyEvent);
	CoreTiming::RestoreRegisterEvent(syncNotifyEvent, "IoSyncNotify", __IoSyncNotify);
	if (s < 2) {
		std::set<SceUID> legacy;
		memStickCallbacks.clear();
		memStickFatCallbacks.clear();

		// Convert from set to vector.
		Do(p, legacy);
		for (SceUID id : legacy) {
			memStickCallbacks.push_back(id);
		}
		Do(p, legacy);
		for (SceUID id : legacy) {
			memStickFatCallbacks.push_back(id);
		}
	} else {
		Do(p, memStickCallbacks);
		Do(p, memStickFatCallbacks);
	}

	if (s >= 3) {
		Do(p, lastMemStickState);
		Do(p, lastMemStickFatState);
	}

	for (int i = 0; i < PSP_COUNT_FDS; ++i) {
		auto clearThread = [&]() {
			if (asyncThreads[i])
				asyncThreads[i]->Forget();
			delete asyncThreads[i];
			asyncThreads[i] = nullptr;
		};

		if (s >= 4) {
			p.DoVoid(&asyncParams[i], (int)sizeof(IoAsyncParams));
			bool hasThread = asyncThreads[i] != nullptr;
			Do(p, hasThread);
			if (hasThread) {
				if (p.GetMode() == p.MODE_READ)
					clearThread();
				DoClass(p, asyncThreads[i]);
			} else if (!hasThread) {
				clearThread();
			}
		} else {
			asyncParams[i].op = IoAsyncOp::NONE;
			asyncParams[i].priority = -1;
			clearThread();
		}
	}

	if (s >= 5) {
		Do(p, asyncDefaultPriority);
	} else {
		asyncDefaultPriority = -1;
	}
}

void __IoShutdown() {
	ioManagerThreadEnabled = false;
	ioManager.SyncThread();
	ioManager.FinishEventLoop();
	if (ioManagerThread != nullptr) {
		ioManagerThread->join();
		delete ioManagerThread;
		ioManagerThread = nullptr;
		ioManager.Shutdown();
	}

	for (int i = 0; i < PSP_COUNT_FDS; ++i) {
		asyncParams[i].op = IoAsyncOp::NONE;
		asyncParams[i].priority = -1;
		if (asyncThreads[i])
			asyncThreads[i]->Forget();
		delete asyncThreads[i];
		asyncThreads[i] = nullptr;
	}
	asyncDefaultPriority = -1;

	pspFileSystem.Unmount("ms0:");
	pspFileSystem.Unmount("fatms0:");
	pspFileSystem.Unmount("fatms:");
	pspFileSystem.Unmount("pfat0:");
	pspFileSystem.Unmount("flash0:");
	pspFileSystem.Unmount("exdata0:");

	MemoryStick_Shutdown();
	memStickCallbacks.clear();
	memStickFatCallbacks.clear();
}

static std::string IODetermineFilename(const FileNode *f) {
	uint64_t offset = pspFileSystem.GetSeekPos(f->handle);
	if ((pspFileSystem.DevType(f->handle) & PSPDevType::BLOCK) != 0) {
		return StringFromFormat("%s offset 0x%08llx", f->fullpath.c_str(), offset * 2048);
	}
	return StringFromFormat("%s offset 0x%08llx", f->fullpath.c_str(), offset);
}

u32 __IoGetFileHandleFromId(u32 id, u32 &outError)
{
	FileNode *f = __IoGetFd(id, outError);
	if (!f) {
		return (u32)-1;
	}
	return f->handle;
}

static void IoStartAsyncThread(int id, FileNode *f) {
	if (asyncThreads[id] && !asyncThreads[id]->Stopped()) {
		// Wake the thread up.
		if (asyncParams[id].priority == -1 && sceKernelGetCompiledSdkVersion() >= 0x04020000) {
			asyncThreads[id]->ChangePriority(KernelCurThreadPriority());
		}
		asyncThreads[id]->Resume(WAITTYPE_ASYNCIO, id, 0);
	} else {
		IoAsyncCleanupThread(id);
		int priority = asyncParams[id].priority;
		if (priority == -1)
			priority = KernelCurThreadPriority();
		asyncThreads[id] = new HLEHelperThread("SceIoAsync", "IoFileMgrForUser", "__IoAsyncFinish", priority, 0x200);
		asyncThreads[id]->Start(id, 0);
	}
	f->pendingAsyncResult = true;
}

static u32 sceIoAssign(u32 alias_addr, u32 physical_addr, u32 filesystem_addr, int mode, u32 arg_addr, int argSize)
{
	if (!Memory::IsValidNullTerminatedString(alias_addr) ||
		!Memory::IsValidNullTerminatedString(physical_addr) ||
		!Memory::IsValidNullTerminatedString(filesystem_addr)) {
		return hleLogError(Log::sceIo, -1, "Bad parameters");
	}

	std::string alias = Memory::GetCharPointer(alias_addr);
	std::string physical_dev = Memory::GetCharPointer(physical_addr);
	std::string filesystem_dev = Memory::GetCharPointer(filesystem_addr);
	std::string perm;

	switch (mode) {
		case 0:
			perm = "IOASSIGN_RDWR";
			break;
		case 1:
			perm = "IOASSIGN_RDONLY";
			break;
		default:
			perm = "unhandled";
			break;
	}
	WARN_LOG_REPORT(Log::sceIo, "sceIoAssign(%s, %s, %s, %s, %08x, %i)", alias.c_str(), physical_dev.c_str(), filesystem_dev.c_str(), perm.c_str(), arg_addr, argSize);
	return 0;
}

static u32 sceIoUnassign(const char *alias)
{
	WARN_LOG_REPORT(Log::sceIo, "sceIoUnassign(%s)", alias);
	return 0;
}

static u32 sceKernelStdin() {
	DEBUG_LOG(Log::sceIo, "%d=sceKernelStdin()", PSP_STDIN);
	return PSP_STDIN;
}

static u32 sceKernelStdout() {
	DEBUG_LOG(Log::sceIo, "%d=sceKernelStdout()", PSP_STDOUT);
	return PSP_STDOUT;
}

static u32 sceKernelStderr() {
	DEBUG_LOG(Log::sceIo, "%d=sceKernelStderr()", PSP_STDERR);
	return PSP_STDERR;
}

u64 __IoCompleteAsyncIO(FileNode *f) {
	PROFILE_THIS_SCOPE("io_rw");

	int ioTimingMethod = GetIOTimingMethod();
	if (ioTimingMethod == IOTIMING_REALISTIC) {
		u64 finishTicks = ioManager.ResultFinishTicks(f->handle);
		if (finishTicks > CoreTiming::GetTicks()) {
			return finishTicks;
		}
	}
	AsyncIOResult managerResult;
	if (ioManager.WaitResult(f->handle, managerResult)) {
		f->asyncResult = managerResult.result;
	} else {
		// It's okay, not all operations are deferred.
	}
	if (f->callbackID) {
		__KernelNotifyCallback(f->callbackID, f->callbackArg);
	}
	f->pendingAsyncResult = false;
	f->hasAsyncResult = true;

	return 0;
}

void __IoCopyDate(ScePspDateTime& date_out, const tm& date_in)
{
	date_out.year = date_in.tm_year+1900;
	date_out.month = date_in.tm_mon+1;
	date_out.day = date_in.tm_mday;
	date_out.hour = date_in.tm_hour;
	date_out.minute = date_in.tm_min;
	date_out.second = date_in.tm_sec;
	date_out.microsecond = 0;
}

static void __IoGetStat(SceIoStat *stat, PSPFileInfo &info) {
	memset(stat, 0xfe, sizeof(SceIoStat));

	int type, attr;
	if (info.type & FILETYPE_DIRECTORY) {
		type = SCE_STM_FDIR;
		attr = TYPE_DIR;
	} else {
		type = SCE_STM_FREG;
		attr = TYPE_FILE;
	}

	stat->st_mode = type | info.access;
	stat->st_attr = attr;
	stat->st_size = info.size;
	__IoCopyDate(stat->st_a_time, info.atime);
	__IoCopyDate(stat->st_c_time, info.ctime);
	__IoCopyDate(stat->st_m_time, info.mtime);
	stat->st_private[0] = info.startSector;
}

static void __IoSchedAsync(FileNode *f, int fd, int usec) {
	CoreTiming::ScheduleEvent(usToCycles(usec), asyncNotifyEvent, fd);

	f->pendingAsyncResult = true;
	f->hasAsyncResult = false;
}

static void __IoSchedSync(FileNode *f, int fd, int usec) {
	u64 param = ((u64)__KernelGetCurThread()) << 32 | fd;
	CoreTiming::ScheduleEvent(usToCycles(usec), syncNotifyEvent, param);

	f->pendingAsyncResult = false;
	f->hasAsyncResult = false;
}

static u32 sceIoGetstat(const char *filename, u32 addr) {
	// TODO: Improve timing (although this seems normally slow..)
	int usec = 1000;

	auto stat = PSPPointer<SceIoStat>::Create(addr);
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	if (info.exists) {
		if (stat.IsValid()) {
			__IoGetStat(stat, info);
			stat.NotifyWrite("IoGetstat");
			DEBUG_LOG(Log::sceIo, "sceIoGetstat(%s, %08x) : sector = %08x", filename, addr, info.startSector);
			return hleDelayResult(0, "io getstat", usec);
		} else {
			ERROR_LOG(Log::sceIo, "sceIoGetstat(%s, %08x) : bad address", filename, addr);
			return hleDelayResult(-1, "io getstat", usec);
		}
	} else {
		DEBUG_LOG(Log::sceIo, "sceIoGetstat(%s, %08x) : FILE NOT FOUND", filename, addr);
		return hleDelayResult(SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "io getstat", usec);
	}
}

static u32 sceIoChstat(const char *filename, u32 iostatptr, u32 changebits) {
	auto iostat = PSPPointer<SceIoStat>::Create(iostatptr);
	if (!iostat.IsValid())
		return hleReportError(Log::sceIo, SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT, "bad address");

	ERROR_LOG_REPORT(Log::sceIo, "UNIMPL sceIoChstat(%s, %08x, %08x)", filename, iostatptr, changebits);
	if (changebits & SCE_CST_MODE)
		ERROR_LOG_REPORT(Log::sceIo, "sceIoChstat: change mode to %03o requested", iostat->st_mode);
	if (changebits & SCE_CST_ATTR)
		ERROR_LOG_REPORT(Log::sceIo, "sceIoChstat: change attr to %04x requested", iostat->st_attr);
	if (changebits & SCE_CST_SIZE)
		ERROR_LOG(Log::sceIo, "sceIoChstat: change size requested");
	if (changebits & SCE_CST_CT)
		ERROR_LOG(Log::sceIo, "sceIoChstat: change creation time requested");
	if (changebits & SCE_CST_AT)
		ERROR_LOG(Log::sceIo, "sceIoChstat: change access time requested");
	if (changebits & SCE_CST_MT)
		ERROR_LOG_REPORT(Log::sceIo, "sceIoChstat: change modification time to %04d-%02d-%02d requested", iostat->st_m_time.year, iostat->st_m_time.month, iostat->st_m_time.day);
	if (changebits & SCE_CST_PRVT)
		ERROR_LOG(Log::sceIo, "sceIoChstat: change private data requested");
	return 0;
}

static u32 npdrmRead(FileNode *f, u8 *data, int size) {
	PGD_DESC *pgd = f->pgdInfo;
	u32 block, offset, blockPos;
	u32 remain_size, copy_size;

	block  = pgd->file_offset/pgd->block_size;
	offset = pgd->file_offset%pgd->block_size;

	if (size > (int)pgd->data_size)
		size = (int)pgd->data_size;
	remain_size = size;

	while(remain_size){
	
		if(pgd->current_block!=block){
			blockPos = block*pgd->block_size;
			pspFileSystem.SeekFile(f->handle, (s32)pgd->data_offset+blockPos, FILEMOVE_BEGIN);
			pspFileSystem.ReadFile(f->handle, pgd->block_buf, pgd->block_size);
			pgd_decrypt_block(pgd, block);
			pgd->current_block = block;
		}

		if(offset+remain_size>pgd->block_size){
			copy_size = pgd->block_size-offset;
			memcpy(data, pgd->block_buf+offset, copy_size);
			block += 1;
			offset = 0;
		} else {
			copy_size = remain_size;
			memcpy(data, pgd->block_buf+offset, copy_size);
		}

		data += copy_size;
		remain_size -= copy_size;
		pgd->file_offset += copy_size;
	}

	return size;
}

static bool __IoRead(int &result, int id, u32 data_addr, int size, int &us) {
	PROFILE_THIS_SCOPE("io_rw");
	// Low estimate, may be improved later from the ReadFile result.

	if (PSP_CoreParameter().compat.flags().ForceUMDReadSpeed || g_Config.iIOTimingMethod == IOTIMING_UMDSLOWREALISTIC) {
		us = size / 4.2;
	}
	else {
		us = size / 100;
	}

	if (us < 100) {
		us = 100;
	}

	if (id == PSP_STDIN) {
		DEBUG_LOG(Log::sceIo, "sceIoRead STDIN");
		result = 0; //stdin
		return true;
	}

	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			result = SCE_KERNEL_ERROR_ASYNC_BUSY;
			return true;
		}
		if (!(f->openMode & FILEACCESS_READ)) {
			result = SCE_KERNEL_ERROR_BADF;
			return true;
		} else if (size < 0) {
			result = SCE_KERNEL_ERROR_ILLEGAL_ADDR;
			return true;
		} else if (Memory::IsValidAddress(data_addr)) {
			const std::string tag = "IoRead/" + IODetermineFilename(f);
			NotifyMemInfo(MemBlockFlags::WRITE, data_addr, size, tag.c_str(), tag.size());
			u8 *data = (u8 *)Memory::GetPointerUnchecked(data_addr);
			u32 validSize = Memory::ValidSize(data_addr, size);
			if (f->npdrm) {
				result = npdrmRead(f, data, validSize);
				currentMIPS->InvalidateICache(data_addr, validSize);
				return true;
			}

			bool useThread = __KernelIsDispatchEnabled() && ioManagerThreadEnabled && size > IO_THREAD_MIN_DATA_SIZE;
			if (useThread) {
				// If there's a pending operation on this file, wait for it to finish and don't overwrite it.
				useThread = !ioManager.HasOperation(f->handle);
				if (!useThread) {
					ioManager.SyncThread();
				}
			}
			if (useThread) {
				AsyncIOEvent ev = IO_EVENT_READ;
				ev.handle = f->handle;
				ev.buf = data;
				ev.bytes = validSize;
				ev.invalidateAddr = data_addr;
				ioManager.ScheduleOperation(ev);
				return false;
			} else {
				if (GetIOTimingMethod() != IOTIMING_REALISTIC) {
					result = (int)pspFileSystem.ReadFile(f->handle, data, validSize);
				} else {
					result = (int)pspFileSystem.ReadFile(f->handle, data, validSize, us);
				}
				currentMIPS->InvalidateICache(data_addr, validSize);
				return true;
			}
		} else {
			if (size != 0) {
				// TODO: For some combinations of bad pointer + size, SCE_KERNEL_ERROR_ILLEGAL_ADDR.
				// Seems like only for kernel RAM.  For most cases, it really is -1.
				result = -1;
			} else {
				result = 0;
			}
			return true;
		}
	} else {
		result = error;
		return true;
	}
}

static u32 sceIoRead(int id, u32 data_addr, int size) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (id > 2 && f != NULL) {
		if (!__KernelIsDispatchEnabled()) {
			DEBUG_LOG(Log::sceIo, "sceIoRead(%d, %08x, %x): dispatch disabled", id, data_addr, size);
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt()) {
			DEBUG_LOG(Log::sceIo, "sceIoRead(%d, %08x, %x): inside interrupt", id, data_addr, size);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
	}

	int result;
	int us;
	bool complete = __IoRead(result, id, data_addr, size, us);
	if (!complete) {
		DEBUG_LOG(Log::sceIo, "sceIoRead(%d, %08x, %x): deferring result", id, data_addr, size);

		__IoSchedSync(f, id, us);
		__KernelWaitCurThread(WAITTYPE_IO, id, 0, 0, false, "io read");
		f->waitingSyncThreads.push_back(__KernelGetCurThread());
		return 0;
	} else if (result >= 0) {
		DEBUG_LOG(Log::sceIo, "%x=sceIoRead(%d, %08x, %x)", result, id, data_addr, size);
		return hleDelayResult(result, "io read", us);
	} else {
		WARN_LOG(Log::sceIo, "sceIoRead(%d, %08x, %x): error %08x", id, data_addr, size, result);
		return result;
	}
}

static u32 sceIoReadAsync(int id, u32 data_addr, int size) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_ASYNC_BUSY, "async busy");
		}

		auto &params = asyncParams[id];
		params.op = IoAsyncOp::READ;
		params.std.addr = data_addr;
		params.std.size = size;
		IoStartAsyncThread(id, f);
		return hleLogSuccessI(Log::sceIo, 0);
	} else {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}
}

static bool __IoWrite(int &result, int id, u32 data_addr, int size, int &us) {
	PROFILE_THIS_SCOPE("io_rw");
	// Low estimate, may be improved later from the WriteFile result.
	us = size / 100;
	if (us < 100) {
		us = 100;
	}

	const void *data_ptr = Memory::GetPointer(data_addr);
	const u32 validSize = Memory::ValidSize(data_addr, size);
	// Let's handle stdout/stderr specially.
	if (id == PSP_STDOUT || id == PSP_STDERR) {
		const char *str = (const char *) data_ptr;
		const int str_size = size <= 0 ? 0 : (str[validSize - 1] == '\n' ? validSize - 1 : validSize);
		INFO_LOG(Log::Printf, "%s: %.*s", id == 1 ? "stdout" : "stderr", str_size, str);
		result = validSize;
		return true;
	}
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			result = SCE_KERNEL_ERROR_ASYNC_BUSY;
			return true;
		}
		if (!(f->openMode & FILEACCESS_WRITE)) {
			result = SCE_KERNEL_ERROR_BADF;
			return true;
		}
		if (size < 0) {
			result = SCE_KERNEL_ERROR_ILLEGAL_ADDR;
			return true;
		}

		const std::string tag = "IoWrite/" + IODetermineFilename(f);
		NotifyMemInfo(MemBlockFlags::READ, data_addr, size, tag.c_str(), tag.size());

		if (f->isTTY) {
			const char *str = (const char *)data_ptr;
			const int str_size = size <= 0 ? 0 : (str[validSize - 1] == '\n' ? validSize - 1 : validSize);
			INFO_LOG(Log::Printf, "%s: %.*s", "tty", str_size, str);
			result = validSize;
			return true;
		}

		bool useThread = __KernelIsDispatchEnabled() && ioManagerThreadEnabled && size > IO_THREAD_MIN_DATA_SIZE;
		if (useThread) {
			// If there's a pending operation on this file, wait for it to finish and don't overwrite it.
			useThread = !ioManager.HasOperation(f->handle);
			if (!useThread) {
				ioManager.SyncThread();
			}
		}
		if (useThread) {
			AsyncIOEvent ev = IO_EVENT_WRITE;
			ev.handle = f->handle;
			ev.buf = (u8 *) data_ptr;
			ev.bytes = validSize;
			ev.invalidateAddr = 0;
			ioManager.ScheduleOperation(ev);
			return false;
		} else {
			if (GetIOTimingMethod() != IOTIMING_REALISTIC) {
				result = (int)pspFileSystem.WriteFile(f->handle, (u8 *) data_ptr, validSize);
			} else {
				result = (int)pspFileSystem.WriteFile(f->handle, (u8 *) data_ptr, validSize, us);
			}
		}
		return true;
	} else {
		ERROR_LOG(Log::sceIo, "sceIoWrite ERROR: no file open");
		result = (s32) error;
		return true;
	}
}

static u32 sceIoWrite(int id, u32 data_addr, int size) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (id > 2 && f != NULL) {
		if (!__KernelIsDispatchEnabled()) {
			DEBUG_LOG(Log::sceIo, "sceIoWrite(%d, %08x, %x): dispatch disabled", id, data_addr, size);
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt()) {
			DEBUG_LOG(Log::sceIo, "sceIoWrite(%d, %08x, %x): inside interrupt", id, data_addr, size);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
	}

	int result;
	int us;
	bool complete = __IoWrite(result, id, data_addr, size, us);
	if (!complete) {
		DEBUG_LOG(Log::sceIo, "sceIoWrite(%d, %08x, %x): deferring result", id, data_addr, size);

		__IoSchedSync(f, id, us);
		__KernelWaitCurThread(WAITTYPE_IO, id, 0, 0, false, "io write");
		f->waitingSyncThreads.push_back(__KernelGetCurThread());
		return 0;
	} else if (result >= 0) {
		DEBUG_LOG(Log::sceIo, "%x=sceIoWrite(%d, %08x, %x)", result, id, data_addr, size);
		if (__KernelIsDispatchEnabled()) {
			// If we wrote to stdout, return an error (even though we did log it) rather than delaying.
			// On actual hardware, it would just return this... we just want the log output.
			if (__IsInInterrupt()) {
				return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
			}
			return hleDelayResult(result, "io write", us);
		} else {
			return result;
		}
	} else {
		WARN_LOG(Log::sceIo, "sceIoWrite(%d, %08x, %x): error %08x", id, data_addr, size, result);
		return result;
	}
}

static u32 sceIoWriteAsync(int id, u32 data_addr, int size) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_ASYNC_BUSY, "async busy");
		}

		auto &params = asyncParams[id];
		params.op = IoAsyncOp::WRITE;
		params.std.addr = data_addr;
		params.std.size = size;
		IoStartAsyncThread(id, f);
		return hleLogSuccessI(Log::sceIo, 0);
	} else {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}
}

static u32 sceIoGetDevType(int id) {
	if (id == PSP_STDOUT || id == PSP_STDERR || id == PSP_STDIN) {
		DEBUG_LOG(Log::sceIo, "sceIoGetDevType(%d)", id);
		return (u32)PSPDevType::FILE;
	}

	u32 error;
	FileNode *f = __IoGetFd(id, error);
	int result;
	if (f) {
		// TODO: When would this return PSP_DEV_TYPE_ALIAS?
		WARN_LOG(Log::sceIo, "sceIoGetDevType(%d - %s)", id, f->fullpath.c_str());
		if (f->isTTY)
			result = (u32)PSPDevType::FILE;
		else
			result = (u32)pspFileSystem.DevType(f->handle) & (u32)PSPDevType::EMU_MASK;
	} else {
		ERROR_LOG(Log::sceIo, "sceIoGetDevType: unknown id %d", id);
		result = SCE_KERNEL_ERROR_BADF;
	}

	return result;
}

static u32 sceIoCancel(int id)
{
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		// It seems like this is unsupported for UMDs and memory sticks, based on tests.
		return hleReportError(Log::sceIo, SCE_KERNEL_ERROR_UNSUP, "unimplemented or unsupported");
	} else {
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_BADF, "invalid fd");
	}
}

static u32 npdrmLseek(FileNode *f, s32 where, FileMove whence)
{
	u32 newPos, blockPos;

	if(whence==FILEMOVE_BEGIN){
		newPos = where;
	}else if(whence==FILEMOVE_CURRENT){
		newPos = f->pgdInfo->file_offset+where;
	}else{
		newPos = f->pgdInfo->data_size+where;
	}

	if (newPos > f->pgdInfo->data_size)
		return -EINVAL;

	f->pgdInfo->file_offset = newPos;
	blockPos = newPos&~(f->pgdInfo->block_size-1);
	pspFileSystem.SeekFile(f->handle, (s32)f->pgdInfo->data_offset+blockPos, whence);

	return newPos;
}

static s64 __IoLseekDest(FileNode *f, s64 offset, int whence, FileMove &seek) {
	PROFILE_THIS_SCOPE("io_rw");
	seek = FILEMOVE_BEGIN;

	// Let's make sure this isn't incorrect mid-operation.
	if (ioManager.HasOperation(f->handle)) {
		ioManager.SyncThread();
	}

	s64 newPos = 0;
	switch (whence) {
	case 0:
		newPos = offset;
		break;
	case 1:
		newPos = pspFileSystem.GetSeekPos(f->handle) + offset;
		seek = FILEMOVE_CURRENT;
		break;
	case 2:
		newPos = f->FileInfo().size + offset;
		seek = FILEMOVE_END;
		break;
	default:
		return (s32)SCE_KERNEL_ERROR_INVAL;
	}

	// Yes, -1 is the correct return code for this case.
	if (newPos < 0)
		return -1;
	return newPos;
}

static s64 __IoLseek(SceUID id, s64 offset, int whence) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			WARN_LOG(Log::sceIo, "sceIoLseek*(%d, %llx, %i): async busy", id, offset, whence);
			return SCE_KERNEL_ERROR_ASYNC_BUSY;
		}
		FileMove seek;
		s64 newPos = __IoLseekDest(f, offset, whence, seek);

		if(f->npdrm)
			return npdrmLseek(f, (s32)offset, seek);

		if (newPos < 0)
			return newPos;
		return pspFileSystem.SeekFile(f->handle, (s32) offset, seek);
	} else {
		return (s32) error;
	}
}

static s64 sceIoLseek(int id, s64 offset, int whence) {
	s64 result = __IoLseek(id, offset, whence);
	if (result >= 0 || result == -1) {
		DEBUG_LOG(Log::sceIo, "%lli = sceIoLseek(%d, %llx, %i)", result, id, offset, whence);
		// Educated guess at timing.
		hleEatCycles(1400);
		hleReSchedule("io seek");
		return result;
	} else {
		return hleLogError(Log::sceIo, result, "bad file descriptor");
	}
}

static u32 sceIoLseek32(int id, int offset, int whence) {
	s32 result = (s32) __IoLseek(id, offset, whence);
	if (result >= 0 || result == -1) {
		DEBUG_LOG(Log::sceIo, "%i = sceIoLseek32(%d, %x, %i)", result, id, offset, whence);
		// Educated guess at timing.
		hleEatCycles(1400);
		hleReSchedule("io seek");
		return result;
	} else {
		return hleLogError(Log::sceIo, result, "bad file descriptor");
	}
}

static u32 sceIoLseekAsync(int id, s64 offset, int whence) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (whence < 0 || whence > 2) {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_INVAL, "invalid whence");
		}
		if (f->asyncBusy()) {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_ASYNC_BUSY, "async busy");
		}

		auto &params = asyncParams[id];
		params.op = IoAsyncOp::SEEK;
		params.seek.pos = offset;
		params.seek.whence = whence;
		IoStartAsyncThread(id, f);
		return hleLogSuccessI(Log::sceIo, 0);
	} else {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}
	return 0;
}

static u32 sceIoLseek32Async(int id, int offset, int whence) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (whence < 0 || whence > 2) {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_INVAL, "invalid whence");
		}
		if (f->asyncBusy()) {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_ASYNC_BUSY, "async busy");
		}

		auto &params = asyncParams[id];
		params.op = IoAsyncOp::SEEK;
		params.seek.pos = offset;
		params.seek.whence = whence;
		IoStartAsyncThread(id, f);
		return hleLogSuccessI(Log::sceIo, 0);
	} else {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}
	return 0;
}

static FileNode *__IoOpen(int &error, const char *filename, int flags, int mode) {
	if (!filename) {
		// To prevent crashes. Not sure about the correct value.
		error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
		return nullptr;
	}

	int access = FILEACCESS_NONE;
	if (flags & PSP_O_RDONLY)
		access |= FILEACCESS_READ;
	if (flags & PSP_O_WRONLY)
		access |= FILEACCESS_WRITE;
	if (flags & PSP_O_APPEND)
		access |= FILEACCESS_APPEND;
	if (flags & PSP_O_CREAT)
		access |= FILEACCESS_CREATE;
	if (flags & PSP_O_TRUNC)
		access |= FILEACCESS_TRUNCATE;
	if (flags & PSP_O_EXCL)
		access |= FILEACCESS_EXCL;

	PSPFileInfo info;
	int h = -1;
	bool isTTY = false;
	// TODO: Technically, tty1, etc. too and space doesn't matter.
	if (startsWithNoCase(filename, "tty0:")) {
		info.name = filename;
		info.access = 0777;
		info.exists = true;

		isTTY = true;
	} else {
		h = pspFileSystem.OpenFile(filename, (FileAccess)access);
		if (h < 0) {
			error = h;
			return nullptr;
		}
	}
	error = 0;

	FileNode *f = new FileNode();
	kernelObjects.Create(f);
	f->handle = h;
	f->fullpath = filename;
	f->asyncResult = h;
	if (isTTY) {
		f->info = info;
		f->infoReady = true;
	}
	f->openMode = access;
	f->isTTY = isTTY;

	f->npdrm = (flags & PSP_O_NPDRM)? true: false;
	f->pgd_offset = 0;

	return f;
}

static u32 sceIoOpen(const char *filename, int flags, int mode) {
	hleEatCycles(18000);

	if (!filename) {
		// Not tested on the PSP.
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "nullptr file not found");
	}

	if (!__KernelIsDispatchEnabled()) {
		hleEatCycles(48000);
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	}

	int error;
	FileNode *f = __IoOpen(error, filename, flags, mode);
	if (!f) {
		_assert_(error != 0);
		if (error == (int)SCE_KERNEL_ERROR_NOCWD) {
			// TODO: Timing is not accurate.
			return hleLogError(Log::sceIo, hleDelayResult(error, "file opened", 10000), "no current working directory");
		} else if (error == (int)SCE_KERNEL_ERROR_NODEV) {
			return hleLogError(Log::sceIo, error, "device not found");
		} else if (error == (int)SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND) {
			// UMD: Varies between 5-10ms, could take longer if disc spins up.
			//      TODO: Bad filename at root (disc0:/no.exist) should take ~200us.
			// Card: Path depth matters, but typically between 10-13ms on a standard Pro Duo.
			// TODO: If a UMD and spun down, this can easily take 1s+.
			int delay = pspFileSystem.FlagsFromFilename(filename) & FileSystemFlags::UMD ? 6000 : 10000;
			return hleLogWarning(Log::sceIo, hleDelayResult(error, "file opened", delay), "file not found");
		} else {
			return hleLogError(Log::sceIo, hleDelayResult(error, "file opened", 10000));
		}
	}

	int id = __IoAllocFd(f);
	if (id < 0) {
		kernelObjects.Destroy<FileNode>(f->GetUID());
		return hleLogError(Log::sceIo, hleDelayResult(id, "file opened", 1000), "out of fds");
	} else {
		asyncParams[id].priority = asyncDefaultPriority;
		IFileSystem *sys = pspFileSystem.GetSystemFromFilename(filename);
		if (sys && !f->isTTY && (sys->DevType(f->handle) & (PSPDevType::BLOCK | PSPDevType::EMU_LBN))) {
			// These are fast to open, no delay or even rescheduling happens.
			return hleLogSuccessI(Log::sceIo, id);
		}
		// UMD: Speed varies from 1-6ms.
		// Card: Path depth matters, but typically between 10-13ms on a standard Pro Duo.
		int delay = pspFileSystem.FlagsFromFilename(filename) & FileSystemFlags::UMD ? 4000 : 10000;
		return hleLogSuccessI(Log::sceIo, hleDelayResult(id, "file opened", delay));
	}
}

static u32 sceIoClose(int id) {
	u32 error;
	DEBUG_LOG(Log::sceIo, "sceIoClose(%d)", id);
	__IoFreeFd(id, error);
	// Timing is not accurate, aiming low for now.
	return hleDelayResult(error, "file closed", 100);
}

static u32 sceIoRemove(const char *filename) {
	DEBUG_LOG(Log::sceIo, "sceIoRemove(%s)", filename);

	// TODO: This timing isn't necessarily accurate, low end for now.
	if(!pspFileSystem.GetFileInfo(filename).exists)
		return hleDelayResult(SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "file removed", 100);

	pspFileSystem.RemoveFile(filename);
	return hleDelayResult(0, "file removed", 100);
}

static u32 sceIoMkdir(const char *dirname, int mode) {
	DEBUG_LOG(Log::sceIo, "sceIoMkdir(%s, %i)", dirname, mode);
	// TODO: Improve timing.
	if (pspFileSystem.MkDir(dirname))
		return hleDelayResult(0, "mkdir", 1000);
	else
		return hleDelayResult(SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS, "mkdir", 1000);
}

static u32 sceIoRmdir(const char *dirname) {
	DEBUG_LOG(Log::sceIo, "sceIoRmdir(%s)", dirname);
	// TODO: Improve timing.
	if (pspFileSystem.RmDir(dirname))
		return hleDelayResult(0, "rmdir", 1000);
	else
		return hleDelayResult(SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "rmdir", 1000);
}

static u32 sceIoSync(const char *devicename, int flag) {
	DEBUG_LOG(Log::sceIo, "UNIMPL sceIoSync(%s, %i)", devicename, flag);
	return 0;
}

struct DeviceSize {
	u32_le maxClusters;
	u32_le freeClusters;
	u32_le maxSectors;
	u32_le sectorSize;
	u32_le sectorCount;
};

static u32 sceIoDevctl(const char *name, int cmd, u32 argAddr, int argLen, u32 outPtr, int outLen) {
	if (strcmp(name, "emulator:")) {
		DEBUG_LOG(Log::sceIo,"sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd, argAddr, argLen, outPtr, outLen);
	}

	// UMD checks
	switch (cmd) {
	case 0x01F20001:  
		// Get UMD disc type
		if (Memory::IsValidAddress(outPtr) && outLen >= 8) {
			Memory::Write_U32(0x10, outPtr + 4);  // Always return game disc (if present)
			return 0;
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F20002:  
		// Get UMD current LBA
		if (Memory::IsValidAddress(outPtr) && outLen >= 4) {
			Memory::Write_U32(0x10, outPtr);  // Assume first sector
			return 0;
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F20003:
		if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
			PSPFileInfo info = pspFileSystem.GetFileInfo("umd1:");
			Memory::Write_U32((u32) (info.size) - 1, outPtr);
			return 0;
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F100A3:  
		// Seek UMD disc (raw)
		if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
			return hleDelayResult(0, "dev seek", 100);
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F100A4:  
		// Prepare UMD data into cache.
		if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
			return 0;
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F300A5:  
		// Prepare UMD data into cache and get status
		if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
			Memory::Write_U32(1, outPtr); // Status (unitary index of the requested read, greater or equal to 1)
			return 0;
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F300A7:
		// Wait for the UMD data cache thread
		if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
			// TODO : 
			// Place the calling thread in wait state
			return 0;
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F300A8:
		// Poll the UMD data cache thread
		if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
			// 0 - UMD data cache thread has finished
			// 0x10 - UMD data cache thread is waiting
			// 0x20 - UMD data cache thread is running
			return 0; // Return finished
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	case 0x01F300A9:
		// Cancel the UMD data cache thread
		if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
			// TODO :
			// Wake up the thread waiting for the UMD data cache handling.
			return 0;
		} else {
			return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
		}
		break;
	// TODO: What do these do?  Seem to require a u32 in, no output.
	case 0x01F100A6:
	case 0x01F100A8:
	case 0x01F100A9:
		ERROR_LOG_REPORT(Log::sceIo, "UNIMPL sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd, argAddr, argLen, outPtr, outLen);
		return 0;
	}

	// This should really send it on to a FileSystem implementation instead.

	if (!strcmp(name, "mscmhc0:") || !strcmp(name, "ms0:") || !strcmp(name, "memstick:"))
	{
		// MemoryStick checks
		switch (cmd) {
		case 0x02025801:	
			// Check the MemoryStick's driver status (mscmhc0: only.)
			if (Memory::IsValidAddress(outPtr) && outLen >= 4) {
				if (MemoryStick_State() == PSP_MEMORYSTICK_STATE_INSERTED) {
					// 1 = not inserted (ready), 4 = inserted
					Memory::Write_U32(PSP_MEMORYSTICK_STATE_DEVICE_INSERTED, outPtr);
				} else {
					Memory::Write_U32(PSP_MEMORYSTICK_STATE_DRIVER_READY, outPtr);
				}
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02015804:
			// Register MemoryStick's insert/eject callback (mscmhc0)
			if (Memory::IsValidAddress(argAddr) && outPtr == 0 && argLen >= 4) {
				u32 cbId = Memory::Read_U32(argAddr);
				int type = -1;
				kernelObjects.GetIDType(cbId, &type);

				if (memStickCallbacks.size() < 32 && type == SCE_KERNEL_TMID_Callback) {
					memStickCallbacks.push_back(cbId);
					if (MemoryStick_State() == PSP_MEMORYSTICK_STATE_INSERTED) {
						// Only fired immediately if the card is currently inserted.
						// Values observed:
						//  * 1 = Memory stick inserted
						//  * 2 = Memory stick removed
						//  * 4 = Memory stick mounting? (followed by a 1 about 500ms later)
						DEBUG_LOG(Log::sceIo, "sceIoDevctl: Memstick callback %i registered, notifying immediately", cbId);
						__KernelNotifyCallback(cbId, MemoryStick_State());
					} else {
						DEBUG_LOG(Log::sceIo, "sceIoDevctl: Memstick callback %i registered", cbId);
					}
					return 0;
				} else {
					return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
				}
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02015805:	
			// Unregister MemoryStick's insert/eject callback (mscmhc0)
			if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
				SceUID cbId = Memory::Read_U32(argAddr);
				size_t slot = (size_t)-1;
				// We want to only remove one at a time.
				for (size_t i = 0; i < memStickCallbacks.size(); ++i) {
					if (memStickCallbacks[i] == cbId) {
						slot = i;
						break;
					}
				}

				if (slot != (size_t)-1) {
					memStickCallbacks.erase(memStickCallbacks.begin() + slot);
					DEBUG_LOG(Log::sceIo, "sceIoDevctl: Unregistered memstick callback %i", cbId);
					return 0;
				} else {
					return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
				}
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02025806:	
			// Check if the device is inserted (mscmhc0)
			if (Memory::IsValidAddress(outPtr) && outLen >= 4) {
				// 1 = Inserted.
				// 2 = Not inserted.
				Memory::Write_U32(MemoryStick_State(), outPtr);
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02425818:  
			// Get MS capacity (fatms0).
			if (MemoryStick_State() != PSP_MEMORYSTICK_STATE_INSERTED) {
				return SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND;
			}
			// TODO: Pretend we have a 2GB memory stick?  Should we check MemoryStick_FreeSpace?
			if (Memory::IsValidRange(argAddr, 4) && argLen >= 4) {  // NOTE: not outPtr
				u32 pointer = Memory::ReadUnchecked_U32(argAddr);
				u32 sectorSize = 0x200;
				u32 memStickSectorSize = 32 * 1024;
				u32 sectorCount = memStickSectorSize / sectorSize;
				u64 freeSize = 1 * 1024 * 1024 * 1024;

				auto deviceSize = PSPPointer<DeviceSize>::Create(pointer);
				if (deviceSize.IsValid()) {
					deviceSize->maxClusters = (u32)((freeSize * 95 / 100) / (sectorSize * sectorCount));
					deviceSize->freeClusters = deviceSize->maxClusters;
					deviceSize->maxSectors = deviceSize->maxClusters;
					deviceSize->sectorSize = sectorSize;
					deviceSize->sectorCount = sectorCount;
					deviceSize.NotifyWrite("ms0:02425818");
				}

				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02425824:
			// Check if write protected
			if (MemoryStick_State() != PSP_MEMORYSTICK_STATE_INSERTED) {
				return SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND;
			}
			if (Memory::IsValidRange(outPtr, 4) && outLen == 4) {
				Memory::WriteUnchecked_U32(0, outPtr);
				return 0;
			} else {
				ERROR_LOG(Log::sceIo, "Failed 0x02425824 fat");
				return -1;
			}
			break;
		}
	}

	if (!strcmp(name, "fatms0:"))
	{
		switch (cmd) {
		case 0x0240d81e:
			// TODO: Invalidate MS driver file table cache (nop)
			break;
		case 0x02415821:
			// MScmRegisterMSInsertEjectCallback
			if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
				u32 cbId = Memory::Read_U32(argAddr);
				int type = -1;
				kernelObjects.GetIDType(cbId, &type);

				if (memStickFatCallbacks.size() < 32 && type == SCE_KERNEL_TMID_Callback) {
					memStickFatCallbacks.push_back(cbId);
					if (MemoryStick_State() == PSP_MEMORYSTICK_STATE_INSERTED) {
						// Only fired immediately if the card is currently inserted.
						// Values observed:
						//  * 1 = Memory stick inserted
						//  * 2 = Memory stick removed
						DEBUG_LOG(Log::sceIo, "sceIoDevCtl: Memstick FAT callback %i registered, notifying immediately", cbId);
						__KernelNotifyCallback(cbId, MemoryStick_FatState());
					} else {
						DEBUG_LOG(Log::sceIo, "sceIoDevCtl: Memstick FAT callback %i registered", cbId);
					}
					return 0;
				} else {
					return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
				}
			} else {
				return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
			}
			break;
		case 0x02415822:
			// MScmUnregisterMSInsertEjectCallback
			if (Memory::IsValidAddress(argAddr) && argLen >= 4) {
				SceUID cbId = Memory::Read_U32(argAddr);
				size_t slot = (size_t)-1;
				// We want to only remove one at a time.
				for (size_t i = 0; i < memStickFatCallbacks.size(); ++i) {
					if (memStickFatCallbacks[i] == cbId) {
						slot = i;
						break;
					}
				}

				if (slot != (size_t)-1) {
					memStickFatCallbacks.erase(memStickFatCallbacks.begin() + slot);
					DEBUG_LOG(Log::sceIo, "sceIoDevCtl: Unregistered memstick FAT callback %i", cbId);
					return 0;
				} else {
					return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
				}
			}
			break;
		case 0x02415823:  
			// Set FAT as enabled
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				MemoryStick_SetFatState((MemStickFatState)Memory::Read_U32(argAddr));
				return 0;
			} else {
				ERROR_LOG(Log::sceIo, "Failed 0x02415823 fat");
				return -1;
			}
			break;
		case 0x02425823:  
			// Check if FAT enabled
			// If the values added together are >= 0x80000000, or less than outPtr, invalid address.
			if (((int)outPtr + outLen) < (int)outPtr) {
				ERROR_LOG(Log::sceIo, "sceIoDevctl: fatms0: 0x02425823 command, bad address");
				return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
			} else if (!Memory::IsValidAddress(outPtr)) {
				// Technically, only checks for NULL, crashes for many bad addresses.
				ERROR_LOG(Log::sceIo, "sceIoDevctl: fatms0: 0x02425823 command, no output address");
				return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
			} else {
				// Does not care about outLen, even if it's 0.
				// Note: writes 1 when inserted, 0 when not inserted.
				Memory::Write_U32(MemoryStick_FatState(), outPtr);
				return hleDelayResult(0, "check fat state", cyclesToUs(23500));
			}
			break;
		case 0x02425824:  
			// Check if write protected
			if (MemoryStick_State() != PSP_MEMORYSTICK_STATE_INSERTED) {
				return SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND;
			}
			if (Memory::IsValidAddress(outPtr) && outLen == 4) {
				Memory::Write_U32(0, outPtr);
				return 0;
			} else {
				ERROR_LOG(Log::sceIo, "Failed 0x02425824 fat");
				return -1;
			}
			break;
		case 0x02425818:  
			// Get MS capacity (fatms0).
			if (MemoryStick_State() != PSP_MEMORYSTICK_STATE_INSERTED) {
				return SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND;
			}
			// TODO: Pretend we have a 2GB memory stick?  Should we check MemoryStick_FreeSpace?
			if (Memory::IsValidAddress(argAddr) && argLen >= 4) {  // NOTE: not outPtr
				u32 pointer = Memory::Read_U32(argAddr);
				u32 sectorSize = 0x200;
				u32 memStickSectorSize = 32 * 1024;
				u32 sectorCount = memStickSectorSize / sectorSize;
				u64 freeSize = 1 * 1024 * 1024 * 1024;

				auto deviceSize = PSPPointer<DeviceSize>::Create(pointer);
				if (deviceSize.IsValid()) {
					deviceSize->maxClusters = (u32)((freeSize  * 95 / 100) / (sectorSize * sectorCount));
					deviceSize->freeClusters = deviceSize->maxClusters;
					deviceSize->maxSectors = deviceSize->maxClusters;
					deviceSize->sectorSize = sectorSize;
					deviceSize->sectorCount = sectorCount;
					deviceSize.NotifyWrite("fatms0:02425818");
				}
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		}
	}

	if (!strcmp(name, "kemulator:") || !strcmp(name, "emulator:"))
	{
		// Emulator special tricks!
		
		enum {
			EMULATOR_DEVCTL__GET_HAS_DISPLAY = 1,
			EMULATOR_DEVCTL__SEND_OUTPUT,
			EMULATOR_DEVCTL__IS_EMULATOR,
			EMULATOR_DEVCTL__VERIFY_STATE,

			EMULATOR_DEVCTL__EMIT_SCREENSHOT = 0x20,
			
			EMULATOR_DEVCTL__TOGGLE_FASTFORWARD = 0x30,
			EMULATOR_DEVCTL__GET_ASPECT_RATIO,
			EMULATOR_DEVCTL__GET_SCALE,
			EMULATOR_DEVCTL__GET_AXIS,
			EMULATOR_DEVCTL__GET_VKEY,
		};

		switch (cmd) {
		case EMULATOR_DEVCTL__GET_HAS_DISPLAY:
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(PSP_CoreParameter().headLess ? 0 : 1, outPtr);
			return 0;
		case EMULATOR_DEVCTL__SEND_OUTPUT:
			if (Memory::IsValidRange(argAddr, argLen)) {
				std::string data(Memory::GetCharPointerUnchecked(argAddr), argLen);
				if (!System_SendDebugOutput(data))
					DEBUG_LOG(Log::sceIo, "%s", data.c_str());
				if (PSP_CoreParameter().collectDebugOutput)
					*PSP_CoreParameter().collectDebugOutput += data;
			}
			return 0;
		case EMULATOR_DEVCTL__IS_EMULATOR:
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(1, outPtr);
			return 0;
		case EMULATOR_DEVCTL__VERIFY_STATE:
			// Note that this is async, and makes sure the save state matches up.
			SaveState::Verify();
			// TODO: Maybe save/load to a file just to be sure?
			return 0;

		case EMULATOR_DEVCTL__EMIT_SCREENSHOT:
		{
			PSPPointer<u8> topaddr;
			u32 linesize;

			__DisplayGetFramebuf(&topaddr, &linesize, nullptr, 0);
			// TODO: Convert based on pixel format / mode / something?
			System_SendDebugScreenshot(std::string((const char *)&topaddr[0], linesize * 272), 272);
			return 0;
		}
		case EMULATOR_DEVCTL__TOGGLE_FASTFORWARD:
			if (argAddr)
				PSP_CoreParameter().fastForward = true;
			else
				PSP_CoreParameter().fastForward = false;
			return 0;
		case EMULATOR_DEVCTL__GET_ASPECT_RATIO:
			if (Memory::IsValidAddress(outPtr)) {
				// TODO: Share code with CalculateDisplayOutputRect to take a few more things into account.
				// I have a planned further refactoring.
				float ar;
				if (g_Config.bDisplayStretch) {
					ar = (float)g_display.dp_xres / (float)g_display.dp_yres;
				} else {
					ar = g_Config.fDisplayAspectRatio * (480.0f / 272.0f);
				}
				Memory::Write_Float(ar, outPtr);
			}
			return 0;
		case EMULATOR_DEVCTL__GET_SCALE:
			if (Memory::IsValidAddress(outPtr)) {
				// TODO: Maybe do something more sophisticated taking the longest side and screen rotation
				// into account, etc.
				float scale = (float)g_display.dp_xres * g_Config.fDisplayScale / 480.0f;
				Memory::Write_Float(scale, outPtr);
			}
			return 0;
		case EMULATOR_DEVCTL__GET_AXIS:
			if (Memory::IsValidAddress(outPtr) && (argAddr >= 0 && argAddr < JOYSTICK_AXIS_MAX)) {
				Memory::Write_Float(HLEPlugins::PluginDataAxis[argAddr], outPtr);
			}
			return 0;
		case EMULATOR_DEVCTL__GET_VKEY:
			if (Memory::IsValidAddress(outPtr) && (argAddr >= 0 && argAddr < NKCODE_MAX)) {
				Memory::Write_U8(HLEPlugins::GetKey(argAddr), outPtr);
			}
			return 0;
		}

		ERROR_LOG(Log::sceIo, "sceIoDevCtl: UNKNOWN PARAMETERS");

		return 0;
	}

	//089c6d1c weird branch
	/*
	089c6bdc ]: HLE: sceKernelCreateCallback(name= MemoryStick Detection ,entry= 089c7484 ) (z_un_089c6bc4)
	089c6c40 ]: HLE: sceKernelCreateCallback(name= MemoryStick Assignment ,entry= 089c7534 ) (z_un_089c6bc4)
	*/
	ERROR_LOG_REPORT(Log::sceIo, "UNIMPL sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd, argAddr, argLen, outPtr, outLen);
	return SCE_KERNEL_ERROR_UNSUP;
}

static u32 sceIoRename(const char *from, const char *to) {
	DEBUG_LOG(Log::sceIo, "sceIoRename(%s, %s)", from, to);

	// TODO: Timing isn't terribly accurate.
	if (!pspFileSystem.GetFileInfo(from).exists)
		return hleDelayResult(SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "file renamed", 1000);

	int result = pspFileSystem.RenameFile(from, to);
	if (result < 0)
		WARN_LOG(Log::sceIo, "Could not move %s to %s", from, to);
	return hleDelayResult(result, "file renamed", 1000);
}

static u32 sceIoChdir(const char *dirname) {
	DEBUG_LOG(Log::sceIo, "sceIoChdir(%s)", dirname);
	return pspFileSystem.ChDir(dirname);
}

static int sceIoChangeAsyncPriority(int id, int priority) {
	// priority = -1 is valid,means the current thread'priority
	if (priority != -1 && (priority < 0x08 || priority > 0x77)) {
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_ILLEGAL_PRIORITY, "illegal priority %d", priority);
	}

	if (id == -1) {
		asyncDefaultPriority = priority;
		return hleLogSuccessI(Log::sceIo, 0);
	}

	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (!f) {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}

	if (asyncThreads[id] && !asyncThreads[id]->Stopped()) {
		if (priority == -1) {
			priority = KernelCurThreadPriority();
		}
		asyncThreads[id]->ChangePriority(priority);
	}

	asyncParams[id].priority = priority;
	return hleLogSuccessI(Log::sceIo, 0);
}

static int sceIoCloseAsync(int id) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (!f) {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}
	if (f->asyncBusy()) {
		return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_ASYNC_BUSY, "async busy");
	}

	f->closePending = true;

	auto &params = asyncParams[id];
	params.op = IoAsyncOp::CLOSE;
	IoStartAsyncThread(id, f);
	return hleLogSuccessI(Log::sceIo, 0);
}

static u32 sceIoSetAsyncCallback(int id, u32 clbckId, u32 clbckArg)
{
	DEBUG_LOG(Log::sceIo, "sceIoSetAsyncCallback(%d, %i, %08x)", id, clbckId, clbckArg);

	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f)
	{
		f->callbackID = clbckId;
		f->callbackArg = clbckArg;
		return 0;
	}
	else
	{
		return error;
	}
}

static u32 sceIoOpenAsync(const char *filename, int flags, int mode) {
	hleEatCycles(18000);
	if (!filename) {
		// Not tested on the PSP.
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "nullptr file not found");
	}

	// TODO: Use an internal method so as not to pollute the log?
	// Intentionally does not work when interrupts disabled.
	if (!__KernelIsDispatchEnabled())
		sceKernelResumeDispatchThread(1);

	int error;
	FileNode *f = __IoOpen(error, filename, flags, mode);

	// We have to return an fd here, which may have been destroyed when we reach Wait if it failed.
	if (f == nullptr) {
		_assert_(error != 0);
		if (error == SCE_KERNEL_ERROR_NODEV)
			return hleLogError(Log::sceIo, error, "device not found");

		f = new FileNode();
		kernelObjects.Create(f);
		f->handle = -1;
		f->fullpath = filename;
		f->closePending = true;
	}

	// We need an fd even for errors, since it's async.
	int fd = __IoAllocFd(f);
	if (fd < 0) {
		kernelObjects.Destroy<FileNode>(f->GetUID());
		return hleLogError(Log::sceIo, hleDelayResult(fd, "file opened", 1000), "out of fds");
	}

	auto &params = asyncParams[fd];
	params.op = IoAsyncOp::OPEN;
	params.priority = asyncDefaultPriority;
	params.open.filenameAddr = PARAM(0);
	params.open.flags = flags;
	params.open.mode = mode;
	IoStartAsyncThread(fd, f);

	if (error != 0) {
		f->asyncResult = (s64)error;
		return hleLogError(Log::sceIo, fd, "file not found");
	}

	f->asyncResult = fd;
	return hleLogSuccessI(Log::sceIo, fd);
}

static u32 sceIoGetAsyncStat(int id, u32 poll, u32 address) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (__IsInInterrupt()) {
			DEBUG_LOG(Log::sceIo, "%lli = sceIoGetAsyncStat(%i, %i, %08x): illegal context", f->asyncResult, id, poll, address);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
		if (f->pendingAsyncResult) {
			if (poll) {
				DEBUG_LOG(Log::sceIo, "%lli = sceIoGetAsyncStat(%i, %i, %08x): not ready", f->asyncResult, id, poll, address);
				return 1;
			} else {
				if (!__KernelIsDispatchEnabled()) {
					DEBUG_LOG(Log::sceIo, "%lli = sceIoGetAsyncStat(%i, %i, %08x): dispatch disabled", f->asyncResult, id, poll, address);
					return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
				}

				DEBUG_LOG(Log::sceIo, "%lli = sceIoGetAsyncStat(%i, %i, %08x): waiting", f->asyncResult, id, poll, address);
				f->waitingThreads.push_back(__KernelGetCurThread());
				__KernelWaitCurThread(WAITTYPE_ASYNCIO, f->GetUID(), address, 0, false, "io waited");
			}
		} else if (f->hasAsyncResult) {
			if (!__KernelIsDispatchEnabled()) {
				DEBUG_LOG(Log::sceIo, "%lli = sceIoGetAsyncStat(%i, %i, %08x): dispatch disabled", f->asyncResult, id, poll, address);
				return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
			}

			DEBUG_LOG(Log::sceIo, "%lli = sceIoGetAsyncStat(%i, %i, %08x)", f->asyncResult, id, poll, address);
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}
		} else {
			WARN_LOG(Log::sceIo, "SCE_KERNEL_ERROR_NOASYNC = sceIoGetAsyncStat(%i, %i, %08x)", id, poll, address);
			return SCE_KERNEL_ERROR_NOASYNC;
		}
		return 0; //completed
	} else {
		if (id == 0) {
			return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_BADF, "ERROR - sceIoGetAsyncStat with invalid id %d", id);
		} else {
			return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_BADF, "ERROR - sceIoGetAsyncStat with invalid id %d", id);
		}
	}
}

static int sceIoWaitAsync(int id, u32 address) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (__IsInInterrupt()) {
			return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "illegal context");
		}
		if (f->pendingAsyncResult) {
			if (!__KernelIsDispatchEnabled()) {
				return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
			}
			f->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitCurThread(WAITTYPE_ASYNCIO, f->GetUID(), address, 0, false, "io waited");
			return hleLogSuccessI(Log::sceIo, 0, "waiting");
		} else if (f->hasAsyncResult) {
			if (!__KernelIsDispatchEnabled()) {
				return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
			}
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}

			return hleLogSuccessI(Log::sceIo, 0, "complete");
		} else {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_NOASYNC, "no async pending");
		}
		return 0; //completed
	} else {
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_BADF, "invalid fd");
	}
}

static int sceIoWaitAsyncCB(int id, u32 address) {
	// Should process callbacks here
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (__IsInInterrupt()) {
			return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "illegal context");
		}

		hleCheckCurrentCallbacks();
		if (f->pendingAsyncResult) {
			// TODO: This seems to re-enable dispatch or something?
			f->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitCurThread(WAITTYPE_ASYNCIO, f->GetUID(), address, 0, true, "io waited");
			return hleLogSuccessI(Log::sceIo, 0, "waiting");
		} else if (f->hasAsyncResult) {
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}

			return hleLogSuccessI(Log::sceIo, 0, "complete");
		} else {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_NOASYNC, "no async pending");
		}
	} else {
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_BADF, "invalid fd");
	}
}

static u32 sceIoPollAsync(int id, u32 address) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->pendingAsyncResult) {
			return hleLogSuccessVerboseI(Log::sceIo, 1, "not ready");
		} else if (f->hasAsyncResult) {
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}
			return hleLogSuccessI(Log::sceIo, 0);
		} else {
			return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_NOASYNC, "no async pending");
		}
	} else {
		return hleLogError(Log::sceIo, SCE_KERNEL_ERROR_BADF, "invalid fd");
	}
}

class DirListing : public KernelObject {
public:
	const char *GetName() override { return name.c_str(); }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "DirListing"; }
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_DirList; }
	int GetIDType() const override { return PPSSPP_KERNEL_TMID_DirList; }

	void DoState(PointerWrap &p) override {
		auto s = p.Section("DirListing", 1);
		if (!s)
			return;

		Do(p, name);
		Do(p, index);

		// TODO: Is this the right way for it to wake up?
		int count = (int) listing.size();
		Do(p, count);
		listing.resize(count);
		for (int i = 0; i < count; ++i) {
			listing[i].DoState(p);
		}
	}

	std::string name;
	std::vector<PSPFileInfo> listing;
	int index;
};

static u32 sceIoDopen(const char *path) {
	DEBUG_LOG(Log::sceIo, "sceIoDopen(\"%s\")", path);

	double startTime = time_now_d();

	bool listingExists = false;
	auto listing = pspFileSystem.GetDirListing(path, &listingExists);

	if (!listingExists) {
		return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}

	DirListing *dir = new DirListing();
	SceUID id = kernelObjects.Create(dir);

	dir->listing = listing;
	dir->index = 0;
	dir->name = std::string(path);

	double listTime = time_now_d() - startTime;

	if (listTime > 0.01) {
		INFO_LOG(Log::IO, "Dir listing '%s' took %0.3f", path, listTime);
	}

	// Blacklist some directories that games should not be able to find out about.
	// Speeds up directory iteration on slow Android Scoped Storage implementations :(
	// Might also want to filter out PSP/GAME if not a homebrew, maybe also irrelevant directories
	// in PSP/SAVEDATA, though iffy to know which ones are irrelevant..
	// Also if we're stripping PSP from the path due to setting a directory named PSP as the memstick root,
	// these will also show up at ms0: which is not ideal. Should find some other way to deal with that.
	if (!strcmp(path, "ms0:/PSP") || !strcmp(path, "ms0:")) {
		static const char *const pspFolderBlacklist[] = {
			"CHEATS",
			"PPSSPP_STATE",
			"PLUGINS",
			"SYSTEM",
			"SCREENSHOT",
			"TEXTURES",
			"DUMP",
			"SHADERS",
		};
		std::vector<PSPFileInfo> filtered;
		for (const auto &entry : dir->listing) {
			bool blacklisted = false;
			for (auto black : pspFolderBlacklist) {
				if (!strcasecmp(entry.name.c_str(), black)) {
					blacklisted = true;
					break;
				}
			}
			// Also don't let games see a GAME directory in the root. This confuses Wipeout...
			if (!strcasecmp(entry.name.c_str(), "GAME") && !strcmp(path, "ms0:")) {
				blacklisted = true;
			}

			if (!blacklisted) {
				filtered.push_back(entry);
			}
		}

		dir->listing = filtered;
	}
	
	// TODO: The result is delayed only from the memstick, it seems.
	return id;
}

// For some reason strncpy will fill up the entire output buffer. No reason to do that,
// so we use this trivial replacement.
static void strcpy_limit(char *dest, const char *src, int limit) {
	int i;
	for (i = 0; i < limit - 1; i++) {
		if (!src[i])
			break;
		dest[i] = src[i];
	}
	// Always null terminate.
	dest[i] = 0;
}

static u32 sceIoDread(int id, u32 dirent_addr) {
	u32 error;
	DirListing *dir = kernelObjects.Get<DirListing>(id, error);
	if (dir) {
		SceIoDirEnt *entry = (SceIoDirEnt*) Memory::GetPointer(dirent_addr);

		if (dir->index == (int) dir->listing.size()) {
			DEBUG_LOG(Log::sceIo, "sceIoDread( %d %08x ) - end", id, dirent_addr);
			entry->d_name[0] = '\0';
			return 0;
		}

		PSPFileInfo &info = dir->listing[dir->index];
		__IoGetStat(&entry->d_stat, info);

		strncpy(entry->d_name, info.name.c_str(), 256);
		entry->d_name[255] = '\0';
		
		bool isFAT = pspFileSystem.FlagsFromFilename(dir->name) & FileSystemFlags::SIMULATE_FAT32;
		// Only write d_private for memory stick
		if (isFAT) {
			// All files look like they're executable on FAT. This is required for Beats, see issue #14812
			entry->d_stat.st_mode |= 0111;
			// write d_private for supporting Custom BGM
			// ref JPCSP https://code.google.com/p/jpcsp/source/detail?r=3468
			if (Memory::IsValidAddress(entry->d_private)){
				if (sceKernelGetCompiledSdkVersion() <= 0x0307FFFF){
					// d_private is pointing to an area of unknown size
					// - [0..12] "8.3" file name (null-terminated), could be empty.
					// - [13..???] long file name (null-terminated)

					// Hm, so currently we don't write the short name at all to d_private? TODO
					strcpy_limit((char*)Memory::GetPointer(entry->d_private + 13), (const char*)entry->d_name, ARRAY_SIZE(entry->d_name));
				}
				else {
					// d_private is pointing to an area of total size 1044
					// - [0..3] size of area
					// - [4..19] "8.3" file name (null-terminated), could be empty.
					// - [20..???] long file name (null-terminated)
					auto size = Memory::Read_U32(entry->d_private);
					// Hm, so currently we don't write the short name at all to d_private? TODO
					if (size >= 1044) {
						strcpy_limit((char*)Memory::GetPointer(entry->d_private + 20), (const char*)entry->d_name, ARRAY_SIZE(entry->d_name));
					}
				}
			}
		}
		DEBUG_LOG(Log::sceIo, "sceIoDread( %d %08x ) = %s", id, dirent_addr, entry->d_name);

		// TODO: Improve timing.  Only happens on the *first* entry read, ms and umd.
		if (dir->index++ == 0) {
			return hleDelayResult(1, "readdir", 1000);
		}
		return 1;
	} else {
		DEBUG_LOG(Log::sceIo, "sceIoDread - invalid listing %i, error %08x", id, error);
		return SCE_KERNEL_ERROR_BADF;
	}
}

static u32 sceIoDclose(int id) {
	DEBUG_LOG(Log::sceIo, "sceIoDclose(%d)", id);
	return kernelObjects.Destroy<DirListing>(id);
}

int __IoIoctl(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (error) {
		ERROR_LOG(Log::sceIo, "%08x=sceIoIoctl id: %08x, cmd %08x, bad file", error, id, cmd);
		return error;
	}
	if (f->asyncBusy()) {
		ERROR_LOG(Log::sceIo, "%08x=sceIoIoctl id: %08x, cmd %08x, async busy", error, id, cmd);
		return SCE_KERNEL_ERROR_ASYNC_BUSY;
	}

	// TODO: Move this into each command, probably?
	usec += 100;

	//KD Hearts:
	//56:46:434 HLE\sceIo.cpp:886 E[HLE]: UNIMPL 0=sceIoIoctrl id: 0000011f, cmd 04100001, indataPtr 08b313d8, inlen 00000010, outdataPtr 00000000, outLen 0
	//	0000000

	// TODO: This kind of stuff should be moved to the devices (wherever that would be)
	// and does not belong in this file. Same thing with Devctl.

	switch (cmd) {
	// Define decryption key (amctrl.prx DRM)
	case 0x04100001: {
		u8 keybuf[16];
		u8 *key_ptr;
		u8 pgd_header[0x90];
		u8 pgd_magic[4] = {0x00, 0x50, 0x47, 0x44};

		if (Memory::IsValidAddress(indataPtr) && inlen == 16) {
			memcpy(keybuf, Memory::GetPointerUnchecked(indataPtr), 16);
			key_ptr = keybuf;
		}else{
			key_ptr = NULL;
		}

		DEBUG_LOG(Log::sceIo, "Decrypting PGD DRM files");
		pspFileSystem.SeekFile(f->handle, (s32)f->pgd_offset, FILEMOVE_BEGIN);
		pspFileSystem.ReadFile(f->handle, pgd_header, 0x90);
		f->pgdInfo = pgd_open(pgd_header, 2, key_ptr);
		if (!f->pgdInfo) {
			f->npdrm = false;
			pspFileSystem.SeekFile(f->handle, (s32)0, FILEMOVE_BEGIN);
			if (memcmp(pgd_header, pgd_magic, 4) == 0) {
				ERROR_LOG(Log::sceIo, "%s is PGD file, but there's likely a key mismatch. Returning error.", f->fullpath.c_str());
				// File is PGD file, but key mismatch
				return ERROR_PGD_INVALID_HEADER;
			} else {
				INFO_LOG(Log::sceIo, "%s is not an encrypted PGD file as was expected. Proceeding.", f->fullpath.c_str());
				// File is not encrypted.
				return 0;
			}
		} else {
			// Everything OK.
			f->npdrm = true;
			f->pgdInfo->data_offset += f->pgd_offset;
			return 0;
		}
		break;
	}

	// Set PGD offset. Called from sceNpDrmEdataSetupKey
	case 0x04100002:
		f->pgd_offset = indataPtr;
		break;

	// Get PGD data size. Called from sceNpDrmEdataGetDataSize
	case 0x04100010:
		if(f->pgdInfo)
			return f->pgdInfo->data_size;
		else
			return (int)f->FileInfo().size;
		break;

	// Get UMD sector size
	case 0x01020003:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Asked for sector size of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 4) {
			// ISOs always use 2048 sized sectors.
			Memory::Write_U32(2048, outdataPtr);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Get UMD file offset
	case 0x01020004:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		DEBUG_LOG(Log::sceIo, "sceIoIoctl: Asked for file offset of file %d", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 4) {
			u32 offset = (u32)pspFileSystem.GetSeekPos(f->handle);
			Memory::Write_U32(offset, outdataPtr);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	case 0x01010005:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Seek for file %i", id);
		// Even if the size is 4, it still actually reads a 16 byte struct, it seems.
		if (Memory::IsValidAddress(indataPtr) && inlen >= 4) {
			struct SeekInfo {
				u64_le offset;
				u32_le unk;
				u32_le whence;
			};
			const auto seekInfo = PSPPointer<SeekInfo>::Create(indataPtr);
			FileMove seek;
			s64 newPos = __IoLseekDest(f, seekInfo->offset, seekInfo->whence, seek);
			if (newPos < 0 || newPos > f->FileInfo().size) {
				// Not allowed to seek past the end of the file with this API.
				return ERROR_ERRNO_IO_ERROR;
			}
			pspFileSystem.SeekFile(f->handle, (s32)seekInfo->offset, seek);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Get UMD file start sector.
	case 0x01020006:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Asked for start sector of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 4) {
			Memory::Write_U32(f->FileInfo().startSector, outdataPtr);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Get UMD file size in bytes.
	case 0x01020007:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Asked for size of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 8) {
			Memory::Write_U64(f->FileInfo().size, outdataPtr);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Read from UMD file.
	case 0x01030008:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Read from file %i", id);
		if (Memory::IsValidAddress(indataPtr) && inlen >= 4) {
			u32 size = Memory::Read_U32(indataPtr);
			if (Memory::IsValidAddress(outdataPtr) && size <= outlen) {
				// sceIoRead does its own delaying (and deferring.)
				usec = 0;
				return sceIoRead(id, outdataPtr, size);
			} else {
				return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
			}
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Get current sector seek pos from UMD device file.
	case 0x01d20001:
		// TODO: Should work only for umd0:/, etc. not for ms0:/ or disc0:/.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Sector tell from file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 4) {
			Memory::Write_U32((u32)pspFileSystem.GetSeekPos(f->handle), outdataPtr);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Read raw sectors from UMD device file.
	case 0x01f30003:
		// TODO: Should work only for umd0:/, etc. not for ms0:/ or disc0:/.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Sector read from file %i", id);
		if (Memory::IsValidAddress(indataPtr) && inlen >= 4) {
			u32 size = Memory::Read_U32(indataPtr);
			// Note that size is specified in sectors, not bytes.
			if (size > 0 && Memory::IsValidAddress(outdataPtr) && size <= outlen) {
				// sceIoRead does its own delaying (and deferring.)
				usec = 0;
				return sceIoRead(id, outdataPtr, size);
			} else {
				return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
			}
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Seek by sector in UMD device file.
	case 0x01f100a6:
		// TODO: Should work only for umd0:/, etc. not for ms0:/ or disc0:/.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(Log::sceIo, "sceIoIoctl: Sector seek for file %i", id);
		// Even if the size is 4, it still actually reads a 16 byte struct, it seems.

		//if (GetIOTimingMethod() == IOTIMING_REALISTIC) // Need a check for io timing method?
		usec += 15000;// Fantasy Golf Pangya Portable(KS) needs a delay over 15000us.

		if (Memory::IsValidAddress(indataPtr) && inlen >= 4) {
			struct SeekInfo {
				u64_le offset;
				u32_le unk;
				u32_le whence;
			};
			const auto seekInfo = PSPPointer<SeekInfo>::Create(indataPtr);
			FileMove seek;
			s64 newPos = __IoLseekDest(f, seekInfo->offset, seekInfo->whence, seek);
			// Position is in sectors, don't forget.
			if (newPos < 0 || newPos > f->FileInfo().size) {
				// Not allowed to seek past the end of the file with this API.
				return SCE_KERNEL_ERROR_ERRNO_INVALID_FILE_SIZE;
			}
			pspFileSystem.SeekFile(f->handle, (s32)seekInfo->offset, seek);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	default:
		{
			int result = pspFileSystem.Ioctl(f->handle, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
			if (result == (int)SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED) {
				char temp[256];
				// We want the reported message to include the cmd, so it's unique.
				snprintf(temp, sizeof(temp), "sceIoIoctl(%%s, %08x, %%08x, %%x, %%08x, %%x)", cmd);
				Reporting::ReportMessage(temp, f->fullpath.c_str(), indataPtr, inlen, outdataPtr, outlen);
				ERROR_LOG(Log::sceIo, "UNIMPL 0=sceIoIoctl id: %08x, cmd %08x, indataPtr %08x, inlen %08x, outdataPtr %08x, outLen %08x", id,cmd,indataPtr,inlen,outdataPtr,outlen);
			}
			return result;
		}
		break;
	}

	return 0;
}

u32 sceIoIoctl(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen)
{
	int usec = 0;
	int result = __IoIoctl(id, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
	if (usec != 0) {
		return hleDelayResult(result, "io ctrl command", usec);
	}
	return result;
}

static u32 sceIoIoctlAsync(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen)
{
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			return hleLogWarning(Log::sceIo, SCE_KERNEL_ERROR_ASYNC_BUSY, "async busy");
		}

		auto &params = asyncParams[id];
		params.op = IoAsyncOp::IOCTL;
		params.ioctl.cmd = cmd;
		params.ioctl.inAddr = indataPtr;
		params.ioctl.inSize = inlen;
		params.ioctl.outAddr = outdataPtr;
		params.ioctl.outSize = outlen;
		IoStartAsyncThread(id, f);
		return hleLogSuccessI(Log::sceIo, 0);
	} else {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}
}

static u32 sceIoGetFdList(u32 outAddr, int outSize, u32 fdNumAddr) {
	WARN_LOG(Log::sceIo, "sceIoGetFdList(%08x, %i, %08x)", outAddr, outSize, fdNumAddr);

	auto out = PSPPointer<SceUID_le>::Create(outAddr);
	int count = 0;

	// Always have the first three.
	for (int i = 0; i < PSP_MIN_FD; ++i) {
		// TODO: Technically it seems like these are fixed ids > PSP_COUNT_FDS.
		if (count < outSize && out.IsValid()) {
			out[count] = i;
		}
		++count;
	}

	for (int i = PSP_MIN_FD; i < PSP_COUNT_FDS; ++i) {
		if (fds[i] == 0) {
			continue;
		}
		if (count < outSize && out.IsValid()) {
			out[count] = i;
		}
		++count;
	}

	if (Memory::IsValidAddress(fdNumAddr))
		Memory::Write_U32(count, fdNumAddr);
	if (count >= outSize) {
		return outSize;
	} else {
		return count;
	}
}

// Presumably lets you hook up stderr to a MsgPipe.
static u32 sceKernelRegisterStderrPipe(u32 msgPipeUID) {
	ERROR_LOG_REPORT(Log::sceIo, "UNIMPL sceKernelRegisterStderrPipe(%08x)", msgPipeUID);
	return 0;
}

static u32 sceKernelRegisterStdoutPipe(u32 msgPipeUID) {
	ERROR_LOG_REPORT(Log::sceIo, "UNIMPL sceKernelRegisterStdoutPipe(%08x)", msgPipeUID);
	return 0;
}

static int IoAsyncFinish(int id) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		// Reset this so the Io funcs don't reject the request.
		f->pendingAsyncResult = false;
		// Reset the PC back so we will run again on resume.
		currentMIPS->pc = asyncThreads[id]->Entry();

		auto &params = asyncParams[id];

		int result;
		int us;
		bool complete;

		switch (params.op) {
		case IoAsyncOp::READ:
			complete = __IoRead(result, id, params.std.addr, params.std.size, us);
			if (complete) {
				f->asyncResult = (s64)result;
				DEBUG_LOG(Log::sceIo, "ASYNC %llx=sceIoReadAsync(%d, %08x, %x)", f->asyncResult, id, params.std.addr, params.std.size);
			} else {
				DEBUG_LOG(Log::sceIo, "ASYNC sceIoReadAsync(%d, %08x, %x): deferring result", id, params.std.addr, params.std.size);
			}
			break;

		case IoAsyncOp::WRITE:
			complete = __IoWrite(result, id, params.std.addr, params.std.size, us);
			if (complete) {
				f->asyncResult = (s64)result;
				DEBUG_LOG(Log::sceIo, "ASYNC %llx=sceIoWriteAsync(%d, %08x, %x)", f->asyncResult, id, params.std.addr, params.std.size);
			} else {
				DEBUG_LOG(Log::sceIo, "ASYNC sceIoWriteAsync(%d, %08x, %x): deferring result", id, params.std.addr, params.std.size);
			}
			break;

		case IoAsyncOp::SEEK:
			f->asyncResult = __IoLseek(id, params.seek.pos, params.seek.whence);
			// Educated guess at timing.
			us = 100;
			DEBUG_LOG(Log::sceIo, "ASYNC %lli = sceIoLseekAsync(%d, %llx, %i)", f->asyncResult, id, params.seek.pos, params.seek.whence);
			break;

		case IoAsyncOp::OPEN:
		{
			// See notes on timing in sceIoOpen.
			if (!Memory::IsValidNullTerminatedString(params.open.filenameAddr)) {
				// Bad
				ERROR_LOG(Log::sceIo, "Bad pointer to filename %08x", params.open.filenameAddr);
				us = 80;
				break;
			}
			const std::string filename = Memory::GetCharPointerUnchecked(params.open.filenameAddr);
			IFileSystem *sys = pspFileSystem.GetSystemFromFilename(filename);
			if (sys) {
				if (f->asyncResult == (int)SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND) {
					us = sys->Flags() & FileSystemFlags::UMD ? 6000 : 10000;
				} else if (sys->DevType(f->handle) & (PSPDevType::BLOCK | PSPDevType::EMU_LBN)) {
					// These are fast to open, no delay or even rescheduling happens.
					us = 80;
				} else {
					us = sys->Flags() & FileSystemFlags::UMD ? 4000 : 10000;
				}
			} else {
				us = 80;
			}
			break;
		}

		case IoAsyncOp::CLOSE:
			f->asyncResult = 0;
			// CLOSE shouldn't have a delay. See #12549.
			us = 0;
			DEBUG_LOG(Log::sceIo, "ASYNC %lli = sceIoCloseAsync(%d)", f->asyncResult, id);
			break;

		case IoAsyncOp::IOCTL:
			us = 0;  // __IoIoctl will add 100.
			f->asyncResult = __IoIoctl(id, params.ioctl.cmd, params.ioctl.inAddr, params.ioctl.inSize, params.ioctl.outAddr, params.ioctl.outSize, us);
			DEBUG_LOG(Log::sceIo, "ASYNC sceIoIoctlAsync(%08x, %08x, %08x, %08x, %08x, %08x)", id, params.ioctl.cmd, params.ioctl.inAddr, params.ioctl.inSize, params.ioctl.outAddr, params.ioctl.outSize);
			break;

		default:
			ERROR_LOG_REPORT(Log::sceIo, "Unknown async op %d", (int)params.op);
			us = 0;
			break;
		}

		__IoSchedAsync(f, id, us);
		__KernelWaitCurThread(WAITTYPE_ASYNCIO, id, 0, 0, false, "async io");
		hleSkipDeadbeef();

		params.op = IoAsyncOp::NONE;
		return 0;
	} else {
		return hleLogError(Log::sceIo, error, "bad file descriptor");
	}
}

KernelObject *__KernelFileNodeObject() {
	return new FileNode;
}

KernelObject *__KernelDirListingObject() {
	return new DirListing;
}

const HLEFunction IoFileMgrForUser[] = {
	{0xB29DDF9C, &WrapU_C<sceIoDopen>,                  "sceIoDopen",                  'i', "s"     },
	{0xE3EB004C, &WrapU_IU<sceIoDread>,                 "sceIoDread",                  'i', "ix"    },
	{0xEB092469, &WrapU_I<sceIoDclose>,                 "sceIoDclose",                 'i', "i"     },
	{0xE95A012B, &WrapU_UUUUUU<sceIoIoctlAsync>,        "sceIoIoctlAsync",             'i', "ixpipi"},
	{0x63632449, &WrapU_UUUUUU<sceIoIoctl>,             "sceIoIoctl",                  'i', "ixpipi"},
	{0xACE946E8, &WrapU_CU<sceIoGetstat>,               "sceIoGetstat",                'i', "sx"    },
	{0xB8A740F4, &WrapU_CUU<sceIoChstat>,               "sceIoChstat",                 'i', "sxx"   },
	{0x55F4717D, &WrapU_C<sceIoChdir>,                  "sceIoChdir",                  'i', "s"     },
	{0X08BD7374, &WrapU_I<sceIoGetDevType>,             "sceIoGetDevType",             'x', "i"     },
	{0xB2A628C1, &WrapU_UUUIUI<sceIoAssign>,            "sceIoAssign",                 'i', "sssixi"},
	{0XE8BC6571, &WrapU_I<sceIoCancel>,                 "sceIoCancel",                 'i', "i"     },
	{0XB293727F, &WrapI_II<sceIoChangeAsyncPriority>,   "sceIoChangeAsyncPriority",    'i', "ix"    },
	{0X810C4BC3, &WrapU_I<sceIoClose>,                  "sceIoClose",                  'i', "i"     },
	{0XFF5940B6, &WrapI_I<sceIoCloseAsync>,             "sceIoCloseAsync",             'i', "i"     },
	{0x54F5FB11, &WrapU_CIUIUI<sceIoDevctl>,            "sceIoDevctl",                 'i', "sxpipi"},
	{0XCB05F8D6, &WrapU_IUU<sceIoGetAsyncStat>,         "sceIoGetAsyncStat",           'i', "iiP"   },
	{0x27EB27B8, &WrapI64_II64I<sceIoLseek>,            "sceIoLseek",                  'I', "iIi"   },
	{0x68963324, &WrapU_III<sceIoLseek32>,              "sceIoLseek32",                'i', "iii"   },
	{0X1B385D8F, &WrapU_III<sceIoLseek32Async>,         "sceIoLseek32Async",           'i', "iii"   },
	{0X71B19E77, &WrapU_II64I<sceIoLseekAsync>,         "sceIoLseekAsync",             'i', "iIi"   },
	{0x109F50BC, &WrapU_CII<sceIoOpen>,                 "sceIoOpen",                   'i', "sii"   },
	{0x89AA9906, &WrapU_CII<sceIoOpenAsync>,            "sceIoOpenAsync",              'i', "sii"   },
	{0x06A70004, &WrapU_CI<sceIoMkdir>,                 "sceIoMkdir",                  'i', "si"    },
	{0x3251EA56, &WrapU_IU<sceIoPollAsync>,             "sceIoPollAsync",              'i', "iP"    },
	{0x6A638D83, &WrapU_IUI<sceIoRead>,                 "sceIoRead",                   'i', "ixi"   },
	{0xA0B5A7C2, &WrapU_IUI<sceIoReadAsync>,            "sceIoReadAsync",              'i', "ixi"   },
	{0xF27A9C51, &WrapU_C<sceIoRemove>,                 "sceIoRemove",                 'i', "s"     },
	{0x779103A0, &WrapU_CC<sceIoRename>,                "sceIoRename",                 'i', "ss"    },
	{0x1117C65F, &WrapU_C<sceIoRmdir>,                  "sceIoRmdir",                  'i', "s"     },
	{0XA12A0514, &WrapU_IUU<sceIoSetAsyncCallback>,     "sceIoSetAsyncCallback",       'i', "ixx"   },
	{0xAB96437F, &WrapU_CI<sceIoSync>,                  "sceIoSync",                   'i', "si"    },
	{0x6D08A871, &WrapU_C<sceIoUnassign>,               "sceIoUnassign",               'i', "s"     },
	{0x42EC03AC, &WrapU_IUI<sceIoWrite>,                "sceIoWrite",                  'i', "ixi"   },
	{0x0FACAB19, &WrapU_IUI<sceIoWriteAsync>,           "sceIoWriteAsync",             'i', "ixi"   },
	{0x35DBD746, &WrapI_IU<sceIoWaitAsyncCB>,           "sceIoWaitAsyncCB",            'i', "iP"    },
	{0xE23EEC33, &WrapI_IU<sceIoWaitAsync>,             "sceIoWaitAsync",              'i', "iP"    },
	{0X5C2BE2CC, &WrapU_UIU<sceIoGetFdList>,            "sceIoGetFdList",              'i', "xip"   },
	{0x13370001, &WrapI_I<IoAsyncFinish>,               "__IoAsyncFinish",             'i', "i"     },
};

void Register_IoFileMgrForUser() {
	RegisterModule("IoFileMgrForUser", ARRAY_SIZE(IoFileMgrForUser), IoFileMgrForUser);
}

const HLEFunction IoFileMgrForKernel[] = {
	{0XA905B705, nullptr,                               "sceIoCloseAll",               '?', ""        },
	{0X411106BA, nullptr,                               "sceIoGetThreadCwd",           '?', ""        },
	{0XCB0A151F, nullptr,                               "sceIoChangeThreadCwd",        '?', ""        },
	{0X8E982A74, nullptr,                               "sceIoAddDrv",                 '?', ""        },
	{0XC7F35804, nullptr,                               "sceIoDelDrv",                 '?', ""        },
	{0X3C54E908, nullptr,                               "sceIoReopen",                 '?', ""        },
	{0xB29DDF9C, &WrapU_C<sceIoDopen>,                  "sceIoDopen",                  'i', "s",      HLE_KERNEL_SYSCALL },
	{0xE3EB004C, &WrapU_IU<sceIoDread>,                 "sceIoDread",                  'i', "ix",     HLE_KERNEL_SYSCALL },
	{0xEB092469, &WrapU_I<sceIoDclose>,                 "sceIoDclose",                 'i', "i",      HLE_KERNEL_SYSCALL },
	{0X109F50BC, &WrapU_CII<sceIoOpen>,                 "sceIoOpen",                   'i', "sii",    HLE_KERNEL_SYSCALL },
	{0x6A638D83, &WrapU_IUI<sceIoRead>,                 "sceIoRead",                   'i', "ixi",    HLE_KERNEL_SYSCALL },
	{0x42EC03AC, &WrapU_IUI<sceIoWrite>,                "sceIoWrite",                  'i', "ixi",    HLE_KERNEL_SYSCALL },
	{0x68963324, &WrapU_III<sceIoLseek32>,              "sceIoLseek32",                'i', "iii",    HLE_KERNEL_SYSCALL },
	{0x27EB27B8, &WrapI64_II64I<sceIoLseek>,            "sceIoLseek",                  'I', "iIi",    HLE_KERNEL_SYSCALL },
	{0x810C4BC3, &WrapU_I<sceIoClose>,                  "sceIoClose",                  'i', "i",      HLE_KERNEL_SYSCALL },
	{0x779103A0, &WrapU_CC<sceIoRename>,                "sceIoRename",                 'i', "ss",     HLE_KERNEL_SYSCALL },
	{0xF27A9C51, &WrapU_C<sceIoRemove>,                 "sceIoRemove",                 'i', "s",      HLE_KERNEL_SYSCALL },
	{0x55F4717D, &WrapU_C<sceIoChdir>,                  "sceIoChdir",                  'i', "s",      HLE_KERNEL_SYSCALL },
	{0x06A70004, &WrapU_CI<sceIoMkdir>,                 "sceIoMkdir",                  'i', "si",     HLE_KERNEL_SYSCALL },
	{0x1117C65F, &WrapU_C<sceIoRmdir>,                  "sceIoRmdir",                  'i', "s",      HLE_KERNEL_SYSCALL },
	{0x54F5FB11, &WrapU_CIUIUI<sceIoDevctl>,            "sceIoDevctl",                 'i', "sxpipi", HLE_KERNEL_SYSCALL },
	{0x63632449, &WrapU_UUUUUU<sceIoIoctl>,             "sceIoIoctl",                  'i', "ixpipi", HLE_KERNEL_SYSCALL },
	{0xAB96437F, &WrapU_CI<sceIoSync>,                  "sceIoSync",                   'i', "si",     HLE_KERNEL_SYSCALL },
	{0xB2A628C1, &WrapU_UUUIUI<sceIoAssign>,            "sceIoAssign",                 'i', "sssixi", HLE_KERNEL_SYSCALL },
	{0x6D08A871, &WrapU_C<sceIoUnassign>,               "sceIoUnassign",               'i', "s",      HLE_KERNEL_SYSCALL },
	{0xACE946E8, &WrapU_CU<sceIoGetstat>,               "sceIoGetstat",                'i', "sx",     HLE_KERNEL_SYSCALL },
	{0xB8A740F4, &WrapU_CUU<sceIoChstat>,               "sceIoChstat",                 'i', "sxx",    HLE_KERNEL_SYSCALL },
	{0xA0B5A7C2, &WrapU_IUI<sceIoReadAsync>,            "sceIoReadAsync",              'i', "ixi",    HLE_KERNEL_SYSCALL },
	{0x3251EA56, &WrapU_IU<sceIoPollAsync>,             "sceIoPollAsync",              'i', "iP",     HLE_KERNEL_SYSCALL },
	{0xE23EEC33, &WrapI_IU<sceIoWaitAsync>,             "sceIoWaitAsync",              'i', "iP",     HLE_KERNEL_SYSCALL },
	{0x35DBD746, &WrapI_IU<sceIoWaitAsyncCB>,           "sceIoWaitAsyncCB",            'i', "iP",     HLE_KERNEL_SYSCALL },
	{0xBD17474F, nullptr,                               "sceIoGetIobUserLevel",        '?', ""        },
	{0x76DA16E3, nullptr,                               "IoFileMgrForKernel_76DA16E3", '?', ""        },
};

void Register_IoFileMgrForKernel() {
	RegisterModule("IoFileMgrForKernel", ARRAY_SIZE(IoFileMgrForKernel), IoFileMgrForKernel);
}

const HLEFunction StdioForUser[] = {
	{0X172D316E, &WrapU_V<sceKernelStdin>,              "sceKernelStdin",              'i', ""      },
	{0XA6BAB2E9, &WrapU_V<sceKernelStdout>,             "sceKernelStdout",             'i', ""      },
	{0XF78BA90A, &WrapU_V<sceKernelStderr>,             "sceKernelStderr",             'i', ""      },
	{0X432D8F5C, &WrapU_U<sceKernelRegisterStdoutPipe>, "sceKernelRegisterStdoutPipe", 'i', "x"     },
	{0X6F797E03, &WrapU_U<sceKernelRegisterStderrPipe>, "sceKernelRegisterStderrPipe", 'i', "x"     },
	{0XA46785C9, nullptr,                               "sceKernelStdioSendChar",      '?', ""      },
	{0X0CBB0571, nullptr,                               "sceKernelStdioLseek",         '?', ""      },
	{0X3054D478, nullptr,                               "sceKernelStdioRead",          '?', ""      },
	{0XA3B931DB, nullptr,                               "sceKernelStdioWrite",         '?', ""      },
	{0X924ABA61, nullptr,                               "sceKernelStdioOpen",          '?', ""      },
	{0X9D061C19, nullptr,                               "sceKernelStdioClose",         '?', ""      },
};

void Register_StdioForUser() {
	RegisterModule("StdioForUser", ARRAY_SIZE(StdioForUser), StdioForUser);
}

const HLEFunction StdioForKernel[] = {
	{0X98220F3E, nullptr,                                            "sceKernelStdoutReopen",                   '?', ""   },
	{0XFB5380C5, nullptr,                                            "sceKernelStderrReopen",                   '?', ""   },
	{0XCAB439DF, nullptr,                                            "printf",                                  '?', ""   },
	{0X2CCF071A, nullptr,                                            "fdprintf",                                '?', ""   },
	{0XD97C8CB9, nullptr,                                            "puts",                                    '?', ""   },
	{0X172D316E, nullptr,                                            "sceKernelStdin",                          '?', ""   },
	{0XA6BAB2E9, &WrapU_V<sceKernelStdout>,                          "sceKernelStdout",                         'i', "" ,HLE_KERNEL_SYSCALL },	
	{0XF78BA90A, nullptr,                                            "sceKernelStderr",                         '?', ""   },
};

void Register_StdioForKernel() {
	RegisterModule("StdioForKernel", ARRAY_SIZE(StdioForKernel), StdioForKernel);
}
