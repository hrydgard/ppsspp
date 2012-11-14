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
#undef DeleteFile
#endif

#include "../System.h"
#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../HW/MemoryStick.h"

#include "../FileSystems/FileSystem.h"
#include "../FileSystems/MetaFileSystem.h"
#include "../FileSystems/ISOFileSystem.h"
#include "../FileSystems/DirectoryFileSystem.h"

#include "sceIo.h"
#include "sceKernel.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"

#define ERROR_ERRNO_FILE_NOT_FOUND													0x80010002


#define ERROR_MEMSTICK_DEVCTL_BAD_PARAMS                    0x80220081
#define ERROR_MEMSTICK_DEVCTL_TOO_MANY_CALLBACKS            0x80220082

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
#define O_CREAT		 0x0200
#define O_TRUNC		 0x0400
#define O_NOWAIT		0x8000


typedef s32 SceMode;
typedef s64 SceOff;
typedef u64 SceIores;

std::string emuDebugOutput;

const std::string &EmuDebugOutput() {
	return emuDebugOutput;
}

#define SCE_STM_FDIR 0x1000
#define SCE_STM_FREG 0x2000
#define SCE_STM_FLNK 0x4000
enum
{
	TYPE_DIR=0x10, 
	TYPE_FILE=0x20 
};

struct ScePspDateTime {
	unsigned short year;
	unsigned short month;
	unsigned short day;
	unsigned short hour;
	unsigned short minute;
	unsigned short second;
	unsigned int microsecond;
};

struct SceIoStat {
	SceMode st_mode;
	unsigned int st_attr;
	SceOff st_size;
	ScePspDateTime st_ctime;
	ScePspDateTime st_atime;
	ScePspDateTime st_mtime;
	unsigned int st_private[6];
};

struct SceIoDirEnt
{
	SceIoStat d_stat;
	char d_name[256];
	u32 d_private;
};

struct dirent { 
	u32 unk0; 
	u32 type; 
	u32 size; 
	u32 unk[19]; 
	char name[0x108]; 
};

class FileNode : public KernelObject
{
public:
	FileNode() : callbackID(0), callbackArg(0), asyncResult(0), pendingAsyncResult(false), sectorBlockMode(false) {}
	~FileNode()
	{
		pspFileSystem.CloseFile(handle);
	}
	const char *GetName() {return fullpath.c_str();}
	const char *GetTypeName() {return "OpenFile";}
	void GetQuickInfo(char *ptr, int size)
	{
		sprintf(ptr, "Seekpos: %08x", (u32)pspFileSystem.GetSeekPos(handle));
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	int GetIDType() const { return 0; }

	std::string fullpath;
	u32 handle;

	u32 callbackID;
	u32 callbackArg;

	u32 asyncResult;

	bool pendingAsyncResult;
	bool sectorBlockMode;
};

void __IoInit()
{
	INFO_LOG(HLE, "Starting up I/O...");

#ifdef _WIN32

	char path_buffer[_MAX_PATH], drive[_MAX_DRIVE] ,dir[_MAX_DIR], file[_MAX_FNAME], ext[_MAX_EXT];
	char mypath[_MAX_PATH];

	GetModuleFileName(NULL,path_buffer,sizeof(path_buffer));

	char *winpos = strstr(path_buffer, "Windows");
	if (winpos)
		*winpos = 0;
	strcat(path_buffer, "dummy.txt");

	_splitpath_s(path_buffer, drive, dir, file, ext );

	// Mount a couple of filesystems
	sprintf(mypath, "%s%sMemStick\\", drive, dir);
#else
	// TODO
	char mypath[256] = "/mount/sdcard/memstick";
#endif

	DirectoryFileSystem *memstick;
	memstick = new DirectoryFileSystem(&pspFileSystem, mypath);

	pspFileSystem.Mount("ms0:",	memstick);
	pspFileSystem.Mount("fatms0:", memstick);
	pspFileSystem.Mount("fatms:", memstick);
	pspFileSystem.Mount("flash0:", new EmptyFileSystem());
	pspFileSystem.Mount("flash1:", new EmptyFileSystem());
}

void __IoShutdown()
{

}

void sceIoAssign()
{
	const char *aliasname = Memory::GetCharPointer(PARAM(0));
	const char *physname = Memory::GetCharPointer(PARAM(1));
	const char *devname = Memory::GetCharPointer(PARAM(2));
	u32 flag = PARAM(3);
	ERROR_LOG(HLE,"UNIMPL sceIoAssign(%s, %s, %s, %08x, ...)",aliasname,physname,devname,flag);
	RETURN(0);
}

void sceKernelStdin()
{
	DEBUG_LOG(HLE,"3=sceKernelStdin()");
	RETURN(3);
}
void sceKernelStdout()
{
	DEBUG_LOG(HLE,"1=sceKernelStdout()");
	RETURN(1);
}

void sceKernelStderr()
{
	DEBUG_LOG(HLE,"2=sceKernelStderr()");
	RETURN(2);
}

void __IoCompleteAsyncIO(SceUID id)
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		if (f->callbackID)
		{
			// __KernelNotifyCallbackType(THREAD_CALLBACK_IO, __KernelGetCurThread(), f->callbackID, f->callbackArg);
		}
	}
}

void __IoGetStat(SceIoStat *stat, PSPFileInfo &info)
{
	memset(stat, 0xfe, sizeof(SceIoStat));
	stat->st_size = (s64)info.size;

	int type, attr;
	if (info.type & FILETYPE_DIRECTORY)
		type = SCE_STM_FDIR, attr = TYPE_DIR;
	else
		type = SCE_STM_FREG, attr = TYPE_FILE;

	stat->st_mode = type; //0777 | type;
	stat->st_attr = attr;
	stat->st_size = info.size;
	stat->st_private[0] = info.startSector;
}


void sceIoGetstat()
{
	const char *filename = Memory::GetCharPointer(PARAM(0));
	u32 addr = PARAM(1);

	SceIoStat *stat = (SceIoStat*)Memory::GetPointer(addr);
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	__IoGetStat(stat, info);
	DEBUG_LOG(HLE,"sceIoGetstat(%s, %08x) : sector = %08x",filename,addr,info.startSector);

	RETURN(0);
}

void sceIoRead()	 //(int fd, void *data, int size);
{
	SceUID id = PARAM(0);
	if (id == 3)
	{
		DEBUG_LOG(HLE,"sceIoRead STDIN");
		RETURN(0); //stdin
		return;
	}

	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		if (PARAM(1))
		{
			u8 *data = (u8*)Memory::GetPointer(PARAM(1));
			int size = PARAM(2);
			f->asyncResult = RETURN((u32)pspFileSystem.ReadFile(f->handle, data, size));
			DEBUG_LOG(HLE,"%i=sceIoRead(%d, %08x , %i)",f->asyncResult, id, PARAM(1), size);
		}
		else
		{
			ERROR_LOG(HLE,"sceIoRead Reading into zero pointer");
			RETURN(-1);
		}
	}
	else
	{
		ERROR_LOG(HLE,"sceIoRead ERROR: no file open");
		RETURN(error);
	}
}

void sceIoWrite()	//(int fd, void *data, int size);
{
	SceUID id = PARAM(0);
	int size = PARAM(2);
	if (PARAM(0) == 2)
	{
		//stderr!
		const char *str = Memory::GetCharPointer(PARAM(1));
		DEBUG_LOG(HLE,"stderr: %s", str);
		RETURN(size);
		return;
	}
	if (PARAM(0) == 1)
	{
		//stdout!
		char *str = (char *)Memory::GetPointer(PARAM(1));
		char temp = str[size];
		str[size]=0;
		DEBUG_LOG(HLE,"stdout: %s", str);
		str[size]=temp;
		RETURN(size);
		return;
	}
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		u8 *data = (u8*)Memory::GetPointer(PARAM(1));
		f->asyncResult = RETURN((u32)pspFileSystem.WriteFile(f->handle,data,size));
	}
	else
	{
		ERROR_LOG(HLE,"sceIoWrite ERROR: no file open");
		RETURN(error);
	}
}

void sceIoLseek() //(int fd, int64 offset, int whence);
{
	SceUID id = PARAM(0);
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	s64 offset = ((s64)PARAM(2)) | ((s64)(PARAM(3))<<32);
	int whence = PARAM(4);
	if (f)
	{
		FileMove seek = FILEMOVE_BEGIN;
		switch (whence)
		{
		case 0: break;
		case 1: seek=FILEMOVE_CURRENT;break;
		case 2: seek=FILEMOVE_END;break;
		}

		f->asyncResult = RETURN((u32)pspFileSystem.SeekFile(f->handle, (s32)offset, seek));
		DEBUG_LOG(HLE,"%i = sceIoLseek(%d,%i,%i)",f->asyncResult, id,(int)offset,whence);
	}
	else
	{
		ERROR_LOG(HLE,"sceIoLseek(%d, %i, %i) - ERROR: invalid file", id, (int)offset, whence);
		RETURN(error);
	}
}

void sceIoLseek32() //(int fd, int offset, int whence); 
{
	SceUID id = PARAM(0);
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		s32 offset = (s32)PARAM(2);
		int whence = PARAM(3);
		DEBUG_LOG(HLE,"sceIoLseek32(%d,%08x,%i)",id,(int)offset,whence);

		FileMove seek=FILEMOVE_BEGIN;
		switch (whence)
		{
		case 0: break;
		case 1: seek=FILEMOVE_CURRENT;break;
		case 2: seek=FILEMOVE_END;break;
		}

		f->asyncResult = RETURN((u32)pspFileSystem.SeekFile(f->handle, (s32)offset, seek));
	}
	else
	{
		ERROR_LOG(HLE,"sceIoLseek32 ERROR: no file open");
		RETURN(error);
	}
}

// Try WrapI_CU
void sceIoOpen()	 //(const char* file, int mode);
{
	const char *filename = Memory::GetCharPointer(PARAM(0));
	int mode = PARAM(1);

	//memory stick filename
	int access=FILEACCESS_NONE;
	if (mode & O_RDONLY) access |= FILEACCESS_READ;
	if (mode & O_WRONLY) access |= FILEACCESS_WRITE;
	if (mode & O_APPEND) access |= FILEACCESS_APPEND;
	if (mode & O_CREAT)	access |= FILEACCESS_CREATE;

	u32 h = pspFileSystem.OpenFile(filename, (FileAccess)access);
	if (h == 0)
	{
		ERROR_LOG(HLE,"ERROR_ERRNO_FILE_NOT_FOUND=sceIoOpen(%s, %08x) - file not found", filename, mode);
		RETURN(ERROR_ERRNO_FILE_NOT_FOUND);
		return;
	}

	FileNode *f = new FileNode();
	SceUID id = kernelObjects.Create(f);
	f->handle = h;
	f->fullpath = filename;
	f->asyncResult = id;
	DEBUG_LOG(HLE,"%i=sceIoOpen(%s, %08x)",id,filename,mode);
	RETURN(id);
}

void sceIoClose()	//(int fd);
{
	SceUID f = PARAM(0);
	DEBUG_LOG(HLE,"sceIoClose(%d)",f);
	RETURN(kernelObjects.Destroy<FileNode>(f));
}

void sceIoRemove() //(const char *file);
{
	const char *filename = Memory::GetCharPointer(PARAM(0));
	if (pspFileSystem.DeleteFile(filename))
		RETURN(0);
	else
		RETURN(-1);
	DEBUG_LOG(HLE,"sceIoRemove(%s)", filename);
}

void sceIoMkdir()	//(const char *dir, int mode); 
{
	const char *filename = Memory::GetCharPointer(PARAM(0));
	int mode = PARAM(1);
	if (pspFileSystem.MkDir(filename))
		RETURN(0);
	else
		RETURN(-1);
	DEBUG_LOG(HLE,"sceIoMkdir(%s, %i)", filename, mode);
}

void sceIoRmdir()	//(const char *dir); 
{
	const char *filename = Memory::GetCharPointer(PARAM(0));
	if (pspFileSystem.RmDir(filename))
		RETURN(0);
	else
		RETURN(-1);
	DEBUG_LOG(HLE,"sceIoRmdir(%s)", filename);
}

void sceIoSync()
{
	DEBUG_LOG(HLE,"UNIMPL sceIoSync not implemented");
}

struct DeviceSize
{
	u32 maxSectors;
	u32 sectorSize;
	u32 sectorsPerCluster;
	u32 totalClusters;
	u32 freeClusters;	
};

void sceIoDevctl() //(const char *name, int cmd, void *arg, size_t arglen, void *buf, size_t *buflen); 
{
	const char *name = Memory::GetCharPointer(PARAM(0));
	int cmd = PARAM(1);
	u32 argAddr = PARAM(2);
	int argLen = PARAM(3);
	u32 outPtr = PARAM(4);
	int outLen = PARAM(5);

	if (strcmp(name, "emulator:")) {
		DEBUG_LOG(HLE,"sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", name, cmd,argAddr,argLen,outPtr,outLen);
	}

	// UMD checks
	switch (cmd) {
	case 0x01F20001:  // Get Disc Type.
		if (Memory::IsValidAddress(outPtr)) {
			Memory::Write_U32(0x10, outPtr);  // Game disc
			RETURN(0); return;
		} else {
			RETURN(-1); return;
		}
		break;
	case 0x01F20002:  // Get current LBA.
		if (Memory::IsValidAddress(outPtr)) {
			Memory::Write_U32(0, outPtr);  // Game disc
			RETURN(0); return;
		} else {
			RETURN(-1); return;
		}
		break;
	case 0x01F100A3:  // Seek
		RETURN(0); return;
		break;
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
					RETURN(0);
				} else {
					RETURN(ERROR_MEMSTICK_DEVCTL_BAD_PARAMS);
				}
				return;
			}
			break;

		case 0x02025805:	// Unregister callback
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				u32 cbId = Memory::Read_U32(argAddr);
				if (0 == __KernelUnregisterCallback(THREAD_CALLBACK_MEMORYSTICK, cbId)) {
					DEBUG_LOG(HLE, "sceIoDevCtl: Unregistered memstick callback %i", cbId);
					RETURN(0);
				} else {
					RETURN(ERROR_MEMSTICK_DEVCTL_BAD_PARAMS);
				}
				return;
			}
			break;

		case 0x02025806:	// Memory stick inserted?
		case 0x02025801:	// Memstick Driver status?
			if (Memory::IsValidAddress(outPtr)) {
				Memory::Write_U32(1, outPtr);
				RETURN(0);
			} else {
				RETURN(ERROR_MEMSTICK_DEVCTL_BAD_PARAMS);
			}
			return;

		case 0x02425818:  // Get memstick size etc
			// Pretend we have a 2GB memory stick.
			if (Memory::IsValidAddress(argAddr)) {  // "Should" be outPtr but isn't
				u32 pointer = Memory::Read_U32(argAddr);

				u64 totalSize = (u32)2 * 1024 * 1024 * 1024;
				u64 freeSize	= 1 * 1024 * 1024 * 1024;
				DeviceSize deviceSize;
				deviceSize.maxSectors				= 512;
				deviceSize.sectorSize				= 0x200;
				deviceSize.sectorsPerCluster = 0x08;
				deviceSize.totalClusters		 = (u32)((totalSize * 95 / 100) / (deviceSize.sectorSize * deviceSize.sectorsPerCluster));
				deviceSize.freeClusters			= (u32)((freeSize	* 95 / 100) / (deviceSize.sectorSize * deviceSize.sectorsPerCluster));
				Memory::WriteStruct(pointer, &deviceSize);
				RETURN(0);
			} else {
				RETURN(ERROR_MEMSTICK_DEVCTL_BAD_PARAMS);
			}
			return;
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
					RETURN(0);
				} else {
					RETURN(-1);
				}
				return;
			}
			break;
		case 0x02415822: // MScmUnregisterMSInsertEjectCallback
			{
				u32 cbId = Memory::Read_U32(argAddr);
				if (0 == __KernelUnregisterCallback(THREAD_CALLBACK_MEMORYSTICK_FAT, cbId)) {
					DEBUG_LOG(HLE, "sceIoDevCtl: Unregistered memstick FAT callback %i", cbId);
					RETURN(0);
				} else {
					RETURN(-1);
				}
				return;
			}

		case 0x02415823:  // Set FAT as enabled
			if (Memory::IsValidAddress(argAddr) && argLen == 4) {
				MemoryStick_SetFatState((MemStickFatState)Memory::Read_U32(argAddr));
				RETURN(0);
			} else {
				ERROR_LOG(HLE, "Failed 0x02415823 fat");
				RETURN(-1);
			}
			break;

		case 0x02425823:  // Check if FAT enabled
			if (Memory::IsValidAddress(outPtr) && outLen == 4) {
				Memory::Write_U32(MemoryStick_FatState(), outPtr);
				RETURN(0);
			} else {
				ERROR_LOG(HLE, "Failed 0x02425823 fat");
				RETURN(-1);
			}
			break;

		case 0x02425818:  // Get memstick size etc
			// Pretend we have a 2GB memory stick.
			{
				if (Memory::IsValidAddress(argAddr)) {  // "Should" be outPtr but isn't
					u32 pointer = Memory::Read_U32(argAddr);

					u64 totalSize = (u32)2 * 1024 * 1024 * 1024;
					u64 freeSize	= 1 * 1024 * 1024 * 1024;
					DeviceSize deviceSize;
					deviceSize.maxSectors				= 512;
					deviceSize.sectorSize				= 0x200;
					deviceSize.sectorsPerCluster = 0x08;
					deviceSize.totalClusters		 = (u32)((totalSize * 95 / 100) / (deviceSize.sectorSize * deviceSize.sectorsPerCluster));
					deviceSize.freeClusters			= (u32)((freeSize	* 95 / 100) / (deviceSize.sectorSize * deviceSize.sectorsPerCluster));
					Memory::WriteStruct(pointer, &deviceSize);
					RETURN(0);
				} else {
					RETURN(ERROR_MEMSTICK_DEVCTL_BAD_PARAMS);
				}
				return;
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
			RETURN(0);
			return;
		case 2:	// EMULATOR_DEVCTL__SEND_OUTPUT
			{
				std::string data(Memory::GetCharPointer(argAddr), argLen);
				if (PSP_CoreParameter().printfEmuLog)
				{
					printf("%s", data.c_str());
#ifdef _WIN32
					OutputDebugString(data.c_str());
#endif
					// Also collect the debug output
					emuDebugOutput += data;
				}
				else
				{
					DEBUG_LOG(HLE, "%s", data.c_str());
				}
				RETURN(0);
				return;
			}
		case 3:	// EMULATOR_DEVCTL__IS_EMULATOR
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(1, outPtr);	 // TODO: Make a headless mode for running tests!
			RETURN(0);
			return;
		}

		ERROR_LOG(HLE, "sceIoDevCtl: UNKNOWN PARAMETERS");
		
		RETURN(0);
		return;
	}

	//089c6d1c weird branch
	/*
	089c6bdc ]: HLE: sceKernelCreateCallback(name= MemoryStick Detection ,entry= 089c7484 ) (z_un_089c6bc4)
	089c6c18 ]: HLE: sceIoDevctl("mscmhc0:", 02015804, 09ffb9c0, 4, 00000000, 0) (z_un_089c6bc4)
	089c6c40 ]: HLE: sceKernelCreateCallback(name= MemoryStick Assignment ,entry= 089c7534 ) (z_un_089c6bc4)
	089c6c78 ]: HLE: sceIoDevctl("fatms0:", 02415821, 09ffb9c4, 4, 00000000, 0) (z_un_089c6bc4)
	089c6cac ]: HLE: sceIoDevctl("mscmhc0:", 02025806, 00000000, 0, 09ffb9c8, 4) (z_un_089c6bc4)
	*/
	RETURN(SCE_KERNEL_ERROR_UNSUP);
}

void sceIoRename() //(const char *oldname, const char *newname); 
{
	const char *from = Memory::GetCharPointer(PARAM(0));
	const char *to = Memory::GetCharPointer(PARAM(1));
	ERROR_LOG(HLE,"UNIMPL sceIoRename(%s, %s)", from, to);
	RETURN(0);
}

void sceIoChdir()
{
	const char *dir = Memory::GetCharPointer(PARAM(0));
	pspFileSystem.ChDir(dir);
	DEBUG_LOG(HLE,"sceIoChdir(%s)",dir);
	RETURN(1);
}

typedef u32 (*DeferredAction)(SceUID id, int param);
DeferredAction defAction = 0;
u32 defParam;

void sceIoChangeAsyncPriority()
{
	ERROR_LOG(HLE,"UNIMPL sceIoChangeAsyncPriority(%d)", PARAM(0));
	RETURN(0);
}

u32 __IoClose(SceUID id, int param)
{
	DEBUG_LOG(HLE,"Deferred IoClose(%d)",id);
	__IoCompleteAsyncIO(id);
	return kernelObjects.Destroy<FileNode>(id);
}

void sceIoCloseAsync()
{
	DEBUG_LOG(HLE,"sceIoCloseAsync(%d)",PARAM(0));
	//sceIoClose();
	defAction = &__IoClose;
	RETURN(0);
}

void sceIoLseekAsync()
{
	sceIoLseek();
	__IoCompleteAsyncIO(PARAM(0));
	RETURN(0);
}

void sceIoSetAsyncCallback()
{
	DEBUG_LOG(HLE,"sceIoSetAsyncCallback(%d, %i, %08x)",PARAM(0), PARAM(1), PARAM(2));

	SceUID id = PARAM(0);
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		f->callbackID = PARAM(1);
		f->callbackArg = PARAM(2);
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}

void sceIoLseek32Async()
{
	DEBUG_LOG(HLE,"sceIoLseek32Async(%d) sorta implemented",PARAM(0));
	sceIoLseek32();
	__IoCompleteAsyncIO(PARAM(0));
	RETURN(0);
}

void sceIoOpenAsync()
{
	DEBUG_LOG(HLE,"sceIoOpenAsync() sorta implemented");
	sceIoOpen();
//	__IoCompleteAsyncIO(currentMIPS->r[2]);	// The return value
	// We have to return a UID here, which may have been destroyed when we reach Wait if it failed.
	// Now that we're just faking it, we just don't RETURN(0) here.
}	

void sceIoReadAsync()
{
	DEBUG_LOG(HLE,"sceIoReadAsync(%d)",PARAM(0));
	sceIoRead();
	__IoCompleteAsyncIO(PARAM(0));
	RETURN(0);
}

void sceIoGetAsyncStat()
{
	SceUID id = PARAM(0);
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(PARAM(2));
		*resPtr = f->asyncResult;
		DEBUG_LOG(HLE,"%i = sceIoGetAsyncStat(%i, %i, %08x) (HACK)", (u32)*resPtr, id, PARAM(1), PARAM(2));
		RETURN(0); //completed
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoGetAsyncStat with invalid id %i", id);
		RETURN(-1);
	}
}


void sceIoWaitAsync()
{
	SceUID id = PARAM(0);
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(PARAM(1));
		*resPtr = f->asyncResult;
		if (defAction)
		{
			*resPtr = defAction(id, defParam);
			defAction = 0;
		}
		DEBUG_LOG(HLE,"%i = sceIoWaitAsync(%i, %08x) (HACK)", (u32)*resPtr, id, PARAM(1));
		RETURN(0); //completed
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoWaitAsync waiting for invalid id %i", id);
		RETURN(-1);
	}
}

void sceIoWaitAsyncCB()
{
	// Should process callbacks here

	SceUID id = PARAM(0);
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(PARAM(1));
		*resPtr = f->asyncResult;
		if (defAction)
		{
			*resPtr = defAction(id, defParam);
			defAction = 0;
		}
		DEBUG_LOG(HLE,"%i = sceIoWaitAsyncCB(%i, %08x) (HACK)", (u32)*resPtr, id, PARAM(1));
		RETURN(0); //completed
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoWaitAsyncCB waiting for invalid id %i", id);
	}
}

void sceIoPollAsync()
{
	SceUID id = PARAM(0);
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(PARAM(1));
		*resPtr = f->asyncResult;
		if (defAction)
		{
			*resPtr = defAction(id, defParam);
			defAction = 0;
		}
		DEBUG_LOG(HLE,"%i = sceIoPollAsync(%i, %08x) (HACK)", (u32)*resPtr, id, PARAM(1));
		RETURN(0); //completed
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoPollAsync waiting for invalid id %i", id);
	}
}

class DirListing : public KernelObject
{
public:
	const char *GetName() {return name.c_str();}
	const char *GetTypeName() {return "DirListing";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_BADF; }
	int GetIDType() const { return 0; }

	std::string name;
	std::vector<PSPFileInfo> listing;
	int index;
};

void sceIoDopen() //(const char *path); 
{
	const char *path = Memory::GetCharPointer(PARAM(0));
	DEBUG_LOG(HLE,"sceIoDopen(\"%s\")",path);

	DirListing *dir = new DirListing();
	SceUID id = kernelObjects.Create(dir);

	// TODO: ERROR_ERRNO_FILE_NOT_FOUND

	dir->listing = pspFileSystem.GetDirListing(path);
	dir->index = 0;
	dir->name = std::string(path);

	RETURN(id);
}

void sceIoDread()
{
	SceUID id = PARAM(0);
	u32 error;
	DirListing *dir = kernelObjects.Get<DirListing>(id, error);
	if (dir)
	{
		if (dir->index == (int)dir->listing.size())
		{
			DEBUG_LOG(HLE,"sceIoDread( %d %08x ) - end of the line", PARAM(0), PARAM(1));
			RETURN(0);
			return;
		}

		PSPFileInfo &info = dir->listing[dir->index];

		SceIoDirEnt *entry = (SceIoDirEnt*)Memory::GetPointer(PARAM(1));

		__IoGetStat(&entry->d_stat, info);

		strncpy(entry->d_name, info.name.c_str(), 256);
		entry->d_private = 0xC0DEBABE;
		DEBUG_LOG(HLE,"sceIoDread( %d %08x ) = %s", PARAM(0), PARAM(1), entry->d_name);

		dir->index++;
		RETURN((u32)(dir->listing.size()-dir->index+1));
	}
	else
	{
		DEBUG_LOG(HLE,"sceIoDread - invalid listing %i, error %08x", id, error);
	}
}

void sceIoDclose()
{
	u32 id = PARAM(0);
	DEBUG_LOG(HLE,"sceIoDclose(%d)",id);
	RETURN(kernelObjects.Destroy<DirListing>(id));
}

const HLEFunction IoFileMgrForUser[] =
{
	{0xb29ddf9c,sceIoDopen, "sceIoDopen"},
	{0xe3eb004c,sceIoDread, "sceIoDread"},
	{0xeb092469,sceIoDclose,"sceIoDclose"},
	{0xe95a012b,0,"sceIoIoctlAsync"},
	{0x63632449,0,"sceIoIoctl"},
	{0xace946e8,sceIoGetstat,"sceIoGetstat"},
	{0xb8a740f4,0,"sceIoChstat"},
	{0x55f4717d,sceIoChdir,"sceIoChdir"},
	{0x08bd7374,0,"sceIoGetDevType"},
	{0xB2A628C1,sceIoAssign,"sceIoAssign"},
	{0xe8bc6571,0,"sceIoCancel"},
	{0xb293727f,sceIoChangeAsyncPriority,"sceIoChangeAsyncPriority"},
	{0x810C4BC3,sceIoClose, "sceIoClose"},	//(int fd); 
	{0xff5940b6,sceIoCloseAsync,"sceIoCloseAsync"},
	{0x54F5FB11,sceIoDevctl,"sceIoDevctl"}, //(const char *name int cmd, void *arg, size_t arglen, void *buf, size_t *buflen); 
	{0xcb05f8d6,sceIoGetAsyncStat,"sceIoGetAsyncStat"},
	{0x27EB27B8,sceIoLseek, "sceIoLseek"},	//(int fd, int offset, int whence); 
	{0x68963324,sceIoLseek32,"sceIoLseek32"},
	{0x1b385d8f,sceIoLseek32Async,"sceIoLseek32Async"},
	{0x71b19e77,sceIoLseekAsync,"sceIoLseekAsync"},
	{0x109F50BC,sceIoOpen,	"sceIoOpen"},	 //(const char* file, int mode); 
	{0x89AA9906,sceIoOpenAsync,"sceIoOpenAsync"},
	{0x06A70004,sceIoMkdir,"sceIoMkdir"},	//(const char *dir, int mode); 
	{0x3251ea56,sceIoPollAsync,"sceIoPollAsync"},
	{0x6A638D83,sceIoRead,	"sceIoRead"},	 //(int fd, void *data, int size); 
	{0xa0b5a7c2,sceIoReadAsync,"sceIoReadAsync"},
	{0xF27A9C51,sceIoRemove,"sceIoRemove"}, //(const char *file); 
	{0x779103A0,sceIoRename,"sceIoRename"}, //(const char *oldname, const char *newname); 
	{0x1117C65F,sceIoRmdir,"sceIoRmdir"},	//(const char *dir); 
	{0xA12A0514,sceIoSetAsyncCallback,"sceIoSetAsyncCallback"},
	{0xab96437f,sceIoSync,"sceIoSync"},
	{0x6d08a871,0,"sceIoUnassign"},
	{0x42EC03AC,sceIoWrite, "sceIoWrite"},	//(int fd, void *data, int size); 
	{0x0facab19,0,"sceIoWriteAsync"},
	{0x35dbd746,sceIoWaitAsyncCB,"sceIoWaitAsyncCB"},
	{0xe23eec33,sceIoWaitAsync,"sceIoWaitAsync"},
};

void Register_IoFileMgrForUser()
{
	RegisterModule("IoFileMgrForUser", ARRAY_SIZE(IoFileMgrForUser), IoFileMgrForUser);
}


const HLEFunction StdioForUser[] = 
{
	{0x172D316E,sceKernelStdin,"sceKernelStdin"},
	{0xA6BAB2E9,sceKernelStdout,"sceKernelStdout"},
	{0xF78BA90A,sceKernelStderr,"sceKernelStderr"},
};

void Register_StdioForUser()
{
	RegisterModule("StdioForUser", ARRAY_SIZE(StdioForUser), StdioForUser);
}
