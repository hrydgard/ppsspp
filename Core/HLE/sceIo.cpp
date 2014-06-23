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

#include "native/thread/thread.h"
#include "native/thread/threadutil.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Host.h"
#include "Core/SaveState.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernel.h"
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

const int ERROR_ERRNO_FILE_NOT_FOUND               = 0x80010002;
const int ERROR_ERRNO_IO_ERROR                     = 0x80010005;
const int ERROR_ERRNO_FILE_ALREADY_EXISTS          = 0x80010011;
const int ERROR_MEMSTICK_DEVCTL_BAD_PARAMS         = 0x80220081;
const int ERROR_MEMSTICK_DEVCTL_TOO_MANY_CALLBACKS = 0x80220082;
const int ERROR_KERNEL_BAD_FILE_DESCRIPTOR         = 0x80020323;

const int ERROR_PGD_INVALID_HEADER                 = 0x80510204;

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

#define O_RDONLY        0x0001
#define O_WRONLY        0x0002
#define O_RDWR          0x0003
#define O_NBLOCK        0x0010
#define O_APPEND        0x0100
#define O_CREAT         0x0200
#define O_TRUNC         0x0400
#define O_NOWAIT        0x8000
#define O_NPDRM         0x40000000

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
// TODO: Should be 3, and stdin/stdout/stderr are special values aliased to 0?
const int PSP_MIN_FD = 4;
const int PSP_STDOUT = 1;
const int PSP_STDERR = 2;
const int PSP_STDIN = 3;
static int asyncNotifyEvent = -1;
static int syncNotifyEvent = -1;
static SceUID fds[PSP_COUNT_FDS];
static std::set<SceUID> memStickCallbacks;
static std::set<SceUID> memStickFatCallbacks;
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

class FileNode : public KernelObject {
public:
	FileNode() : callbackID(0), callbackArg(0), asyncResult(0), hasAsyncResult(false), pendingAsyncResult(false), sectorBlockMode(false), closePending(false), npdrm(0), pgdInfo(NULL) {}
	~FileNode() {
		pspFileSystem.CloseFile(handle);
		pgd_close(pgdInfo);
	}
	const char *GetName() {return fullpath.c_str();}
	const char *GetTypeName() {return "OpenFile";}
	void GetQuickInfo(char *ptr, int size) {
		sprintf(ptr, "Seekpos: %08x", (u32)pspFileSystem.GetSeekPos(handle));
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_File; }
	int GetIDType() const { return PPSSPP_KERNEL_TMID_File; }

	bool asyncBusy() {
		return pendingAsyncResult || hasAsyncResult;
	}

	virtual void DoState(PointerWrap &p) {
		auto s = p.Section("FileNode", 1);
		if (!s)
			return;

		p.Do(fullpath);
		p.Do(handle);
		p.Do(callbackID);
		p.Do(callbackArg);
		p.Do(asyncResult);
		p.Do(hasAsyncResult);
		p.Do(pendingAsyncResult);
		p.Do(sectorBlockMode);
		p.Do(closePending);
		p.Do(info);
		p.Do(openMode);

		p.Do(npdrm);
		p.Do(pgd_offset);
		bool hasPGD = pgdInfo != NULL;
		p.Do(hasPGD);
		if (hasPGD) {
			if (p.mode == p.MODE_READ) {
				pgdInfo = (PGD_DESC*) malloc(sizeof(PGD_DESC));
			}
			p.DoVoid(pgdInfo, sizeof(PGD_DESC));
			if (p.mode == p.MODE_READ) {
				pgdInfo->block_buf = (u8 *)malloc(pgdInfo->block_size * 2);
			}
		}

		p.Do(waitingThreads);
		p.Do(pausedWaits);
	}

	std::string fullpath;
	u32 handle;

	u32 callbackID;
	u32 callbackArg;

	s64 asyncResult;
	bool hasAsyncResult;
	bool pendingAsyncResult;

	bool sectorBlockMode;
	// TODO: Use an enum instead?
	bool closePending;

	PSPFileInfo info;
	u32 openMode;

	u32 npdrm;
	u32 pgd_offset;
	PGD_DESC *pgdInfo;

	std::vector<SceUID> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	// Value is actually meaningless but kept for consistency with other wait types.
	std::map<SceUID, u64> pausedWaits;
};

/******************************************************************************/



/******************************************************************************/

void __IoCompleteAsyncIO(int fd);

static void TellFsThreadEnded (SceUID threadID) {
	pspFileSystem.ThreadEnded(threadID);
}

FileNode *__IoGetFd(int fd, u32 &error) {
	if (fd < 0 || fd >= PSP_COUNT_FDS) {
		error = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
		return NULL;
	}

	return kernelObjects.Get<FileNode>(fds[fd], error);
}

int __IoAllocFd(FileNode *f) {
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

void __IoFreeFd(int fd, u32 &error) {
	if (fd < PSP_MIN_FD || fd >= PSP_COUNT_FDS) {
		error = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
	} else {
		FileNode *f = __IoGetFd(fd, error);
		if (f) {
			// Wake anyone waiting on the file before closing it.
			for (size_t i = 0; i < f->waitingThreads.size(); ++i) {
				HLEKernel::ResumeFromWait(f->waitingThreads[i], WAITTYPE_ASYNCIO, f->GetUID(), (int)SCE_KERNEL_ERROR_WAIT_DELETE);
			}

			// Discard any pending results.
			AsyncIOResult managerResult;
			ioManager.WaitResult(f->handle, managerResult);
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

// TODO: We don't do any of that yet.
// For now, let's at least delay the callback mnotification.
void __IoAsyncNotify(u64 userdata, int cyclesLate) {
	int fd = (int) userdata;
	__IoCompleteAsyncIO(fd);

	u32 error;
	FileNode *f = __IoGetFd(fd, error);
	if (!f) {
		ERROR_LOG_REPORT(SCEIO, "__IoAsyncNotify: file no longer exists?");
		return;
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

void __IoSyncNotify(u64 userdata, int cyclesLate) {
	SceUID threadID = userdata >> 32;
	int fd = (int) (userdata & 0xFFFFFFFF);

	s64 result = -1;
	u32 error;
	FileNode *f = __IoGetFd(fd, error);
	if (!f) {
		ERROR_LOG_REPORT(SCEIO, "__IoSyncNotify: file no longer exists?");
		return;
	}

	f->pendingAsyncResult = false;
	f->hasAsyncResult = false;

	AsyncIOResult managerResult;
	if (ioManager.WaitResult(f->handle, managerResult)) {
		result = managerResult;
	} else {
		ERROR_LOG(SCEIO, "Unable to complete IO operation on %s", f->GetName());
	}

	HLEKernel::ResumeFromWait(threadID, WAITTYPE_IO, fd, result);
}

void __IoAsyncBeginCallback(SceUID threadID, SceUID prevCallbackId) {
	auto result = HLEKernel::WaitBeginCallback<FileNode, WAITTYPE_ASYNCIO, SceUID>(threadID, prevCallbackId, -1);
	if (result == HLEKernel::WAIT_CB_SUCCESS) {
		DEBUG_LOG(SCEIO, "sceIoWaitAsync: Suspending wait for callback")
	} else if (result == HLEKernel::WAIT_CB_BAD_WAIT_ID) {
		WARN_LOG_REPORT(SCEIO, "sceIoWaitAsync: beginning callback with bad wait id?");
	}
}

bool __IoCheckAsyncWait(FileNode *f, SceUID threadID, u32 &error, int result, bool &wokeThreads)
{
	int fd = -1;
	for (int i = 0; i < (int)ARRAY_SIZE(fds); ++i) {
		if (fds[i] == f->GetUID()) {
			fd = i;
			break;
		}
	}
	if (fd == -1) {
		ERROR_LOG_REPORT(SCEIO, "__IoCheckAsyncWait: could not find io handle");
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

void __IoAsyncEndCallback(SceUID threadID, SceUID prevCallbackId) {
	auto result = HLEKernel::WaitEndCallback<FileNode, WAITTYPE_ASYNCIO, SceUID>(threadID, prevCallbackId, -1, __IoCheckAsyncWait);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT) {
		DEBUG_LOG(SCEIO, "sceKernelWaitEventFlagCB: Resuming lock wait for callback");
	}
}

static DirectoryFileSystem *memstickSystem = NULL;
#if defined(USING_WIN_UI) || defined(APPLE)
static DirectoryFileSystem *flash0System = NULL;
#else
static VFSFileSystem *flash0System = NULL;
#endif

void __IoManagerThread() {
	setCurrentThreadName("IOThread");
	while (ioManagerThreadEnabled && coreState != CORE_ERROR && coreState != CORE_POWERDOWN) {
		ioManager.RunEventsUntil(CoreTiming::GetTicks() + msToCycles(1000));
	}
}

void __IoWakeManager() {
	// Ping the thread so that it knows to check coreState.
	ioManagerThreadEnabled = false;
	ioManager.FinishEventLoop();
}

void __IoInit() {
	MemoryStick_SetFatState(PSP_FAT_MEMORYSTICK_STATE_ASSIGNED);

	asyncNotifyEvent = CoreTiming::RegisterEvent("IoAsyncNotify", __IoAsyncNotify);
	syncNotifyEvent = CoreTiming::RegisterEvent("IoSyncNotify", __IoSyncNotify);

	memstickSystem = new DirectoryFileSystem(&pspFileSystem, g_Config.memCardDirectory, FILESYSTEM_SIMULATE_FAT32);
#if defined(USING_WIN_UI) || defined(APPLE)
	flash0System = new DirectoryFileSystem(&pspFileSystem, g_Config.flash0Directory);
#else
	flash0System = new VFSFileSystem(&pspFileSystem, "flash0");
#endif
	pspFileSystem.Mount("ms0:", memstickSystem);
	pspFileSystem.Mount("fatms0:", memstickSystem);
	pspFileSystem.Mount("fatms:", memstickSystem);
	pspFileSystem.Mount("pfat0:", memstickSystem);
	pspFileSystem.Mount("flash0:", flash0System);
	
	__KernelListenThreadEnd(&TellFsThreadEnded);

	memset(fds, 0, sizeof(fds));

	ioManagerThreadEnabled = g_Config.bSeparateIOThread;
	ioManager.SetThreadEnabled(ioManagerThreadEnabled);
	if (ioManagerThreadEnabled) {
		Core_ListenShutdown(&__IoWakeManager);
		ioManagerThread = new std::thread(&__IoManagerThread);
#ifdef _XBOX
		SuspendThread(ioManagerThread->native_handle());
		XSetThreadProcessor(ioManagerThread->native_handle(), 4);
		ResumeThread(ioManagerThread->native_handle());
#endif
		ioManagerThread->detach();
	}

	__KernelRegisterWaitTypeFuncs(WAITTYPE_ASYNCIO, __IoAsyncBeginCallback, __IoAsyncEndCallback);
}

void __IoDoState(PointerWrap &p) {
	auto s = p.Section("sceIo", 1);
	if (!s)
		return;

	ioManager.DoState(p);
	p.DoArray(fds, ARRAY_SIZE(fds));
	p.Do(asyncNotifyEvent);
	CoreTiming::RestoreRegisterEvent(asyncNotifyEvent, "IoAsyncNotify", __IoAsyncNotify);
	p.Do(syncNotifyEvent);
	CoreTiming::RestoreRegisterEvent(syncNotifyEvent, "IoSyncNotify", __IoSyncNotify);
	p.Do(memStickCallbacks);
	p.Do(memStickFatCallbacks);
}

void __IoShutdown() {
	ioManagerThreadEnabled = false;
	ioManager.SyncThread();
	ioManager.FinishEventLoop();
	if (ioManagerThread != NULL) {
		delete ioManagerThread;
		ioManagerThread = NULL;
		ioManager.Shutdown();
	}

	pspFileSystem.Unmount("ms0:", memstickSystem);
	pspFileSystem.Unmount("fatms0:", memstickSystem);
	pspFileSystem.Unmount("fatms:", memstickSystem);
	pspFileSystem.Unmount("pfat0:", memstickSystem);
	pspFileSystem.Unmount("flash0:", flash0System);

	delete memstickSystem;
	memstickSystem = NULL;
	delete flash0System;
	flash0System = NULL;

	memStickCallbacks.clear();
	memStickFatCallbacks.clear();
}

u32 __IoGetFileHandleFromId(u32 id, u32 &outError)
{
	FileNode *f = __IoGetFd(id, outError);
	if (!f) {
		return (u32)-1;
	}
	return f->handle;
}

u32 sceIoAssign(u32 alias_addr, u32 physical_addr, u32 filesystem_addr, int mode, u32 arg_addr, int argSize) 
{
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
	WARN_LOG_REPORT(SCEIO, "sceIoAssign(%s, %s, %s, %s, %08x, %i)", alias.c_str(), physical_dev.c_str(), filesystem_dev.c_str(), perm.c_str(), arg_addr, argSize);
	return 0;
}

u32 sceIoUnassign(const char *alias)  
{
	WARN_LOG_REPORT(SCEIO, "sceIoUnassign(%s)", alias);
	return 0;
}

u32 sceKernelStdin() {
	DEBUG_LOG(SCEIO, "%d=sceKernelStdin()", PSP_STDIN);
	return PSP_STDIN;
}

u32 sceKernelStdout() {
	DEBUG_LOG(SCEIO, "%d=sceKernelStdout()", PSP_STDOUT);
	return PSP_STDOUT;
}

u32 sceKernelStderr() {
	DEBUG_LOG(SCEIO, "%d=sceKernelStderr()", PSP_STDERR);
	return PSP_STDERR;
}

void __IoCompleteAsyncIO(int fd) {
	u32 error;
	FileNode *f = __IoGetFd(fd, error);
	if (f) {
		AsyncIOResult managerResult;
		if (ioManager.WaitResult(f->handle, managerResult)) {
			f->asyncResult = managerResult;
		} else {
			// It's okay, not all operations are deferred.
		}
		if (f->callbackID) {
			__KernelNotifyCallback(f->callbackID, f->callbackArg);
		}
		f->pendingAsyncResult = false;
		f->hasAsyncResult = true;
	}
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

void __IoGetStat(SceIoStat *stat, PSPFileInfo &info) {
	memset(stat, 0xfe, sizeof(SceIoStat));
	stat->st_size = (s64) info.size;

	int type, attr;
	if (info.type & FILETYPE_DIRECTORY)
		type = SCE_STM_FDIR, attr = TYPE_DIR;
	else
		type = SCE_STM_FREG, attr = TYPE_FILE;

	stat->st_mode = type | info.access;
	stat->st_attr = attr;
	stat->st_size = info.size;
	__IoCopyDate(stat->st_a_time, info.atime);
	__IoCopyDate(stat->st_c_time, info.ctime);
	__IoCopyDate(stat->st_m_time, info.mtime);
	stat->st_private[0] = info.startSector;
}

void __IoSchedAsync(FileNode *f, int fd, int usec) {
	CoreTiming::ScheduleEvent(usToCycles(usec), asyncNotifyEvent, fd);

	f->pendingAsyncResult = true;
	f->hasAsyncResult = false;
}

void __IoSchedSync(FileNode *f, int fd, int usec) {
	u64 param = ((u64)__KernelGetCurThread()) << 32 | fd;
	CoreTiming::ScheduleEvent(usToCycles(usec), syncNotifyEvent, param);

	f->pendingAsyncResult = false;
	f->hasAsyncResult = false;
}

u32 sceIoGetstat(const char *filename, u32 addr) {
	// TODO: Improve timing (although this seems normally slow..)
	int usec = 1000;

	SceIoStat stat;
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	if (info.exists) {
		__IoGetStat(&stat, info);
		if (Memory::IsValidAddress(addr)) {
			Memory::WriteStruct(addr, &stat);
			DEBUG_LOG(SCEIO, "sceIoGetstat(%s, %08x) : sector = %08x", filename, addr, info.startSector);
			return hleDelayResult(0, "io getstat", usec);
		} else {
			ERROR_LOG(SCEIO, "sceIoGetstat(%s, %08x) : bad address", filename, addr);
			return hleDelayResult(-1, "io getstat", usec);
		}
	} else {
		DEBUG_LOG(SCEIO, "sceIoGetstat(%s, %08x) : FILE NOT FOUND", filename, addr);
		return hleDelayResult(ERROR_ERRNO_FILE_NOT_FOUND, "io getstat", usec);
	}
}

u32 sceIoChstat(const char *filename, u32 iostatptr, u32 changebits) {
	ERROR_LOG(SCEIO, "UNIMPL sceIoChstat(%s, %08x, %08x)", filename, iostatptr, changebits);
	if (changebits & SCE_CST_MODE)
		ERROR_LOG(SCEIO, "sceIoChstat: change mode requested");
	if (changebits & SCE_CST_ATTR)
		ERROR_LOG(SCEIO, "sceIoChstat: change attr requested");
	if (changebits & SCE_CST_SIZE)
		ERROR_LOG(SCEIO, "sceIoChstat: change size requested");
	if (changebits & SCE_CST_CT)
		ERROR_LOG(SCEIO, "sceIoChstat: change creation time requested");
	if (changebits & SCE_CST_AT)
		ERROR_LOG(SCEIO, "sceIoChstat: change access time requested");
	if (changebits & SCE_CST_MT)
		ERROR_LOG(SCEIO, "sceIoChstat: change modification time requested");
	if (changebits & SCE_CST_PRVT)
		ERROR_LOG(SCEIO, "sceIoChstat: change private data requested");
	return 0;
}

u32 npdrmRead(FileNode *f, u8 *data, int size) {
	PGD_DESC *pgd = f->pgdInfo;
	u32 block, offset, blockPos;
	u32 remain_size, copy_size;

	block  = pgd->file_offset/pgd->block_size;
	offset = pgd->file_offset%pgd->block_size;

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

bool __IoRead(int &result, int id, u32 data_addr, int size) {
	if (id == PSP_STDIN) {
		DEBUG_LOG(SCEIO, "sceIoRead STDIN");
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
			result = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
			return true;
		} else if (Memory::IsValidAddress(data_addr)) {
			CBreakPoints::ExecMemCheck(data_addr, true, size, currentMIPS->pc);
			u8 *data = (u8*) Memory::GetPointer(data_addr);
			if (f->npdrm) {
				result = npdrmRead(f, data, size);
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
				ev.bytes = size;
				ioManager.ScheduleOperation(ev);
				return false;
			} else {
				result = (int) pspFileSystem.ReadFile(f->handle, data, size);
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

u32 sceIoRead(int id, u32 data_addr, int size) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (id > 2 && f != NULL) {
		if (!__KernelIsDispatchEnabled()) {
			DEBUG_LOG(SCEIO, "sceIoRead(%d, %08x, %x): dispatch disabled", id, data_addr, size);
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt()) {
			DEBUG_LOG(SCEIO, "sceIoRead(%d, %08x, %x): inside interrupt", id, data_addr, size);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
	}

	// TODO: Timing is probably not very accurate, low estimate.
	int us = size / 100;
	if (us < 100) {
		us = 100;
	}

	int result;
	bool complete = __IoRead(result, id, data_addr, size);
	if (!complete) {
		DEBUG_LOG(SCEIO, "sceIoRead(%d, %08x, %x): deferring result", id, data_addr, size);

		__IoSchedSync(f, id, us);
		__KernelWaitCurThread(WAITTYPE_IO, id, 0, 0, false, "io read");
		return 0;
	} else if (result >= 0) {
		DEBUG_LOG(SCEIO, "%x=sceIoRead(%d, %08x, %x)", result, id, data_addr, size);
		return hleDelayResult(result, "io read", us);
	} else {
		WARN_LOG(SCEIO, "sceIoRead(%d, %08x, %x): error %08x", id, data_addr, size, result);
		return result;
	}
}

u32 sceIoReadAsync(int id, u32 data_addr, int size) {
	// TODO: Not sure what the correct delay is (and technically we shouldn't read into the buffer yet...)
	int us = size / 100;
	if (us < 100) {
		us = 100;
	}

	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			WARN_LOG(SCEIO, "sceIoReadAsync(%d, %08x, %x): async busy", id, data_addr, size);
			return SCE_KERNEL_ERROR_ASYNC_BUSY;
		}
		int result;
		bool complete = __IoRead(result, id, data_addr, size);
		if (complete) {
			f->asyncResult = result;
			DEBUG_LOG(SCEIO, "%llx=sceIoReadAsync(%d, %08x, %x)", f->asyncResult, id, data_addr, size);
		} else {
			DEBUG_LOG(SCEIO, "sceIoReadAsync(%d, %08x, %x): deferring result", id, data_addr, size);
		}
		__IoSchedAsync(f, id, us);
		return 0;
	} else {
		ERROR_LOG(SCEIO, "sceIoReadAsync: bad file %d", id);
		return error;
	}
}

bool __IoWrite(int &result, int id, u32 data_addr, int size) {
	const void *data_ptr = Memory::GetPointer(data_addr);
	// Let's handle stdout/stderr specially.
	if (id == PSP_STDOUT || id == PSP_STDERR) {
		const char *str = (const char *) data_ptr;
		const int str_size = size == 0 ? 0 : (str[size - 1] == '\n' ? size - 1 : size);
		INFO_LOG(SCEIO, "%s: %.*s", id == 1 ? "stdout" : "stderr", str_size, str);
		result = size;
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
			result = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
			return true;
		}

		CBreakPoints::ExecMemCheck(data_addr, false, size, currentMIPS->pc);

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
			ev.bytes = size;
			ioManager.ScheduleOperation(ev);
			return false;
		} else {
			result = (int) pspFileSystem.WriteFile(f->handle, (u8 *) data_ptr, size);
		}
		return true;
	} else {
		ERROR_LOG(SCEIO, "sceIoWrite ERROR: no file open");
		result = (s32) error;
		return true;
	}
}

u32 sceIoWrite(int id, u32 data_addr, int size) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (id > 2 && f != NULL) {
		if (!__KernelIsDispatchEnabled()) {
			DEBUG_LOG(SCEIO, "sceIoWrite(%d, %08x, %x): dispatch disabled", id, data_addr, size);
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt()) {
			DEBUG_LOG(SCEIO, "sceIoWrite(%d, %08x, %x): inside interrupt", id, data_addr, size);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
	}

	// TODO: Timing is probably not very accurate, low estimate.
	int us = size / 100;
	if (us < 100) {
		us = 100;
	}

	int result;
	bool complete = __IoWrite(result, id, data_addr, size);
	if (!complete) {
		DEBUG_LOG(SCEIO, "sceIoWrite(%d, %08x, %x): deferring result", id, data_addr, size);

		__IoSchedSync(f, id, us);
		__KernelWaitCurThread(WAITTYPE_IO, id, 0, 0, false, "io write");
		return 0;
	} else if (result >= 0) {
		DEBUG_LOG(SCEIO, "%x=sceIoWrite(%d, %08x, %x)", result, id, data_addr, size);
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
		WARN_LOG(SCEIO, "sceIoWrite(%d, %08x, %x): error %08x", id, data_addr, size, result);
		return result;
	}
}

u32 sceIoWriteAsync(int id, u32 data_addr, int size) {
	// TODO: Not sure what the correct delay is (and technically we shouldn't read from the buffer yet...)
	int us = size / 100;
	if (us < 100) {
		us = 100;
	}

	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			WARN_LOG(SCEIO, "sceIoWriteAsync(%d, %08x, %x): async busy", id, data_addr, size);
			return SCE_KERNEL_ERROR_ASYNC_BUSY;
		}
		int result;
		bool complete = __IoWrite(result, id, data_addr, size);
		if (complete) {
			f->asyncResult = result;
			DEBUG_LOG(SCEIO, "%llx=sceIoWriteAsync(%d, %08x, %x)", f->asyncResult, id, data_addr, size);
		} else {
			DEBUG_LOG(SCEIO, "sceIoWriteAsync(%d, %08x, %x): deferring result", id, data_addr, size);
		}
		__IoSchedAsync(f, id, us);
		return 0;
	} else {
		ERROR_LOG(SCEIO, "sceIoWriteAsync: bad file %d", id);
		return error;
	}
}

u32 sceIoGetDevType(int id) {
	if (id == PSP_STDOUT || id == PSP_STDERR || id == PSP_STDIN) {
		DEBUG_LOG(SCEIO, "sceIoGetDevType(%d)", id);
		return PSP_DEV_TYPE_FILE;
	}

	u32 error;
	FileNode *f = __IoGetFd(id, error);
	int result;
	if (f) {
		// TODO: When would this return PSP_DEV_TYPE_ALIAS?
		WARN_LOG(SCEIO, "sceIoGetDevType(%d - %s)", id, f->fullpath.c_str());
		result = pspFileSystem.DevType(f->handle);
	} else {
		ERROR_LOG(SCEIO, "sceIoGetDevType: unknown id %d", id);
		result = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
	}

	return result;
}

u32 sceIoCancel(int id) 
{
	ERROR_LOG_REPORT(SCEIO, "UNIMPL sceIoCancel(%d)", id);
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		// TODO: Cancel the async operation if possible?
	} else {
		ERROR_LOG(SCEIO, "sceIoCancel: unknown id %d", id);
		error = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
	}

	return error;
}

u32 npdrmLseek(FileNode *f, s32 where, FileMove whence)
{
	u32 newPos, blockPos;

	if(whence==FILEMOVE_BEGIN){
		newPos = where;
	}else if(whence==FILEMOVE_CURRENT){
		newPos = f->pgdInfo->file_offset+where;
	}else{
		newPos = f->pgdInfo->data_size+where;
	}

	if(newPos<0 || newPos>f->pgdInfo->data_size){
		return -EINVAL;
	}

	f->pgdInfo->file_offset = newPos;
	blockPos = newPos&~(f->pgdInfo->block_size-1);
	pspFileSystem.SeekFile(f->handle, (s32)f->pgdInfo->data_offset+blockPos, whence);

	return newPos;
}

s64 __IoLseekDest(FileNode *f, s64 offset, int whence, FileMove &seek) {
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
		newPos = f->info.size + offset;
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

s64 __IoLseek(SceUID id, s64 offset, int whence) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			WARN_LOG(SCEIO, "sceIoLseek*(%d, %llx, %i): async busy", id, offset, whence);
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

s64 sceIoLseek(int id, s64 offset, int whence) {
	s64 result = __IoLseek(id, offset, whence);
	if (result >= 0 || result == -1) {
		DEBUG_LOG(SCEIO, "%lli = sceIoLseek(%d, %llx, %i)", result, id, offset, whence);
		// Educated guess at timing.
		return hleDelayResult(result, "io seek", 100);
	} else {
		ERROR_LOG(SCEIO, "sceIoLseek(%d, %llx, %i) - ERROR: invalid file", id, offset, whence);
		return result;
	}
}

u32 sceIoLseek32(int id, int offset, int whence) {
	s32 result = (s32) __IoLseek(id, offset, whence);
	if (result >= 0 || result == -1) {
		DEBUG_LOG(SCEIO, "%i = sceIoLseek32(%d, %x, %i)", result, id, offset, whence);
		// Educated guess at timing.
		return hleDelayResult(result, "io seek", 100);
	} else {
		ERROR_LOG(SCEIO, "sceIoLseek32(%d, %x, %i) - ERROR: invalid file", id, offset, whence);
		return result;
	}
}

u32 sceIoLseekAsync(int id, s64 offset, int whence) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (whence < 0 || whence > 2) {
			WARN_LOG(SCEIO, "sceIoLseekAsync(%d, %llx, %i): invalid whence", id, offset, whence);
			return SCE_KERNEL_ERROR_INVAL;
		}
		if (f->asyncBusy()) {
			WARN_LOG(SCEIO, "sceIoLseekAsync(%d, %llx, %i): async busy", id, offset, whence);
			return SCE_KERNEL_ERROR_ASYNC_BUSY;
		}
		f->asyncResult = __IoLseek(id, offset, whence);
		// Educated guess at timing.
		__IoSchedAsync(f, id, 100);
		DEBUG_LOG(SCEIO, "%lli = sceIoLseekAsync(%d, %llx, %i)", f->asyncResult, id, offset, whence);
		return 0;
	} else {
		ERROR_LOG(SCEIO, "sceIoLseekAsync(%d, %llx, %i) - ERROR: invalid file", id, offset, whence);
		return error;
	}
	return 0;
}

u32 sceIoLseek32Async(int id, int offset, int whence) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (whence < 0 || whence > 2) {
			WARN_LOG(SCEIO, "sceIoLseek32Async(%d, %x, %i): invalid whence", id, offset, whence);
			return SCE_KERNEL_ERROR_INVAL;
		}
		if (f->asyncBusy()) {
			WARN_LOG(SCEIO, "sceIoLseek*(%d, %x, %i): async busy", id, offset, whence);
			return SCE_KERNEL_ERROR_ASYNC_BUSY;
		}
		f->asyncResult = __IoLseek(id, offset, whence);
		// Educated guess at timing.
		__IoSchedAsync(f, id, 100);
		DEBUG_LOG(SCEIO, "%lli = sceIoLseek32Async(%d, %x, %i)", f->asyncResult, id, offset, whence);
		return 0;
	} else {
		ERROR_LOG(SCEIO, "sceIoLseek32Async(%d, %x, %i) - ERROR: invalid file", id, offset, whence);
		return error;
	}
	return 0;
}

FileNode *__IoOpen(int &error, const char* filename, int flags, int mode) {
	//memory stick filename
	int access = FILEACCESS_NONE;
	if (flags & O_RDONLY)
		access |= FILEACCESS_READ;
	if (flags & O_WRONLY)
		access |= FILEACCESS_WRITE;
	if (flags & O_APPEND)
		access |= FILEACCESS_APPEND;
	if (flags & O_CREAT)
		access |= FILEACCESS_CREATE;
	if (flags & O_TRUNC)
		access |= FILEACCESS_TRUNCATE;

	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);

	u32 h = pspFileSystem.OpenWithError(error, filename, (FileAccess) access);
	if (h == 0) {
		return NULL;
	}

	FileNode *f = new FileNode();
	SceUID id = kernelObjects.Create(f);
	f->handle = h;
	f->fullpath = filename;
	f->asyncResult = id;
	f->info = info;
	f->openMode = access;

	f->npdrm = (flags & O_NPDRM)? true: false;
	f->pgd_offset = 0;

	return f;
}

u32 sceIoOpen(const char *filename, int flags, int mode) {
	if (!__KernelIsDispatchEnabled())
		return -1;

	int error;
	FileNode *f = __IoOpen(error, filename, flags, mode);
	if (f == NULL) 
	{
		// Timing is not accurate, aiming low for now.
		if (error == (int)SCE_KERNEL_ERROR_NOCWD)
		{
			ERROR_LOG(SCEIO, "SCE_KERNEL_ERROR_NOCWD=sceIoOpen(%s, %08x, %08x) - no current working directory", filename, flags, mode);
			return hleDelayResult(SCE_KERNEL_ERROR_NOCWD , "no cwd", 10000);
		}
		else
		{
			ERROR_LOG(SCEIO, "ERROR_ERRNO_FILE_NOT_FOUND=sceIoOpen(%s, %08x, %08x) - file not found", filename, flags, mode);
			return hleDelayResult(ERROR_ERRNO_FILE_NOT_FOUND , "file opened", 10000);
		}
	}

	int id = __IoAllocFd(f);
	if (id < 0) {
		ERROR_LOG(SCEIO, "%08x=sceIoOpen(%s, %08x, %08x): out of fds", id, filename, flags, mode);
		kernelObjects.Destroy<FileNode>(f->GetUID());
		return id;
	} else {
		DEBUG_LOG(SCEIO, "%i=sceIoOpen(%s, %08x, %08x)", id, filename, flags, mode);
		// Timing is not accurate, aiming low for now.
		return hleDelayResult(id, "file opened", 100);
	}
}

u32 sceIoClose(int id) {
	u32 error;
	DEBUG_LOG(SCEIO, "sceIoClose(%d)", id);
	__IoFreeFd(id, error);
	// Timing is not accurate, aiming low for now.
	return hleDelayResult(error, "file closed", 100);
}

u32 sceIoRemove(const char *filename) {
	DEBUG_LOG(SCEIO, "sceIoRemove(%s)", filename);

	// TODO: This timing isn't necessarily accurate, low end for now.
	if(!pspFileSystem.GetFileInfo(filename).exists)
		return hleDelayResult(ERROR_ERRNO_FILE_NOT_FOUND, "file removed", 100);

	pspFileSystem.RemoveFile(filename);
	return hleDelayResult(0, "file removed", 100);
}

u32 sceIoMkdir(const char *dirname, int mode) {
	DEBUG_LOG(SCEIO, "sceIoMkdir(%s, %i)", dirname, mode);
	// TODO: Improve timing.
	if (pspFileSystem.MkDir(dirname))
		return hleDelayResult(0, "mkdir", 1000);
	else
		return hleDelayResult(ERROR_ERRNO_FILE_ALREADY_EXISTS, "mkdir", 1000);
}

u32 sceIoRmdir(const char *dirname) {
	DEBUG_LOG(SCEIO, "sceIoRmdir(%s)", dirname);
	// TODO: Improve timing.
	if (pspFileSystem.RmDir(dirname))
		return hleDelayResult(0, "rmdir", 1000);
	else
		return hleDelayResult(ERROR_ERRNO_FILE_NOT_FOUND, "rmdir", 1000);
}

u32 sceIoSync(const char *devicename, int flag) {
	DEBUG_LOG(SCEIO, "UNIMPL sceIoSync(%s, %i)", devicename, flag);
	return 0;
}

struct DeviceSize {
	u32_le maxClusters;
	u32_le freeClusters;
	u32_le maxSectors;
	u32_le sectorSize;
	u32_le sectorCount;
};

u32 sceIoDevctl(const char *name, int cmd, u32 argAddr, int argLen, u32 outPtr, int outLen) {
	if (strcmp(name, "emulator:")) {
		DEBUG_LOG(SCEIO,"sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd, argAddr, argLen, outPtr, outLen);
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
	// TODO: What does these do?  Seem to require a u32 in, no output.
	case 0x01F100A6:
	case 0x01F100A8:
	case 0x01F100A9:
		ERROR_LOG_REPORT(SCEIO, "UNIMPL sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd, argAddr, argLen, outPtr, outLen);
		return 0;
	}

	// This should really send it on to a FileSystem implementation instead.

	if (!strcmp(name, "mscmhc0:") || !strcmp(name, "ms0:") || !strcmp(name, "memstick:"))
	{
		// MemorySticks Checks
		switch (cmd) {
		case 0x02025801:	
			// Check the MemoryStick's driver status (mscmhc0).
			if (Memory::IsValidAddress(outPtr)) {
				Memory::Write_U32(4, outPtr);  // JPSCP: The right return value is 4 for some reason
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02015804:	
			// Register MemoryStick's insert/eject callback (mscmhc0)
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				// TODO: Verify how duplicates work / how many are allowed.
				u32 cbId = Memory::Read_U32(argAddr);
				if (memStickCallbacks.find(cbId) == memStickCallbacks.end()) {
					memStickCallbacks.insert(cbId);
					DEBUG_LOG(SCEIO, "sceIoDevCtl: Memstick callback %i registered, notifying immediately.", cbId);
					__KernelNotifyCallback(cbId, MemoryStick_State());
					return 0;
				} else {
					return ERROR_MEMSTICK_DEVCTL_TOO_MANY_CALLBACKS;
				}
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02025805:	
			// Unregister MemoryStick's insert/eject callback (mscmhc0)
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				// TODO: Verify how duplicates work / how many are allowed.
				u32 cbId = Memory::Read_U32(argAddr);
				if (memStickCallbacks.find(cbId) != memStickCallbacks.end()) {
					memStickCallbacks.erase(cbId);
					DEBUG_LOG(SCEIO, "sceIoDevCtl: Unregistered memstick callback %i", cbId);
					return 0;
				} else {
					return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
				}
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02025806:	
			// Check if the device is inserted (mscmhc0)
			if (Memory::IsValidAddress(outPtr)) {
				// 0 = Not inserted.
				// 1 = Inserted.
				Memory::Write_U32(1, outPtr);
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02425818:  
			// // Get MS capacity (fatms0).
			// Pretend we have a 2GB memory stick.
			if (Memory::IsValidAddress(argAddr) && argLen >= 4) {  // NOTE: not outPtr
				u32 pointer = Memory::Read_U32(argAddr);
				u32 sectorSize = 0x200;
				u32 memStickSectorSize = 32 * 1024;
				u32 sectorCount = memStickSectorSize / sectorSize;
				u64 freeSize = 1 * 1024 * 1024 * 1024;
				DeviceSize deviceSize;
				deviceSize.maxClusters = (u32)((freeSize  * 95 / 100) / (sectorSize * sectorCount));
				deviceSize.freeClusters = deviceSize.maxClusters;
				deviceSize.maxSectors = deviceSize.maxClusters;
				deviceSize.sectorSize = sectorSize;
				deviceSize.sectorCount = sectorCount;
				Memory::WriteStruct(pointer, &deviceSize);
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
			break;
		case 0x02425824:  // Check if write protected
			if (Memory::IsValidAddress(outPtr) && outLen == 4) {
				Memory::Write_U32(0, outPtr);
				return 0;
			} else {
				ERROR_LOG(SCEIO, "Failed 0x02425824 fat");
				return -1;
			}
			break;
		}
	}

	if (!strcmp(name, "fatms0:"))
	{
		switch (cmd) {
		case 0x02415821:  // MScmRegisterMSInsertEjectCallback
			{
				// TODO: Verify how duplicates work / how many are allowed.
				u32 cbId = Memory::Read_U32(argAddr);
				if (memStickFatCallbacks.find(cbId) == memStickFatCallbacks.end()) {
					memStickFatCallbacks.insert(cbId);
					DEBUG_LOG(SCEIO, "sceIoDevCtl: Memstick FAT callback %i registered, notifying immediately.", cbId);
					__KernelNotifyCallback(cbId, MemoryStick_FatState());
					return 0;
				} else {
					return -1;
				}
			}
			break;
		case 0x02415822: 
			{
				// MScmUnregisterMSInsertEjectCallback
				// TODO: Verify how duplicates work / how many are allowed.
				u32 cbId = Memory::Read_U32(argAddr);
				if (memStickFatCallbacks.find(cbId) != memStickFatCallbacks.end()) {
					memStickFatCallbacks.erase(cbId);
					DEBUG_LOG(SCEIO, "sceIoDevCtl: Unregistered memstick FAT callback %i", cbId);
					return 0;
				} else {
					return -1;
				}
			}
			break;
		case 0x02415823:  
			// Set FAT as enabled
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				MemoryStick_SetFatState((MemStickFatState)Memory::Read_U32(argAddr));
				return 0;
			} else {
				ERROR_LOG(SCEIO, "Failed 0x02415823 fat");
				return -1;
			}
			break;
		case 0x02425823:  
			// Check if FAT enabled
			// If the values added together are >= 0x80000000, or less than outPtr, invalid address.
			if (((int)outPtr + outLen) < (int)outPtr) {
				ERROR_LOG(SCEIO, "sceIoDevctl: fatms0: 0x02425823 command, bad address");
				return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
			} else if (!Memory::IsValidAddress(outPtr)) {
				// Technically, only checks for NULL, crashes for many bad addresses.
				ERROR_LOG(SCEIO, "sceIoDevctl: fatms0: 0x02425823 command, no output address");
				return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
			} else {
				// Does not care about outLen, even if it's 0.
				Memory::Write_U32(MemoryStick_FatState(), outPtr);
				return hleDelayResult(0, "check fat state", cyclesToUs(23500));
			}
			break;
		case 0x02425824:  
			// Check if write protected
			if (Memory::IsValidAddress(outPtr) && outLen == 4) {
				Memory::Write_U32(0, outPtr);
				return 0;
			} else {
				ERROR_LOG(SCEIO, "Failed 0x02425824 fat");
				return -1;
			}
			break;
		case 0x02425818:  
			// // Get MS capacity (fatms0).
			// Pretend we have a 2GB memory stick.
			if (Memory::IsValidAddress(argAddr) && argLen >= 4) {  // NOTE: not outPtr
				u32 pointer = Memory::Read_U32(argAddr);
				u32 sectorSize = 0x200;
				u32 memStickSectorSize = 32 * 1024;
				u32 sectorCount = memStickSectorSize / sectorSize;
				u64 freeSize = 1 * 1024 * 1024 * 1024;
				DeviceSize deviceSize;
				deviceSize.maxClusters = (u32)((freeSize  * 95 / 100) / (sectorSize * sectorCount));
				deviceSize.freeClusters = deviceSize.maxClusters;
				deviceSize.maxSectors = deviceSize.maxClusters;
				deviceSize.sectorSize = sectorSize;
				deviceSize.sectorCount = sectorCount;
				Memory::WriteStruct(pointer, &deviceSize);
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
		switch (cmd) {
		case 1:	// EMULATOR_DEVCTL__GET_HAS_DISPLAY
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(0, outPtr);	 // TODO: Make a headless mode for running tests!
			return 0;
		case 2:	// EMULATOR_DEVCTL__SEND_OUTPUT
			{
				std::string data(Memory::GetCharPointer(argAddr), argLen);
				if (PSP_CoreParameter().printfEmuLog) {
					host->SendDebugOutput(data);
				} else {
					if (PSP_CoreParameter().collectEmuLog) {
						*PSP_CoreParameter().collectEmuLog += data;
					} else {
						DEBUG_LOG(SCEIO, "%s", data.c_str());
					}
				}
				return 0;
			}
		case 3:	// EMULATOR_DEVCTL__IS_EMULATOR
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(1, outPtr);
			return 0;
		case 4: // EMULATOR_DEVCTL__VERIFY_STATE
			// Note that this is async, and makes sure the save state matches up.
			SaveState::Verify();
			// TODO: Maybe save/load to a file just to be sure?
			return 0;

		case 0x20: // EMULATOR_DEVCTL__EMIT_SCREENSHOT
			u8 *topaddr;
			u32 linesize, pixelFormat;

			__DisplayGetFramebuf(&topaddr, &linesize, &pixelFormat, 0);
			// TODO: Convert based on pixel format / mode / something?
			host->SendDebugScreenshot(topaddr, linesize, 272);
			return 0;
		}

		ERROR_LOG(SCEIO, "sceIoDevCtl: UNKNOWN PARAMETERS");

		return 0;
	}

	//089c6d1c weird branch
	/*
	089c6bdc ]: HLE: sceKernelCreateCallback(name= MemoryStick Detection ,entry= 089c7484 ) (z_un_089c6bc4)
	089c6c40 ]: HLE: sceKernelCreateCallback(name= MemoryStick Assignment ,entry= 089c7534 ) (z_un_089c6bc4)
	*/
	ERROR_LOG_REPORT(SCEIO, "UNIMPL sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd, argAddr, argLen, outPtr, outLen);
	return SCE_KERNEL_ERROR_UNSUP;
}

u32 sceIoRename(const char *from, const char *to) {
	DEBUG_LOG(SCEIO, "sceIoRename(%s, %s)", from, to);

	// TODO: Timing isn't terribly accurate.
	if (!pspFileSystem.GetFileInfo(from).exists)
		return hleDelayResult(ERROR_ERRNO_FILE_NOT_FOUND, "file renamed", 1000);

	int result = pspFileSystem.RenameFile(from, to);
	if (result < 0)
		WARN_LOG(SCEIO, "Could not move %s to %s", from, to);
	return hleDelayResult(result, "file renamed", 1000);
}

u32 sceIoChdir(const char *dirname) {
	DEBUG_LOG(SCEIO, "sceIoChdir(%s)", dirname);
	return pspFileSystem.ChDir(dirname);
}

int sceIoChangeAsyncPriority(int id, int priority)
{	
	// priority = -1 is valid
	if (priority < 0 && priority != -1) {
		ERROR_LOG(SCEIO, "sceIoChangeAsyncPriority : Illegal Priority %i", priority);
		return SCE_KERNEL_ERROR_ILLEGAL_PRIORITY;
	}
	ERROR_LOG(SCEIO, "UNIMPL sceIoChangeAsyncPriority(%d, %d)", id, priority);
	return 0;
}

int sceIoCloseAsync(int id)
{
	DEBUG_LOG(SCEIO, "sceIoCloseAsync(%d)", id);
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f)
	{
		f->closePending = true;
		f->asyncResult = 0;
		// TODO: Rough estimate.
		__IoSchedAsync(f, id, 100);
		return 0;
	}
	else
		return error;
}

u32 sceIoSetAsyncCallback(int id, u32 clbckId, u32 clbckArg)
{
	DEBUG_LOG(SCEIO, "sceIoSetAsyncCallback(%d, %i, %08x)", id, clbckId, clbckArg);

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

u32 sceIoOpenAsync(const char *filename, int flags, int mode)
{
	// TOOD: Use an internal method so as not to pollute the log?
	// Intentionally does not work when interrupts disabled.
	if (!__KernelIsDispatchEnabled())
		sceKernelResumeDispatchThread(1);

	int error;
	FileNode *f = __IoOpen(error, filename, flags, mode);
	int fd;

	// We have to return an fd here, which may have been destroyed when we reach Wait if it failed.
	if (f == NULL)
	{
		ERROR_LOG(SCEIO, "ERROR_ERRNO_FILE_NOT_FOUND=sceIoOpenAsync(%s, %08x, %08x) - file not found", filename, flags, mode);

		f = new FileNode();
		f->handle = kernelObjects.Create(f);
		f->fullpath = filename;
		f->asyncResult = error == 0 ? ERROR_ERRNO_FILE_NOT_FOUND : error;
		f->closePending = true;

		fd = __IoAllocFd(f);
	}
	else
	{
		fd = __IoAllocFd(f);
		if (fd >= 0) {
			DEBUG_LOG(SCEIO, "%x=sceIoOpenAsync(%s, %08x, %08x)", fd, filename, flags, mode);
			f->asyncResult = fd;
		}
	}

	if (fd < 0) {
		ERROR_LOG(SCEIO, "%08x=sceIoOpenAsync(%s, %08x, %08x): out of fds", fd, filename, flags, mode);
		kernelObjects.Destroy<FileNode>(f->GetUID());
		return fd;
	}

	// TODO: Timing is very inconsistent.  From ms0, 10ms - 20ms depending on filesize/dir depth?  From umd, can take > 1s.
	// For now let's aim low.
	__IoSchedAsync(f, fd, 100);

	return fd;
}

u32 sceIoGetAsyncStat(int id, u32 poll, u32 address) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (__IsInInterrupt()) {
			DEBUG_LOG(SCEIO, "%lli = sceIoGetAsyncStat(%i, %i, %08x): illegal context", f->asyncResult, id, poll, address);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
		if (f->pendingAsyncResult) {
			if (poll) {
				DEBUG_LOG(SCEIO, "%lli = sceIoGetAsyncStat(%i, %i, %08x): not ready", f->asyncResult, id, poll, address);
				return 1;
			} else {
				if (!__KernelIsDispatchEnabled()) {
					DEBUG_LOG(SCEIO, "%lli = sceIoGetAsyncStat(%i, %i, %08x): dispatch disabled", f->asyncResult, id, poll, address);
					return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
				}

				DEBUG_LOG(SCEIO, "%lli = sceIoGetAsyncStat(%i, %i, %08x): waiting", f->asyncResult, id, poll, address);
				f->waitingThreads.push_back(__KernelGetCurThread());
				__KernelWaitCurThread(WAITTYPE_ASYNCIO, f->GetUID(), address, 0, false, "io waited");
			}
		} else if (f->hasAsyncResult) {
			if (!__KernelIsDispatchEnabled()) {
				DEBUG_LOG(SCEIO, "%lli = sceIoGetAsyncStat(%i, %i, %08x): dispatch disabled", f->asyncResult, id, poll, address);
				return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
			}

			DEBUG_LOG(SCEIO, "%lli = sceIoGetAsyncStat(%i, %i, %08x)", f->asyncResult, id, poll, address);
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}
		} else {
			WARN_LOG(SCEIO, "SCE_KERNEL_ERROR_NOASYNC = sceIoGetAsyncStat(%i, %i, %08x)", id, poll, address);
			return SCE_KERNEL_ERROR_NOASYNC;
		}
		return 0; //completed
	}
	else
	{
		ERROR_LOG(SCEIO, "ERROR - sceIoGetAsyncStat with invalid id %i", id);
		return SCE_KERNEL_ERROR_BADF;
	}
}

int sceIoWaitAsync(int id, u32 address) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (__IsInInterrupt()) {
			DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsync(%i, %08x): illegal context", f->asyncResult, id, address);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
		if (f->pendingAsyncResult) {
			if (!__KernelIsDispatchEnabled()) {
				DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsync(%i, %08x): dispatch disabled", f->asyncResult, id, address);
				return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
			}
			DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsync(%i, %08x): waiting", f->asyncResult, id, address);
			f->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitCurThread(WAITTYPE_ASYNCIO, f->GetUID(), address, 0, false, "io waited");
		} else if (f->hasAsyncResult) {
			if (!__KernelIsDispatchEnabled()) {
				DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsync(%i, %08x): dispatch disabled", f->asyncResult, id, address);
				return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
			}
			DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsync(%i, %08x)", f->asyncResult, id, address);
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}
		} else {
			WARN_LOG(SCEIO, "SCE_KERNEL_ERROR_NOASYNC = sceIoWaitAsync(%i, %08x)", id, address);
			return SCE_KERNEL_ERROR_NOASYNC;
		}
		return 0; //completed
	} else {
		ERROR_LOG(SCEIO, "ERROR - sceIoWaitAsync waiting for invalid id %i", id);
		return SCE_KERNEL_ERROR_BADF;
	}
}

int sceIoWaitAsyncCB(int id, u32 address) {
	// Should process callbacks here
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (__IsInInterrupt()) {
			DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsyncCB(%i, %08x): illegal context", f->asyncResult, id, address);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}

		hleCheckCurrentCallbacks();
		if (f->pendingAsyncResult) {
			DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsyncCB(%i, %08x): waiting", f->asyncResult, id, address);
			// TODO: This seems to re-enable dispatch or something?
			f->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitCurThread(WAITTYPE_ASYNCIO, f->GetUID(), address, 0, false, "io waited");
		} else if (f->hasAsyncResult) {
			DEBUG_LOG(SCEIO, "%lli = sceIoWaitAsyncCB(%i, %08x)", f->asyncResult, id, address);
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}
		} else {
			WARN_LOG(SCEIO, "SCE_KERNEL_ERROR_NOASYNC = sceIoWaitAsyncCB(%i, %08x)", id, address);
			return SCE_KERNEL_ERROR_NOASYNC;
		}
		return 0; //completed
	} else {
		ERROR_LOG(SCEIO, "ERROR - sceIoWaitAsyncCB waiting for invalid id %i", id);
		return SCE_KERNEL_ERROR_BADF;
	}
}

u32 sceIoPollAsync(int id, u32 address) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->pendingAsyncResult) {
			DEBUG_LOG(SCEIO, "%lli = sceIoPollAsync(%i, %08x): not ready", f->asyncResult, id, address);
			return 1;
		} else if (f->hasAsyncResult) {
			DEBUG_LOG(SCEIO, "%lli = sceIoPollAsync(%i, %08x)", f->asyncResult, id, address);
			Memory::Write_U64((u64) f->asyncResult, address);
			f->hasAsyncResult = false;

			if (f->closePending) {
				__IoFreeFd(id, error);
			}
			return 0; //completed
		} else {
			// This seems to be normal-ish usage so demoted to DEBUG from WARN.
			DEBUG_LOG(SCEIO, "SCE_KERNEL_ERROR_NOASYNC = sceIoPollAsync(%i, %08x)", id, address);
			return SCE_KERNEL_ERROR_NOASYNC;
		}
	} else {
		ERROR_LOG(SCEIO, "ERROR - sceIoPollAsync waiting for invalid id %i", id);
		return SCE_KERNEL_ERROR_BADF;
	}
}

class DirListing : public KernelObject {
public:
	const char *GetName() {return name.c_str();}
	const char *GetTypeName() {return "DirListing";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_DirList; }
	int GetIDType() const { return PPSSPP_KERNEL_TMID_DirList; }

	virtual void DoState(PointerWrap &p) {
		auto s = p.Section("DirListing", 1);
		if (!s)
			return;

		p.Do(name);
		p.Do(index);

		// TODO: Is this the right way for it to wake up?
		int count = (int) listing.size();
		p.Do(count);
		listing.resize(count);
		for (int i = 0; i < count; ++i) {
			listing[i].DoState(p);
		}
	}

	std::string name;
	std::vector<PSPFileInfo> listing;
	int index;
};

u32 sceIoDopen(const char *path) {
	DEBUG_LOG(SCEIO, "sceIoDopen(\"%s\")", path);

	if(!pspFileSystem.GetFileInfo(path).exists)
	{
		return ERROR_ERRNO_FILE_NOT_FOUND;
	}

	DirListing *dir = new DirListing();
	SceUID id = kernelObjects.Create(dir);

	dir->listing = pspFileSystem.GetDirListing(path);
	dir->index = 0;
	dir->name = std::string(path);

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

u32 sceIoDread(int id, u32 dirent_addr) {
	u32 error;
	DirListing *dir = kernelObjects.Get<DirListing>(id, error);
	if (dir) {
		SceIoDirEnt *entry = (SceIoDirEnt*) Memory::GetPointer(dirent_addr);

		if (dir->index == (int) dir->listing.size()) {
			DEBUG_LOG(SCEIO, "sceIoDread( %d %08x ) - end of the line", id, dirent_addr);
			entry->d_name[0] = '\0';
			return 0;
		}

		PSPFileInfo &info = dir->listing[dir->index];
		__IoGetStat(&entry->d_stat, info);

		strncpy(entry->d_name, info.name.c_str(), 256);
		entry->d_name[255] = '\0';
		
		bool isFAT = false;
		IFileSystem *sys = pspFileSystem.GetSystemFromFilename(dir->name);
		if (sys && (sys->Flags() & FILESYSTEM_SIMULATE_FAT32))
			isFAT = true;
		else
			isFAT = false;

		// Only write d_private for memory stick
		if (isFAT) {
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
		DEBUG_LOG(SCEIO, "sceIoDread( %d %08x ) = %s", id, dirent_addr, entry->d_name);

		// TODO: Improve timing.  Only happens on the *first* entry read, ms and umd.
		if (dir->index++ == 0) {
			return hleDelayResult(1, "readdir", 1000);
		}
		return 1;
	} else {
		DEBUG_LOG(SCEIO, "sceIoDread - invalid listing %i, error %08x", id, error);
		return SCE_KERNEL_ERROR_BADF;
	}
}

u32 sceIoDclose(int id) {
	DEBUG_LOG(SCEIO, "sceIoDclose(%d)", id);
	return kernelObjects.Destroy<DirListing>(id);
}

int __IoIoctl(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (error) {
		ERROR_LOG(SCEIO, "%08x=sceIoIoctl id: %08x, cmd %08x, bad file", error, id, cmd);
		return error;
	}
	if (f->asyncBusy()) {
		ERROR_LOG(SCEIO, "%08x=sceIoIoctl id: %08x, cmd %08x, async busy", error, id, cmd);
		return SCE_KERNEL_ERROR_ASYNC_BUSY;
	}

	// TODO: Move this into each command, probably?
	usec = 100;

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
			memcpy(keybuf, Memory::GetPointer(indataPtr), 16);
			key_ptr = keybuf;
		}else{
			key_ptr = NULL;
		}

		DEBUG_LOG(SCEIO, "Decrypting PGD DRM files");
		pspFileSystem.SeekFile(f->handle, (s32)f->pgd_offset, FILEMOVE_BEGIN);
		pspFileSystem.ReadFile(f->handle, pgd_header, 0x90);
		f->pgdInfo = pgd_open(pgd_header, 2, key_ptr);
		if(f->pgdInfo==NULL){
			ERROR_LOG(SCEIO, "Not a valid PGD file. Open as normal file.");
			f->npdrm = false;
			pspFileSystem.SeekFile(f->handle, (s32)0, FILEMOVE_BEGIN);
			if(memcmp(pgd_header, pgd_magic, 4)==0){
				// File is PGD file, but key mismatch
				return ERROR_PGD_INVALID_HEADER;
			}else{
				// File is decrypted.
				return 0;
			}
		}else{
			// Everthing OK.
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
			return (int)f->info.size;
		break;

	// Get UMD sector size
	case 0x01020003:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(SCEIO, "sceIoIoctl: Asked for sector size of file %i", id);
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
		INFO_LOG(SCEIO, "sceIoIoctl: Asked for file offset of file %i", id);
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
		INFO_LOG(SCEIO, "sceIoIoctl: Seek for file %i", id);
		// Even if the size is 4, it still actually reads a 16 byte struct, it seems.
		if (Memory::IsValidAddress(indataPtr) && inlen >= 4) {
			struct SeekInfo {
				u64 offset;
				u32 unk;
				u32 whence;
			};
			const auto seekInfo = PSPPointer<SeekInfo>::Create(indataPtr);
			FileMove seek;
			s64 newPos = __IoLseekDest(f, seekInfo->offset, seekInfo->whence, seek);
			if (newPos < 0 || newPos > f->info.size) {
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
		INFO_LOG(SCEIO, "sceIoIoctl: Asked for start sector of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 4) {
			Memory::Write_U32(f->info.startSector, outdataPtr);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Get UMD file size in bytes.
	case 0x01020007:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(SCEIO, "sceIoIoctl: Asked for size of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 8) {
			Memory::Write_U64(f->info.size, outdataPtr);
		} else {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		break;

	// Read from UMD file.
	case 0x01030008:
		// TODO: Should not work for umd0:/, ms0:/, etc.
		// TODO: Should probably move this to something common between ISOFileSystem and VirtualDiscSystem.
		INFO_LOG(SCEIO, "sceIoIoctl: Read from file %i", id);
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
		INFO_LOG(SCEIO, "sceIoIoctl: Sector tell from file %i", id);
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
		INFO_LOG(SCEIO, "sceIoIoctl: Sector read from file %i", id);
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
		INFO_LOG(SCEIO, "sceIoIoctl: Sector seek for file %i", id);
		// Even if the size is 4, it still actually reads a 16 byte struct, it seems.
		if (Memory::IsValidAddress(indataPtr) && inlen >= 4) {
			struct SeekInfo {
				u64 offset;
				u32 unk;
				u32 whence;
			};
			const auto seekInfo = PSPPointer<SeekInfo>::Create(indataPtr);
			FileMove seek;
			s64 newPos = __IoLseekDest(f, seekInfo->offset, seekInfo->whence, seek);
			// Position is in sectors, don't forget.
			if (newPos < 0 || newPos > f->info.size) {
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
				sprintf(temp, "sceIoIoctl(%%s, %08x, %%08x, %%x, %%08x, %%x)", cmd);
				Reporting::ReportMessage(temp, f->fullpath.c_str(), indataPtr, inlen, outdataPtr, outlen);
				ERROR_LOG(SCEIO, "UNIMPL 0=sceIoIoctl id: %08x, cmd %08x, indataPtr %08x, inlen %08x, outdataPtr %08x, outLen %08x", id,cmd,indataPtr,inlen,outdataPtr,outlen);
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

u32 sceIoIoctlAsync(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen)
{
	u32 error;
	FileNode *f = __IoGetFd(id, error);
	if (f) {
		if (f->asyncBusy()) {
			WARN_LOG(SCEIO, "sceIoIoctlAsync(%08x, %08x, %08x, %08x, %08x, %08x): async busy", id, cmd, indataPtr, inlen, outdataPtr, outlen);
			return SCE_KERNEL_ERROR_ASYNC_BUSY;
		}
		DEBUG_LOG(SCEIO, "sceIoIoctlAsync(%08x, %08x, %08x, %08x, %08x, %08x)", id, cmd, indataPtr, inlen, outdataPtr, outlen);
		int usec = 100;
		f->asyncResult = __IoIoctl(id, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
		__IoSchedAsync(f, id, usec);
		return 0;
	} else {
		ERROR_LOG(SCEIO, "UNIMPL %08x=sceIoIoctlAsync id: %08x, cmd %08x, bad file", error, id, cmd);
		return error;
	}
}

u32 sceIoGetFdList(u32 outAddr, int outSize, u32 fdNumAddr) {
	WARN_LOG(SCEIO, "sceIoGetFdList(%08x, %i, %08x)", outAddr, outSize, fdNumAddr);

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
u32 sceKernelRegisterStderrPipe(u32 msgPipeUID) {
	ERROR_LOG_REPORT(SCEIO, "UNIMPL sceKernelRegisterStderrPipe(%08x)", msgPipeUID);
	return 0;
}

u32 sceKernelRegisterStdoutPipe(u32 msgPipeUID) {
	ERROR_LOG_REPORT(SCEIO, "UNIMPL sceKernelRegisterStdoutPipe(%08x)", msgPipeUID);
	return 0;
}

KernelObject *__KernelFileNodeObject() {
	return new FileNode;
}

KernelObject *__KernelDirListingObject() {
	return new DirListing;
}

const HLEFunction IoFileMgrForUser[] = {
	{ 0xb29ddf9c, &WrapU_C<sceIoDopen>, "sceIoDopen" },
	{ 0xe3eb004c, &WrapU_IU<sceIoDread>, "sceIoDread" },
	{ 0xeb092469, &WrapU_I<sceIoDclose>, "sceIoDclose" },
	{ 0xe95a012b, &WrapU_UUUUUU<sceIoIoctlAsync>, "sceIoIoctlAsync" },
	{ 0x63632449, &WrapU_UUUUUU<sceIoIoctl>, "sceIoIoctl" },
	{ 0xace946e8, &WrapU_CU<sceIoGetstat>, "sceIoGetstat" },
	{ 0xb8a740f4, &WrapU_CUU<sceIoChstat>, "sceIoChstat" },
	{ 0x55f4717d, &WrapU_C<sceIoChdir>, "sceIoChdir" },
	{ 0x08bd7374, &WrapU_I<sceIoGetDevType>, "sceIoGetDevType" },
	{ 0xB2A628C1, &WrapU_UUUIUI<sceIoAssign>, "sceIoAssign" },
	{ 0xe8bc6571, &WrapU_I<sceIoCancel>, "sceIoCancel" },
	{ 0xb293727f, &WrapI_II<sceIoChangeAsyncPriority>, "sceIoChangeAsyncPriority" },
	{ 0x810C4BC3, &WrapU_I<sceIoClose>, "sceIoClose" }, //(int fd);
	{ 0xff5940b6, &WrapI_I<sceIoCloseAsync>, "sceIoCloseAsync" },
	{ 0x54F5FB11, &WrapU_CIUIUI<sceIoDevctl>, "sceIoDevctl" }, //(const char *name int cmd, void *arg, size_t arglen, void *buf, size_t *buflen);
	{ 0xcb05f8d6, &WrapU_IUU<sceIoGetAsyncStat>, "sceIoGetAsyncStat" },
	{ 0x27EB27B8, &WrapI64_II64I<sceIoLseek>, "sceIoLseek" }, //(int fd, int offset, int whence);
	{ 0x68963324, &WrapU_III<sceIoLseek32>, "sceIoLseek32" },
	{ 0x1b385d8f, &WrapU_III<sceIoLseek32Async>, "sceIoLseek32Async" },
	{ 0x71b19e77, &WrapU_II64I<sceIoLseekAsync>, "sceIoLseekAsync" },
	{ 0x109F50BC, &WrapU_CII<sceIoOpen>, "sceIoOpen" }, //(const char* file, int mode);
	{ 0x89AA9906, &WrapU_CII<sceIoOpenAsync>, "sceIoOpenAsync" },
	{ 0x06A70004, &WrapU_CI<sceIoMkdir>, "sceIoMkdir" }, //(const char *dir, int mode);
	{ 0x3251ea56, &WrapU_IU<sceIoPollAsync>, "sceIoPollAsync" },
	{ 0x6A638D83, &WrapU_IUI<sceIoRead>, "sceIoRead" }, //(int fd, void *data, int size);
	{ 0xa0b5a7c2, &WrapU_IUI<sceIoReadAsync>, "sceIoReadAsync" },
	{ 0xF27A9C51, &WrapU_C<sceIoRemove>, "sceIoRemove" }, //(const char *file);
	{ 0x779103A0, &WrapU_CC<sceIoRename>, "sceIoRename" }, //(const char *oldname, const char *newname);
	{ 0x1117C65F, &WrapU_C<sceIoRmdir>, "sceIoRmdir" }, //(const char *dir);
	{ 0xA12A0514, &WrapU_IUU<sceIoSetAsyncCallback>, "sceIoSetAsyncCallback" },
	{ 0xab96437f, &WrapU_CI<sceIoSync>, "sceIoSync" },
	{ 0x6d08a871, &WrapU_C<sceIoUnassign>, "sceIoUnassign" },
	{ 0x42EC03AC, &WrapU_IUI<sceIoWrite>, "sceIoWrite" }, //(int fd, void *data, int size);
	{ 0x0facab19, &WrapU_IUI<sceIoWriteAsync>, "sceIoWriteAsync" },
	{ 0x35dbd746, &WrapI_IU<sceIoWaitAsyncCB>, "sceIoWaitAsyncCB" },
	{ 0xe23eec33, &WrapI_IU<sceIoWaitAsync>, "sceIoWaitAsync" },
	{ 0x5C2BE2CC, &WrapU_UIU<sceIoGetFdList>, "sceIoGetFdList"},
};

void Register_IoFileMgrForUser() {
	RegisterModule("IoFileMgrForUser", ARRAY_SIZE(IoFileMgrForUser), IoFileMgrForUser);
}


const HLEFunction StdioForUser[] = {
	{ 0x172D316E, &WrapU_V<sceKernelStdin>, "sceKernelStdin" },
	{ 0xA6BAB2E9, &WrapU_V<sceKernelStdout>, "sceKernelStdout" },
	{ 0xF78BA90A, &WrapU_V<sceKernelStderr>, "sceKernelStderr" },
	{ 0x432D8F5C, &WrapU_U<sceKernelRegisterStdoutPipe>, "sceKernelRegisterStdoutPipe" },
	{ 0x6F797E03, &WrapU_U<sceKernelRegisterStderrPipe>, "sceKernelRegisterStderrPipe" },
	{ 0xa46785c9, 0, "sceKernelStdioSendChar" },
	{ 0x0cbb0571, 0, "sceKernelStdioLseek" },
	{ 0x3054d478, 0, "sceKernelStdioRead" },
	{ 0xa3b931db, 0, "sceKernelStdioWrite" },
	{ 0x924aba61, 0, "sceKernelStdioOpen" },
	{ 0x9d061c19, 0, "sceKernelStdioClose" },
};

void Register_StdioForUser() {
	RegisterModule("StdioForUser", ARRAY_SIZE(StdioForUser), StdioForUser);
}
