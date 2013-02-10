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

#ifdef _WIN32
#include <windows.h>
#endif

#include "../Config.h"
#include "../Host.h"
#include "../SaveState.h"
#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../HW/MemoryStick.h"

#include "../FileSystems/FileSystem.h"
#include "../FileSystems/MetaFileSystem.h"
#include "../FileSystems/ISOFileSystem.h"
#include "../FileSystems/DirectoryFileSystem.h"

#include "sceIo.h"
#include "sceRtc.h"
#include "sceKernel.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"

// For headless screenshots.
#include "sceDisplay.h"

#define ERROR_ERRNO_FILE_NOT_FOUND               0x80010002

#define ERROR_MEMSTICK_DEVCTL_BAD_PARAMS         0x80220081
#define ERROR_MEMSTICK_DEVCTL_TOO_MANY_CALLBACKS 0x80220082
#define ERROR_KERNEL_BAD_FILE_DESCRIPTOR		 0x80020323

#define PSP_DEV_TYPE_ALIAS 0x20

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

#define O_RDONLY		0x0001
#define O_WRONLY		0x0002
#define O_RDWR			0x0003
#define O_NBLOCK		0x0010
#define O_APPEND		0x0100
#define O_CREAT			0x0200
#define O_TRUNC			0x0400
#define O_NOWAIT		0x8000


typedef s32 SceMode;
typedef s64 SceOff;
typedef u64 SceIores;

typedef u32 (*DeferredAction)(SceUID id, int param);
DeferredAction defAction = 0;
u32 defParam = 0;

#define SCE_STM_FDIR 0x1000
#define SCE_STM_FREG 0x2000
#define SCE_STM_FLNK 0x4000

enum {
	TYPE_DIR=0x10,
	TYPE_FILE=0x20
};

#ifdef __SYMBIAN32__
#undef st_ctime
#undef st_atime
#undef st_mtime
#endif

struct SceIoStat {
	SceMode st_mode;
	unsigned int st_attr;
	SceOff st_size;
	ScePspDateTime st_ctime;
	ScePspDateTime st_atime;
	ScePspDateTime st_mtime;
	unsigned int st_private[6];
};

struct SceIoDirEnt {
	SceIoStat d_stat;
	char d_name[256];
	u32 d_private;
};
#ifndef __SYMBIAN32__
struct dirent {
	u32 unk0;
	u32 type;
	u32 size;
	u32 unk[19];
	char name[0x108];
};
#endif

class FileNode : public KernelObject {
public:
	FileNode() : callbackID(0), callbackArg(0), asyncResult(0), closePending(false), pendingAsyncResult(false), sectorBlockMode(false) {}
	~FileNode() {
		pspFileSystem.CloseFile(handle);
	}
	const char *GetName() {return fullpath.c_str();}
	const char *GetTypeName() {return "OpenFile";}
	void GetQuickInfo(char *ptr, int size) {
		sprintf(ptr, "Seekpos: %08x", (u32)pspFileSystem.GetSeekPos(handle));
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	int GetIDType() const { return PPSSPP_KERNEL_TMID_File; }

	virtual void DoState(PointerWrap &p) {
		p.Do(fullpath);
		p.Do(handle);
		p.Do(callbackID);
		p.Do(callbackArg);
		p.Do(asyncResult);
		p.Do(pendingAsyncResult);
		p.Do(sectorBlockMode);
		p.Do(closePending);
		p.Do(info);
		p.Do(openMode);
		p.DoMarker("File");
	}

	std::string fullpath;
	u32 handle;

	u32 callbackID;
	u32 callbackArg;

	u32 asyncResult;

	bool pendingAsyncResult;
	bool sectorBlockMode;
	bool closePending;

	PSPFileInfo info;
	u32 openMode;
};

static void TellFsThreadEnded (SceUID threadID) {
	pspFileSystem.ThreadEnded(threadID);
}

void __IoInit() {
	INFO_LOG(HLE, "Starting up I/O...");

	MemoryStick_SetFatState(PSP_FAT_MEMORYSTICK_STATE_ASSIGNED);

#ifdef _WIN32

	char path_buffer[_MAX_PATH], drive[_MAX_DRIVE] ,dir[_MAX_DIR], file[_MAX_FNAME], ext[_MAX_EXT];
	char memstickpath[_MAX_PATH];
	char flashpath[_MAX_PATH];

	GetModuleFileName(NULL,path_buffer,sizeof(path_buffer));

	char *winpos = strstr(path_buffer, "Windows");
	if (winpos)
	*winpos = 0;
	strcat(path_buffer, "dummy.txt");

	_splitpath_s(path_buffer, drive, dir, file, ext );

	// Mount a couple of filesystems
	sprintf(memstickpath, "%s%sMemStick\\", drive, dir);
	sprintf(flashpath, "%s%sFlash\\", drive, dir);

#else
	// TODO
	std::string memstickpath = g_Config.memCardDirectory;
	std::string flashpath = g_Config.flashDirectory;
#endif

	DirectoryFileSystem *memstick;
	DirectoryFileSystem *flash;

	memstick = new DirectoryFileSystem(&pspFileSystem, memstickpath);
	flash = new DirectoryFileSystem(&pspFileSystem, flashpath);
	pspFileSystem.Mount("ms0:", memstick);
	pspFileSystem.Mount("fatms0:", memstick);
	pspFileSystem.Mount("fatms:", memstick);
	pspFileSystem.Mount("flash0:", flash);
	pspFileSystem.Mount("flash1:", flash);
	
	__KernelListenThreadEnd(&TellFsThreadEnded);
}

void __IoDoState(PointerWrap &p) {
	// TODO: defAction is hard to save, and not the right way anyway.
	// Should probbly be an enum and on the FileNode anyway.
	if (defAction != NULL) {
		WARN_LOG(HLE, "FIXME: Savestate failure: deferred IO not saved yet.");
	}
}

void __IoShutdown() {
	defAction = 0;
	defParam = 0;
}

u32 __IoGetFileHandleFromId(u32 id, u32 &outError)
{
	FileNode *f = kernelObjects.Get < FileNode > (id, outError);
	if (!f) {
		return -1;
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
			perm = "unhandled " + mode;
			break;
	}
	DEBUG_LOG(HLE, "sceIoAssign(%s, %s, %s, %s, %08x, %i)", alias.c_str(), physical_dev.c_str(), filesystem_dev.c_str(), perm.c_str(), arg_addr, argSize);
	return 0;
}

u32 sceIoUnassign(const char *alias)  
{
	DEBUG_LOG(HLE, "sceIoUnassign(%s)", alias);
	return 0;
}

u32 sceKernelStdin() {
	DEBUG_LOG(HLE, "3=sceKernelStdin()");
	return 3;
}

u32 sceKernelStdout() {
	DEBUG_LOG(HLE, "1=sceKernelStdout()");
	return 1;
}

u32 sceKernelStderr() {
	DEBUG_LOG(HLE, "2=sceKernelStderr()");
	return 2;
}

void __IoCompleteAsyncIO(SceUID id) {
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		if (f->callbackID) {
			__KernelNotifyCallback(THREAD_CALLBACK_IO, f->callbackID, f->callbackArg);
		}
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
	__IoCopyDate(stat->st_atime, info.atime);
	__IoCopyDate(stat->st_ctime, info.ctime);
	__IoCopyDate(stat->st_mtime, info.mtime);
	stat->st_private[0] = info.startSector;
}

u32 sceIoGetstat(const char *filename, u32 addr) {
	SceIoStat stat;
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	if (info.exists) {
		__IoGetStat(&stat, info);
		if (Memory::IsValidAddress(addr)) {
			Memory::WriteStruct(addr, &stat);
			DEBUG_LOG(HLE, "sceIoGetstat(%s, %08x) : sector = %08x", filename, addr, info.startSector);
			return 0;
		} else {
			ERROR_LOG(HLE, "sceIoGetstat(%s, %08x) : bad address", filename, addr);
			return -1;
		}
	} else {
		DEBUG_LOG(HLE, "sceIoGetstat(%s, %08x) : FILE NOT FOUND", filename, addr);
		return ERROR_ERRNO_FILE_NOT_FOUND;
	}
}

//Not sure about wrapping it or not, since the log seems to take the address of the data var
u32 sceIoRead(int id, u32 data_addr, int size) {
	if (id == 3) {
		DEBUG_LOG(HLE, "sceIoRead STDIN");
		return 0; //stdin
	}

	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		if(!(f->openMode & FILEACCESS_READ))
		{
			return ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
		}
		else if (Memory::IsValidAddress(data_addr)) {
			u8 *data = (u8*) Memory::GetPointer(data_addr);
			f->asyncResult = (u32) pspFileSystem.ReadFile(f->handle, data, size);
			DEBUG_LOG(HLE, "%i=sceIoRead(%d, %08x , %i)", f->asyncResult, id, data_addr, size);
			return f->asyncResult;
		} else {
			ERROR_LOG(HLE, "sceIoRead Reading into bad pointer %08x", data_addr);
			return -1;
		}
	} else {
		ERROR_LOG(HLE, "sceIoRead ERROR: no file open");
		return error;
	}
}

u32 sceIoWrite(int id, void *data_ptr, int size) 
{
	if (id == 2) {
		//stderr!
		const char *str = (const char*) data_ptr;
		INFO_LOG(HLE, "stderr: %s", str);
		return size;
	} else if (id == 1) {
		//stdout!
		char *str = (char *) data_ptr;
		char temp = str[size];
		str[size] = 0;
		INFO_LOG(HLE, "stdout: %s", str);
		str[size] = temp;
		return size;
	}
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		if(!(f->openMode & FILEACCESS_WRITE))
		{
			return ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
		}
		else
		{
			u8 *data = (u8*) data_ptr;
			f->asyncResult = (u32) pspFileSystem.WriteFile(f->handle, data, size);
			return f->asyncResult;
		}
	} else {
		ERROR_LOG(HLE, "sceIoWrite ERROR: no file open");
		return error;
	}
}

u32 sceIoWriteAsync(int id, void *data_ptr, int size) 
{
	DEBUG_LOG(HLE, "sceIoWriteAsync(%d)", id);
	sceIoWrite(id, data_ptr, size);
	__IoCompleteAsyncIO(id);
	return 0;
}

u32 sceIoGetDevType(int id) 
{
	ERROR_LOG(HLE, "UNIMPL sceIoGetDevType(%d)", id);
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	int result;
	if (f) 
		result = PSP_DEV_TYPE_ALIAS;
	else {
		ERROR_LOG(HLE, "sceIoGetDevType: unknown id %d", id);
		result = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
	}

	return result;
}

u32 sceIoCancel(int id) 
{
	ERROR_LOG(HLE, "UNIMPL sceIoCancel(%d)", id);
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		f->closePending = true;
	} else {
		ERROR_LOG(HLE, "sceIoCancel: unknown id %d", id);
		error = ERROR_KERNEL_BAD_FILE_DESCRIPTOR;
	}

	return error;
}

s64 sceIoLseek(int id, s64 offset, int whence) {
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		FileMove seek = FILEMOVE_BEGIN;
		bool outOfBound = false;
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
		}
		if(newPos < 0)
			return -1;

		f->asyncResult = (u32) pspFileSystem.SeekFile(f->handle, (s32) offset, seek);
		DEBUG_LOG(HLE, "%i = sceIoLseek(%d,%i,%i)", f->asyncResult, id, (int) offset, whence);
		return f->asyncResult;
	} else {
		ERROR_LOG(HLE, "sceIoLseek(%d, %i, %i) - ERROR: invalid file", id, (int) offset, whence);
		return error;
	}
}

u32 sceIoLseek32(int id, int offset, int whence) {
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		FileMove seek = FILEMOVE_BEGIN;
		bool outOfBound = false;
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
		}
		if(newPos < 0)
			return -1;

		f->asyncResult = (u32) pspFileSystem.SeekFile(f->handle, (s32) offset, seek);
		DEBUG_LOG(HLE, "%i = sceIoLseek32(%d,%i,%i)", f->asyncResult, id, (int) offset, whence);
		return f->asyncResult;
	} else {
		ERROR_LOG(HLE, "sceIoLseek32(%d, %i, %i) - ERROR: invalid file", id, (int) offset, whence);
		return error;
	}
}

u32 sceIoOpen(const char* filename, int flags, int mode) {
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

	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	u32 h = pspFileSystem.OpenFile(filename, (FileAccess) access);
	if (h == 0)
	{
		ERROR_LOG(HLE,
				"ERROR_ERRNO_FILE_NOT_FOUND=sceIoOpen(%s, %08x, %08x) - file not found", filename, flags, mode);
		return ERROR_ERRNO_FILE_NOT_FOUND;
	}

	FileNode *f = new FileNode();
	SceUID id = kernelObjects.Create(f);
	f->handle = h;
	f->fullpath = filename;
	f->asyncResult = id;
	f->info = info;
	f->openMode = access;
	DEBUG_LOG(HLE, "%i=sceIoOpen(%s, %08x, %08x)", id, filename, flags, mode);
	return id;
}

u32 sceIoClose(int id) {
	DEBUG_LOG(HLE, "sceIoClose(%d)", id);
	return kernelObjects.Destroy < FileNode > (id);
}

u32 sceIoRemove(const char *filename) {
	DEBUG_LOG(HLE, "sceIoRemove(%s)", filename);

	if(!pspFileSystem.GetFileInfo(filename).exists)
		return ERROR_ERRNO_FILE_NOT_FOUND;

	pspFileSystem.RemoveFile(filename);
	return 0;
}

u32 sceIoMkdir(const char *dirname, int mode) {
	DEBUG_LOG(HLE, "sceIoMkdir(%s, %i)", dirname, mode);
	if (pspFileSystem.MkDir(dirname))
		return 0;
	else
		return -1;
}

u32 sceIoRmdir(const char *dirname) {
	DEBUG_LOG(HLE, "sceIoRmdir(%s)", dirname);
	if (pspFileSystem.RmDir(dirname))
		return 0;
	else
		return -1;
}

u32 sceIoSync(const char *devicename, int flag) {
	DEBUG_LOG(HLE, "UNIMPL sceIoSync(%s, %i)", devicename, flag);
	return 0;
}

struct DeviceSize {
	u32 maxClusters;
	u32 freeClusters;
	u32 maxSectors;
	u32 sectorSize;
	u32 sectorCount;
};

u32 sceIoDevctl(const char *name, int cmd, u32 argAddr, int argLen, u32 outPtr, int outLen) {
	if (strcmp(name, "emulator:")) {
		DEBUG_LOG(HLE,"sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd, argAddr, argLen, outPtr, outLen);
	}

	// UMD checks
	switch (cmd) {
	case 0x01F20001:  // Get Disc Type.
		if (Memory::IsValidAddress(outPtr)) {
			Memory::Write_U32(0x10, outPtr);  // Game disc
			return 0;
		} else {
			return -1;
		}
		break;
	case 0x01F20002:  // Get current LBA.
		if (Memory::IsValidAddress(outPtr)) {
			Memory::Write_U32(0, outPtr);  // Game disc
			return 0;
		} else {
			return -1;
		}
		break;
	case 0x01F100A3:  // Seek
		return 0;
	}

	// This should really send it on to a FileSystem implementation instead.

	if (!strcmp(name, "mscmhc0:") || !strcmp(name, "ms0:"))
	{
		switch (cmd)
		{
		// does one of these set a callback as well? (see coded arms)
		case 0x02015804:	// Register callback
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				u32 cbId = Memory::Read_U32(argAddr);
				if (0 == __KernelRegisterCallback(THREAD_CALLBACK_MEMORYSTICK, cbId)) {
					DEBUG_LOG(HLE, "sceIoDevCtl: Memstick callback %i registered, notifying immediately.", cbId);
					__KernelNotifyCallbackType(THREAD_CALLBACK_MEMORYSTICK, cbId, MemoryStick_State());
					return 0;
				} else {
					return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
				}
			}
			break;

		case 0x02025805:	// Unregister callback
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				u32 cbId = Memory::Read_U32(argAddr);
				if (0 == __KernelUnregisterCallback(THREAD_CALLBACK_MEMORYSTICK, cbId)) {
					DEBUG_LOG(HLE, "sceIoDevCtl: Unregistered memstick callback %i", cbId);
					return 0;
				} else {
					return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
				}
			}
			break;

		case 0x02025801:	// Memstick Driver status?
			if (Memory::IsValidAddress(outPtr) && outLen >= 4) {
				Memory::Write_U32(4, outPtr);  // JPSCP: The right return value is 4 for some reason
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}

		case 0x02025806:	// Memory stick inserted?
			if (Memory::IsValidAddress(outPtr) && outLen >= 4) {
				Memory::Write_U32(1, outPtr);
				return 0;
			} else {
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}

		case 0x02425818:  // Get memstick size etc
			// Pretend we have a 2GB memory stick.
			if (Memory::IsValidAddress(argAddr) && argLen >= 4) {  // "Should" be outPtr but isn't
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
				DEBUG_LOG(HLE, "Returned memstick size: maxSectors=%i", deviceSize.maxSectors);
				return 0;
			} else {
				ERROR_LOG(HLE, "memstick size query: bad params");
				return ERROR_MEMSTICK_DEVCTL_BAD_PARAMS;
			}
		}
	}

	if (!strcmp(name, "fatms0:"))
	{
		switch (cmd) {
		case 0x02415821:  // MScmRegisterMSInsertEjectCallback
			{
				u32 cbId = Memory::Read_U32(argAddr);
				if (0 == __KernelRegisterCallback(THREAD_CALLBACK_MEMORYSTICK_FAT, cbId)) {
					DEBUG_LOG(HLE, "sceIoDevCtl: Memstick FAT callback %i registered, notifying immediately.", cbId);
					__KernelNotifyCallbackType(THREAD_CALLBACK_MEMORYSTICK_FAT, cbId, MemoryStick_FatState());
					return 0;
				} else {
					return -1;
				}
			}
			break;
		case 0x02415822: // MScmUnregisterMSInsertEjectCallback
			{
				u32 cbId = Memory::Read_U32(argAddr);
				if (0 == __KernelUnregisterCallback(THREAD_CALLBACK_MEMORYSTICK_FAT, cbId)) {
					DEBUG_LOG(HLE, "sceIoDevCtl: Unregistered memstick FAT callback %i", cbId);
					return 0;
				} else {
					return -1;
				}
			}

		case 0x02415823:  // Set FAT as enabled
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				MemoryStick_SetFatState((MemStickFatState)Memory::Read_U32(argAddr));
				return 0;
			} else {
				ERROR_LOG(HLE, "Failed 0x02415823 fat");
				return -1;
			}
			break;

		case 0x02425823:  // Check if FAT enabled
			if (Memory::IsValidAddress(outPtr) && outLen == 4) {
				Memory::Write_U32(MemoryStick_FatState(), outPtr);
				return 0;
			} else {
				ERROR_LOG(HLE, "Failed 0x02425823 fat");
				return -1;
			}
			break;

		case 0x02425818:  // Get memstick size etc
			// Pretend we have a 2GB memory stick.
			{
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
			}
		}
	}

	if (!strcmp(name, "kemulator:") || !strcmp(name, "emulator:"))
	{
		// Emulator special tricks!
		switch (cmd)
		{
		case 1:	// EMULATOR_DEVCTL__GET_HAS_DISPLAY
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(0, outPtr);	 // TODO: Make a headless mode for running tests!
			return 0;
		case 2:	// EMULATOR_DEVCTL__SEND_OUTPUT
			{
				std::string data(Memory::GetCharPointer(argAddr), argLen);
				if (PSP_CoreParameter().printfEmuLog)
				{
					host->SendDebugOutput(data.c_str());
				}
				else
				{
					DEBUG_LOG(HLE, "%s", data.c_str());
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

		ERROR_LOG(HLE, "sceIoDevCtl: UNKNOWN PARAMETERS");

		return 0;
	}

	//089c6d1c weird branch
	/*
	089c6bdc ]: HLE: sceKernelCreateCallback(name= MemoryStick Detection ,entry= 089c7484 ) (z_un_089c6bc4)
	089c6c18 ]: HLE: sceIoDevctl("mscmhc0:", 02015804, 09ffb9c0, 4, 00000000, 0) (z_un_089c6bc4)
	089c6c40 ]: HLE: sceKernelCreateCallback(name= MemoryStick Assignment ,entry= 089c7534 ) (z_un_089c6bc4)
	089c6c78 ]: HLE: sceIoDevctl("fatms0:", 02415821, 09ffb9c4, 4, 00000000, 0) (z_un_089c6bc4)
	089c6cac ]: HLE: sceIoDevctl("mscmhc0:", 02025806, 00000000, 0, 09ffb9c8, 4) (z_un_089c6bc4)
	*/
	return SCE_KERNEL_ERROR_UNSUP;
}

u32 sceIoRename(const char *from, const char *to) {
	DEBUG_LOG(HLE, "sceIoRename(%s, %s)", from, to);

	if(!pspFileSystem.GetFileInfo(from).exists)
		return ERROR_ERRNO_FILE_NOT_FOUND;

	if(!pspFileSystem.RenameFile(from, to))
		WARN_LOG(HLE, "Could not move %s to %s\n",from, to);
	return 0;
}

u32 sceIoChdir(const char *dirname) {
	DEBUG_LOG(HLE, "sceIoChdir(%s)", dirname);
	pspFileSystem.ChDir(dirname);
	return 0;
}

int sceIoChangeAsyncPriority(int id, int priority)
{
	ERROR_LOG(HLE, "UNIMPL sceIoChangeAsyncPriority(%d, %d)", id, priority);
	return 0;
}

u32 __IoClose(SceUID actedFd, int closedFd)
{
	DEBUG_LOG(HLE, "Deferred IoClose(%d, %d)", actedFd, closedFd);
	__IoCompleteAsyncIO(closedFd);
	return kernelObjects.Destroy < FileNode > (closedFd);
}

int sceIoCloseAsync(int id)
{
	DEBUG_LOG(HLE, "sceIoCloseAsync(%d)", id);
	//sceIoClose();
	// TODO: Not sure this is a good solution.  Seems like you can defer one per fd.
	defAction = &__IoClose;
	defParam = id;
	return 0;
}

u32 sceIoLseekAsync(int id, s64 offset, int whence)
{
	DEBUG_LOG(HLE, "sceIoLseekAsync(%d) sorta implemented", id);
	sceIoLseek(id, offset, whence);
	__IoCompleteAsyncIO(id);
	return 0;
}

u32 sceIoSetAsyncCallback(int id, u32 clbckId, u32 clbckArg)
{
	DEBUG_LOG(HLE, "sceIoSetAsyncCallback(%d, %i, %08x)", id, clbckId, clbckArg);

	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f)
	{
		// TODO: Check replacing / updating?
		f->callbackID = clbckId;
		f->callbackArg = clbckArg;
		return 0;
	}
	else
	{
		return error;
	}
}

u32 sceIoLseek32Async(int id, int offset, int whence)
{
	DEBUG_LOG(HLE, "sceIoLseek32Async(%d) sorta implemented", id);
	sceIoLseek32(id, offset, whence);
	__IoCompleteAsyncIO(id);
	return 0;
}

u32 sceIoOpenAsync(const char *filename, int flags, int mode)
{
	DEBUG_LOG(HLE, "sceIoOpenAsync() sorta implemented");
	u32 fd = sceIoOpen(filename, flags, mode);
	// TODO: This can't actually have a callback yet, but if it's set before waiting, it should be called.
	__IoCompleteAsyncIO(fd);
	// We have to return an fd here, which may have been destroyed when we reach Wait if it failed.
	return fd;
}

u32 sceIoReadAsync(int id, u32 data_addr, int size)
{
	DEBUG_LOG(HLE, "sceIoReadAsync(%d)", id);
	sceIoRead(id, data_addr, size);
	__IoCompleteAsyncIO(id);
	return 0;
}

u32 sceIoGetAsyncStat(int id, u32 poll, u32 address)
{
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f)
	{
		Memory::Write_U64(f->asyncResult, address);
		DEBUG_LOG(HLE, "%i = sceIoGetAsyncStat(%i, %i, %08x) (HACK)", (u32) f->asyncResult, id, poll, address);
		if (!poll)
			hleReSchedule("io waited");
		return 0; //completed
	}
	else
	{
		ERROR_LOG(HLE, "ERROR - sceIoGetAsyncStat with invalid id %i", id);
		return -1;
	}
}

int sceIoWaitAsync(int id, u32 address) {
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		u64 res = f->asyncResult;
		if (defAction) {
			res = defAction(id, defParam);
			defAction = 0;
		}
		Memory::Write_U64(res, address);
		DEBUG_LOG(HLE, "%i = sceIoWaitAsync(%i, %08x) (HACK)", (u32) res, id, address);
		hleReSchedule("io waited");
		return 0; //completed
	} else {
		ERROR_LOG(HLE, "ERROR - sceIoWaitAsync waiting for invalid id %i", id);
		return -1;
	}
}

int sceIoWaitAsyncCB(int id, u32 address) {
	// Should process callbacks here
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		u64 res = f->asyncResult;
		if (defAction) {
			res = defAction(id, defParam);
			defAction = 0;
		}
		Memory::Write_U64(res, address);
		DEBUG_LOG(HLE, "%i = sceIoWaitAsyncCB(%i, %08x) (HACK)", (u32) res, id,	address);
		hleCheckCurrentCallbacks();
		hleReSchedule(true, "io waited");
		return 0; //completed
	} else {
		ERROR_LOG(HLE, "ERROR - sceIoWaitAsyncCB waiting for invalid id %i", id);
		return -1;
	}
}

u32 sceIoPollAsync(int id, u32 address) {
	u32 error;
	FileNode *f = kernelObjects.Get < FileNode > (id, error);
	if (f) {
		u64 res = f->asyncResult;
		if (defAction) {
			res = defAction(id, defParam);
			defAction = 0;
		}
		Memory::Write_U64(res, address);
		DEBUG_LOG(HLE, "%i = sceIoPollAsync(%i, %08x) (HACK)", (u32) res, id, address);
		return 0; //completed
	} else {
		ERROR_LOG(HLE, "ERROR - sceIoPollAsync waiting for invalid id %i", id);
		return -1;  // TODO: correct error code
	}
}

class DirListing : public KernelObject {
public:
	const char *GetName() {return name.c_str();}
	const char *GetTypeName() {return "DirListing";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	int GetIDType() const { return PPSSPP_KERNEL_TMID_DirList; }

	virtual void DoState(PointerWrap &p) {
		p.Do(name);
		p.Do(index);

		// TODO: Is this the right way for it to wake up?
		int count = (int) listing.size();
		p.Do(count);
		listing.resize(count);
		for (int i = 0; i < count; ++i) {
			listing[i].DoState(p);
		}
		p.DoMarker("DirListing");
	}

	std::string name;
	std::vector<PSPFileInfo> listing;
	int index;
};

u32 sceIoDopen(const char *path) {
	DEBUG_LOG(HLE, "sceIoDopen(\"%s\")", path);


	if(!pspFileSystem.GetFileInfo(path).exists)
	{
		return ERROR_ERRNO_FILE_NOT_FOUND;
	}

	DirListing *dir = new DirListing();
	SceUID id = kernelObjects.Create(dir);

	dir->listing = pspFileSystem.GetDirListing(path);
	dir->index = 0;
	dir->name = std::string(path);

	return id;
}

u32 sceIoDread(int id, u32 dirent_addr) {
	u32 error;
	DirListing *dir = kernelObjects.Get<DirListing>(id, error);
	if (dir) {
		SceIoDirEnt *entry = (SceIoDirEnt*) Memory::GetPointer(dirent_addr);

		if (dir->index == (int) dir->listing.size()) {
			DEBUG_LOG(HLE, "sceIoDread( %d %08x ) - end of the line", id, dirent_addr);
			entry->d_name[0] = '\0';
			return 0;
		}

		PSPFileInfo &info = dir->listing[dir->index];
		__IoGetStat(&entry->d_stat, info);

		strncpy(entry->d_name, info.name.c_str(), 256);
		entry->d_name[255] = '\0';
		entry->d_private = 0xC0DEBABE;
		DEBUG_LOG(HLE, "sceIoDread( %d %08x ) = %s", id, dirent_addr, entry->d_name);

		dir->index++;
		return 1;
	} else {
		DEBUG_LOG(HLE, "sceIoDread - invalid listing %i, error %08x", id, error);
		return SCE_KERNEL_ERROR_BADF;
	}
}

u32 sceIoDclose(int id) {
	DEBUG_LOG(HLE, "sceIoDclose(%d)", id);
	return kernelObjects.Destroy<DirListing>(id);
}

u32 sceIoIoctl(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen) 
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (error) {
		return error;
	}

	//KD Hearts:
	//56:46:434 HLE\sceIo.cpp:886 E[HLE]: UNIMPL 0=sceIoIoctrl id: 0000011f, cmd 04100001, indataPtr 08b313d8, inlen 00000010, outdataPtr 00000000, outLen 0
	//	0000000

	// TODO: This kind of stuff should be moved to the devices (wherever that would be)
	// and does not belong in this file. Same thing with Devctl.

	switch (cmd) {
	// Define decryption key (amctrl.prx DRM)
	case 0x04100001:  
		if (Memory::IsValidAddress(indataPtr) && inlen == 16) {
			u8 keybuf[16];
			memcpy(keybuf, Memory::GetPointer(indataPtr), 16);
			ERROR_LOG(HLE, "PGD DRM not yet supported, sorry.");
		}
		break;

	// Get UMD sector size
	case 0x01020003:
		INFO_LOG(HLE, "sceIoIoCtl: Asked for sector size of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen == 4) {
			Memory::Write_U32(f->info.sectorSize, outdataPtr);
		}
		break;

	// Get UMD file pointer
	case 0x01020004:
		INFO_LOG(HLE, "sceIoIoCtl: Asked for fpointer of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 4) {
			Memory::Write_U32(f->info.fpointer, outdataPtr);
		}
		break;

	// Get UMD file start sector .
	case 0x01020006:
		INFO_LOG(HLE, "sceIoIoCtl: Asked for start sector of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 4) {
			Memory::Write_U32(f->info.startSector, outdataPtr);
		}
		break;

	// Get UMD file size in bytes.
	case 0x01020007:
		INFO_LOG(HLE, "sceIoIoCtl: Asked for size of file %i", id);
		if (Memory::IsValidAddress(outdataPtr) && outlen >= 8) {
			Memory::Write_U64(f->info.size, outdataPtr);
		}
		break;

	default:
		ERROR_LOG(HLE, "UNIMPL 0=sceIoIoctl id: %08x, cmd %08x, indataPtr %08x, inlen %08x, outdataPtr %08x, outLen %08x", id,cmd,indataPtr,inlen,outdataPtr,outlen);
		break;
	}

  return 0;
}

u32 sceIoIoctlAsync(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen)
{
	DEBUG_LOG(HLE, "sceIoIoctlAsync(%08x, %08x, %08x, %08x, %08x, %08x)", id, cmd, indataPtr, inlen, outdataPtr, outlen);
	sceIoIoctl(id, cmd, indataPtr, inlen, outdataPtr, outlen);
	__IoCompleteAsyncIO(id);
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
	{ 0xb8a740f4, 0, "sceIoChstat" },
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
	{ 0x42EC03AC, &WrapU_IVI<sceIoWrite>, "sceIoWrite" }, //(int fd, void *data, int size);
	{ 0x0facab19, &WrapU_IVI<sceIoWriteAsync>, "sceIoWriteAsync" },
	{ 0x35dbd746, &WrapI_IU<sceIoWaitAsyncCB>, "sceIoWaitAsyncCB" },
	{ 0xe23eec33, &WrapI_IU<sceIoWaitAsync>, "sceIoWaitAsync" },
};

void Register_IoFileMgrForUser() {
	RegisterModule("IoFileMgrForUser", ARRAY_SIZE(IoFileMgrForUser), IoFileMgrForUser);
}


const HLEFunction StdioForUser[] = {
	{ 0x172D316E, &WrapU_V<sceKernelStdin>, "sceKernelStdin" },
	{ 0xA6BAB2E9, &WrapU_V<sceKernelStdout>, "sceKernelStdout" },
	{ 0xF78BA90A, &WrapU_V<sceKernelStderr>, "sceKernelStderr" }, 
};

void Register_StdioForUser() {
	RegisterModule("StdioForUser", ARRAY_SIZE(StdioForUser), StdioForUser);
}
