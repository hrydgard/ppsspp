// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

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

#include "../FileSystems/FileSystem.h"
#include "../FileSystems/MetaFileSystem.h"
#include "../FileSystems/ISOFileSystem.h"
#include "../FileSystems/DirectoryFileSystem.h"

#include "sceIo.h"
#include "sceKernel.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"

#define ERROR_ERRNO_FILE_NOT_FOUND													0x80010002

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
	pspFileSystem.Mount("fatms0:",	memstick);
	pspFileSystem.Mount("fatms:",	 memstick);
	pspFileSystem.Mount("flash0:",	new EmptyFileSystem());
	pspFileSystem.Mount("flash1:",	new EmptyFileSystem());
}

void __IoShutdown()
{

}

u32 sceIoAssign(u32 aliasNameAddr, u32 physNameAddr, u32 devNameAddr, u32 flag, u32, u32)
{
	const char *aliasname = Memory::GetCharPointer(aliasNameAddr);
	const char *physname = Memory::GetCharPointer(physNameAddr);
	const char *devname = Memory::GetCharPointer(devNameAddr);
	ERROR_LOG(HLE,"UNIMPL sceIoAssign(%s, %s, %s, %08x, ...)",aliasname,physname,devname,flag);
	return 0;
}

u32 sceKernelStdin()
{
	DEBUG_LOG(HLE,"3=sceKernelStdin()");
	return 3;
}

u32 sceKernelStdout()
{
	DEBUG_LOG(HLE,"1=sceKernelStdout()");
	return 1;
}

u32 sceKernelStderr()
{
	DEBUG_LOG(HLE,"2=sceKernelStderr()");
	return 2;
}

void __IoCompleteAsyncIO(SceUID id)
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(id, error);
	if (f)
	{
		if (f->callbackID)
		{
			__KernelNotifyCallback(__KernelGetCurThread(), f->callbackID, f->callbackArg);
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

	stat->st_mode = 0777 | type;
	stat->st_attr = attr;
	stat->st_size = info.size;
	stat->st_private[0] = info.startSector;
}


u32 sceIoGetstat(u32 fileAddr, u32 statAddr)
{
	const char *filename = Memory::GetCharPointer(fileAddr);
	DEBUG_LOG(HLE,"sceIoGetstat(%s, %08x)",filename,statAddr);

	SceIoStat *stat = (SceIoStat*)Memory::GetPointer(statAddr);
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	__IoGetStat(stat, info);

    return 0;
}

u32 sceIoRead(SceUID fd, u32 dataAddr, SceSize size)
{
	if (fd == 3)
	{
		DEBUG_LOG(HLE,"sceIoRead STDIN");
		return 0;
	}

	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		if (dataAddr)
		{
			u8 *data = (u8*)Memory::GetPointer(dataAddr);
			f->asyncResult = (u32)pspFileSystem.ReadFile(f->handle, data, size);
			DEBUG_LOG(HLE,"%i=sceIoRead(%d, %08x , %i)",f->asyncResult, fd, dataAddr, size);
			return f->asyncResult;
		}
		else
		{
			ERROR_LOG(HLE,"sceIoRead Reading into zero pointer");
			return -1;
		}
	}
	else
	{
		ERROR_LOG(HLE,"sceIoRead ERROR: no file open");
		return error;
	}
}

u32 sceIoWrite(SceUID fd, u32 dataAddr, SceSize size)
{
	if (fd == 2)
	{
		//stderr!
		const char *str = Memory::GetCharPointer(dataAddr);
		DEBUG_LOG(HLE,"stderr: %s", str);
		return size;
	}
	if (fd == 1)
	{
		//stdout!
		char *str = (char *)Memory::GetPointer(dataAddr);
		char temp = str[size];
		str[size]=0;
		DEBUG_LOG(HLE,"stdout: %s", str);
		str[size]=temp;
		return size;
	}
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		u8 *data = (u8*)Memory::GetPointer(dataAddr);
		return (f->asyncResult = (u32)pspFileSystem.WriteFile(f->handle,data,size));
	}
	else
	{
		ERROR_LOG(HLE,"sceIoWrite ERROR: no file open");
		return error;
	}
}

u64 sceIoLseek(SceUID fd, s64 offset, u32 whence)
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		FileMove seek = FILEMOVE_BEGIN;
		switch (whence)
		{
		case 0: break;
		case 1: seek=FILEMOVE_CURRENT;break;
		case 2: seek=FILEMOVE_END;break;
		}

		f->asyncResult = (u32)pspFileSystem.SeekFile(f->handle, offset, seek);
		DEBUG_LOG(HLE,"%i = sceIoLseek(%d,%lli,%i)",f->asyncResult, fd, offset,whence);
		return f->asyncResult;
	}
	else
	{
		ERROR_LOG(HLE,"sceIoLseek(%d, %lli, %i) - ERROR: invalid file", fd, offset, whence);
		return error;
	}
}

u32 sceIoLseek32(SceUID fd, int offset, u32 whence)
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		DEBUG_LOG(HLE,"sceIoLseek32(%d,%08x,%i)",fd,offset,whence);

		FileMove seek=FILEMOVE_BEGIN;
		switch (whence)
		{
		case 0: break;
		case 1: seek=FILEMOVE_CURRENT;break;
		case 2: seek=FILEMOVE_END;break;
		}

		return (f->asyncResult = (u32)pspFileSystem.SeekFile(f->handle, (s32)offset, seek));
	}
	else
	{
		ERROR_LOG(HLE,"sceIoLseek32 ERROR: no file open");
		return error;
	}
}

u32 sceIoOpen(const char *filename, int flags, SceMode)
{
	//memory stick filename
	int access=FILEACCESS_NONE;
	if (flags & O_RDONLY) access |= FILEACCESS_READ;
	if (flags & O_WRONLY) access |= FILEACCESS_WRITE;
	if (flags & O_APPEND) access |= FILEACCESS_APPEND;
	if (flags & O_CREAT)	access |= FILEACCESS_CREATE;

	u32 h = pspFileSystem.OpenFile(filename, (FileAccess)access);
	if (h == 0)
	{
		ERROR_LOG(HLE,"ERROR_ERRNO_FILE_NOT_FOUND=sceIoOpen(%s, %08x) - file not found", filename, flags);
		return ERROR_ERRNO_FILE_NOT_FOUND;
	}

	FileNode *f = new FileNode();
	SceUID id = kernelObjects.Create(f);
	f->handle = h;
	f->fullpath = filename;
	f->asyncResult = id;
	DEBUG_LOG(HLE,"%i=sceIoOpen(%s, %08x)",id,filename,flags);
	return id;
}

u32 sceIoClose(SceUID fd)
{
	DEBUG_LOG(HLE,"sceIoClose(%d)",fd);
	return kernelObjects.Destroy<FileNode>(fd);
}

u32 sceIoRemove(u32 filenameAddr)
{
	const char *filename = Memory::GetCharPointer(filenameAddr);
	DEBUG_LOG(HLE,"sceIoRemove(%s)", filename);
	if (pspFileSystem.DeleteFile(filename))
	    return 0;
	else
	    return -1;
}

u32 sceIoMkdir(const char *filename, int mode)
{
	DEBUG_LOG(HLE,"sceIoMkdir(%s, %i)", filename, mode);
	if (pspFileSystem.MkDir(filename))
	    return 0;
	else
	    return -1;
}

u32 sceIoRmdir(u32 dirAddr)
{
	const char *filename = Memory::GetCharPointer(dirAddr);
	DEBUG_LOG(HLE,"sceIoRmdir(%s)", filename);
	if (pspFileSystem.RmDir(filename))
	    return 0;
	else
	    return -1;
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

u32 sceIoDevctl(u32 nameAddr, int cmd, u32 argAddr, int argLen, u32 outPtr, int outLen)
{
	const char *name = Memory::GetCharPointer(nameAddr);
	int retVal = 0;
	DEBUG_LOG(HLE,"%i=sceIoDevctl(\"%s\", %08x, %08x, %i, %08x, %i)", 
		retVal, name, cmd,argAddr,argLen,outPtr,outLen);

	// This should really send it on to a FileSystem implementation instead.

	if (!strcmp(name, "mscmhc0:"))
	{
		switch (cmd)
		{
		// does one of these set a callback as well? (see coded arms)
		case 0x02025806:	// Memory stick inserted?
		case 0x02025801:	// Memstick Driver status?
			Memory::Write_U32(1, outPtr);
			return 0;
		case 0x02425818:
			// Pretend we have a 2GB memory stick.
			{
				u64 totalSize = (u32)2 * 1024 * 1024 * 1024;
				u64 freeSize	= 1 * 1024 * 1024 * 1024;
				DeviceSize deviceSize;
				deviceSize.maxSectors				= 512;
				deviceSize.sectorSize				= 0x200;
				deviceSize.sectorsPerCluster = 0x08;
				deviceSize.totalClusters		 = (u32)((totalSize * 95 / 100) / (deviceSize.sectorSize * deviceSize.sectorsPerCluster));
				deviceSize.freeClusters			= (u32)((freeSize	* 95 / 100) / (deviceSize.sectorSize * deviceSize.sectorsPerCluster));
				Memory::WriteStruct(outPtr, &deviceSize);
				return 0;
			}

		}
	}

	if (!strcmp(name, "fatms0:") && cmd == 0x02425823)
	{
		if (Memory::IsValidAddress(outPtr))
			Memory::Write_U32(1, outPtr);	 // TODO: Make a headless mode for running tests!

        return 0;
	}


	if (!strcmp(name, "kemulator:") || !strcmp(name, "emulator:"))
	{
		// Emulator special tricks!
		switch (cmd)
		{
		case 1:	// EMULATOR_DEVCTL__GET_HAS_DISPLAY
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(0, outPtr);	 // TODO: Make a headless mode for running tests!
			break;
		case 2:	// EMULATOR_DEVCTL__SEND_OUTPUT
			{
				std::string data(Memory::GetCharPointer(argAddr), argLen);
				if (PSP_CoreParameter().printfEmuLog)
				{
					puts(data.c_str());
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
				break;
			}
		case 3:	// EMULATOR_DEVCTL__IS_EMULATOR
			if (Memory::IsValidAddress(outPtr))
				Memory::Write_U32(1, outPtr);	 // TODO: Make a headless mode for running tests!
			break;	
		}
		retVal = 0;
	}

	//089c6d1c weird branch
	/*
	089c6bdc ]: HLE: sceKernelCreateCallback(name= MemoryStick Detection ,entry= 089c7484 ) (z_un_089c6bc4)
	089c6c18 ]: HLE: sceIoDevctl("mscmhc0:", 02015804, 09ffb9c0, 4, 00000000, 0) (z_un_089c6bc4)
	089c6c40 ]: HLE: sceKernelCreateCallback(name= MemoryStick Assignment ,entry= 089c7534 ) (z_un_089c6bc4)
	089c6c78 ]: HLE: sceIoDevctl("fatms0:", 02415821, 09ffb9c4, 4, 00000000, 0) (z_un_089c6bc4)
	089c6cac ]: HLE: sceIoDevctl("mscmhc0:", 02025806, 00000000, 0, 09ffb9c8, 4) (z_un_089c6bc4)
	*/
	return retVal;
}

u32 sceIoRename(u32 oldNameAddr, u32 newNameAddr)
{
	const char *from = Memory::GetCharPointer(oldNameAddr);
	const char *to = Memory::GetCharPointer(newNameAddr);
	ERROR_LOG(HLE,"UNIMPL sceIoRename(%s, %s)", from, to);
	return 0;
}

u32 sceIoChdir(u32 dirAddr)
{
	const char *dir = Memory::GetCharPointer(dirAddr);
	pspFileSystem.ChDir(dir);
	DEBUG_LOG(HLE,"sceIoChdir(%s)",dir);
	return 1;
}

typedef u32 (*DeferredAction)(SceUID id, int param);
DeferredAction defAction = 0;
u32 defParam;

u32 sceIoChangeAsyncPriority(u32 prio)
{
	ERROR_LOG(HLE,"UNIMPL sceIoChangeAsyncPriority(%d)", prio);
	return 0;
}

u32 __IoClose(SceUID id, int param)
{
	DEBUG_LOG(HLE,"Deferred IoClose(%d)",id);
	__IoCompleteAsyncIO(id);
	return kernelObjects.Destroy<FileNode>(id);
}

u32 sceIoCloseAsync(SceUID fd)
{
	DEBUG_LOG(HLE,"sceIoCloseAsync(%d)",fd);
	//sceIoClose();
	defAction = &__IoClose;
	return 0;
}

u32 sceIoLseekAsync(SceUID fd, SceOff offset, u32 whence)
{
	sceIoLseek(fd, offset, whence);
	__IoCompleteAsyncIO(fd);
	return 0;
}

u32 sceIoSetAsyncCallback(SceUID fd, SceUID cb, u32 argAddr)
{
	DEBUG_LOG(HLE,"sceIoSetAsyncCallback(%d, %i, %08x)", fd, cb, argAddr);

	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		f->callbackID = cb;
		f->callbackArg = argAddr;
		return 0;
	}
	else
	{
	    return error;
	}
}

u32 sceIoLseek32Async(SceUID fd, int offset, u32 whence)
{
	DEBUG_LOG(HLE,"sceIoLseek32Async(%d) sorta implemented",fd);
	sceIoLseek32(fd, offset, whence);
	__IoCompleteAsyncIO(fd);
	return 0;
}

u32 sceIoOpenAsync(const char *filename, int flags, SceMode mode)
{
	DEBUG_LOG(HLE,"sceIoOpenAsync() sorta implemented");
	return sceIoOpen(filename, flags, mode);
//	__IoCompleteAsyncIO(currentMIPS->r[2]);	// The return value
	// We have to return a UID here, which may have been destroyed when we reach Wait if it failed.
	// Now that we're just faking it, we just don't RETURN(0) here.
}	

u32 sceIoReadAsync(SceUID fd, u32 dataAddr, SceSize size)
{
	DEBUG_LOG(HLE,"sceIoReadAsync(%d)",fd);
	sceIoRead(fd, dataAddr, size);
	__IoCompleteAsyncIO(fd);
	return 0;
}

u32 sceIoGetAsyncStat(SceUID fd, int poll, u32 resAddr)
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(resAddr);
		*resPtr = f->asyncResult;
		DEBUG_LOG(HLE,"%i = sceIoGetAsyncStat(%i, %i, %08x) (HACK)", (u32)*resPtr, fd, poll, resAddr);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoGetAsyncStat with invalid id %i", fd);
		return -1;
	}
}


u32 sceIoWaitAsync(SceUID fd, u32 resAddr)
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(resAddr);
		*resPtr = f->asyncResult;
		if (defAction)
		{
			*resPtr = defAction(fd, defParam);
			defAction = 0;
		}
		DEBUG_LOG(HLE,"%i = sceIoWaitAsync(%i, %08x) (HACK)", (u32)*resPtr, fd, resAddr);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoWaitAsync waiting for invalid id %i", fd);
		return -1;
	}
}

u32 sceIoWaitAsyncCB(SceUID fd, u32 resAddr)
{
	// Should process callbacks here
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(resAddr);
		*resPtr = f->asyncResult;
		if (defAction)
		{
			*resPtr = defAction(fd, defParam);
			defAction = 0;
		}
		DEBUG_LOG(HLE,"%i = sceIoWaitAsyncCB(%i, %08x) (HACK)", (u32)*resPtr, fd, resAddr);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoWaitAsyncCB waiting for invalid id %i", fd);
		return error;
	}
}

u32 sceIoPollAsync(SceUID fd, u32 resAddr)
{
	u32 error;
	FileNode *f = kernelObjects.Get<FileNode>(fd, error);
	if (f)
	{
		u64 *resPtr = (u64*)Memory::GetPointer(resAddr);
		*resPtr = f->asyncResult;
		if (defAction)
		{
			*resPtr = defAction(fd, defParam);
			defAction = 0;
		}
		DEBUG_LOG(HLE,"%i = sceIoPollAsync(%i, %08x) (HACK)", (u32)*resPtr, fd, resAddr);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE,"ERROR - sceIoPollAsync waiting for invalid id %i", fd);
	    return error;
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

u32 sceIoDopen(u32 pathAddr)
{
	const char *path = Memory::GetCharPointer(pathAddr);
	DEBUG_LOG(HLE,"sceIoDopen(\"%s\")",path);

	DirListing *dir = new DirListing();
	SceUID id = kernelObjects.Create(dir);

	// TODO: ERROR_ERRNO_FILE_NOT_FOUND

	dir->listing = pspFileSystem.GetDirListing(path);
	dir->index = 0;
	dir->name = std::string(path);

    return id;
}

u32 sceIoDread(SceUID id, u32 dirEntAddr)
{
	u32 error;
	DirListing *dir = kernelObjects.Get<DirListing>(id, error);
	if (dir)
	{
		if (dir->index == (int)dir->listing.size())
		{
		    return 0;
		}

		PSPFileInfo &info = dir->listing[dir->index];

		SceIoDirEnt *entry = (SceIoDirEnt*)Memory::GetPointer(dirEntAddr);

		__IoGetStat(&entry->d_stat, info);

		strncpy(entry->d_name, info.name.c_str(), 256);
		entry->d_private = 0xC0DEBABE;
		DEBUG_LOG(HLE,"sceIoDread( %d %08x ) = %s", id, dirEntAddr, entry->d_name);

		dir->index++;
		return (u32)(dir->listing.size()-dir->index+1);
	}
	else
	{
		DEBUG_LOG(HLE,"sceIoDread - invalid listing %i, error %08x", id, error);
		return error;
	}
}

u32 sceIoDclose(SceUID id)
{
	DEBUG_LOG(HLE,"sceIoDclose(%d)",id);
	return kernelObjects.Destroy<DirListing>(id);
}

const HLEFunction IoFileMgrForUser[] =
{
	{0xb29ddf9c,&Wrap<sceIoDopen>, "sceIoDopen"},
	{0xe3eb004c,&Wrap<sceIoDread>, "sceIoDread"},
	{0xeb092469,&Wrap<sceIoDclose>,"sceIoDclose"},
	{0xe95a012b,0,"sceIoIoctlAsync"},
	{0x63632449,0,"sceIoIoctl"},
	{0xace946e8,&Wrap<sceIoGetstat>,"sceIoGetstat"},
	{0xb8a740f4,0,"sceIoChstat"},
	{0x55f4717d,&Wrap<sceIoChdir>,"sceIoChdir"},
	{0x08bd7374,0,"sceIoGetDevType"},
	{0xB2A628C1,&Wrap<sceIoAssign>,"sceIoAssign"},
	{0xe8bc6571,0,"sceIoCancel"},
	{0xb293727f,&Wrap<sceIoChangeAsyncPriority>,"sceIoChangeAsyncPriority"},
	{0x810C4BC3,&Wrap<sceIoClose>, "sceIoClose"},	//(int fd); 
	{0xff5940b6,&Wrap<sceIoCloseAsync>,"sceIoCloseAsync"},
	{0x54F5FB11,&Wrap<sceIoDevctl>,"sceIoDevctl"}, //(const char *name int cmd, void *arg, size_t arglen, void *buf, size_t *buflen); 
	{0xcb05f8d6,&Wrap<sceIoGetAsyncStat>,"sceIoGetAsyncStat"},
	{0x27EB27B8,&Wrap<sceIoLseek>, "sceIoLseek"},	//(int fd, int offset, int whence); 
	{0x68963324,&Wrap<sceIoLseek32>,"sceIoLseek32"},
	{0x1b385d8f,&Wrap<sceIoLseek32Async>,"sceIoLseek32Async"},
	{0x71b19e77,&Wrap<sceIoLseekAsync>,"sceIoLseekAsync"},
	{0x109F50BC,&Wrap<sceIoOpen>,	"sceIoOpen"},	 //(const char* file, int mode); 
	{0x89AA9906,&Wrap<sceIoOpenAsync>,"sceIoOpenAsync"},
	{0x06A70004,&Wrap<sceIoMkdir>,"sceIoMkdir"},	//(const char *dir, int mode); 
	{0x3251ea56,&Wrap<sceIoPollAsync>,"sceIoPollAsync"},
	{0x6A638D83,&Wrap<sceIoRead>,	"sceIoRead"},	 //(int fd, void *data, int size); 
	{0xa0b5a7c2,&Wrap<sceIoReadAsync>,"sceIoReadAsync"},
	{0xF27A9C51,&Wrap<sceIoRemove>,"sceIoRemove"}, //(const char *file); 
	{0x779103A0,&Wrap<sceIoRename>,"sceIoRename"}, //(const char *oldname, const char *newname); 
	{0x1117C65F,&Wrap<sceIoRmdir>,"sceIoRmdir"},	//(const char *dir); 
	{0xA12A0514,&Wrap<sceIoSetAsyncCallback>,"sceIoSetAsyncCallback"},
	{0xab96437f,&Wrap<sceIoSync>,"sceIoSync"},
	{0x6d08a871,0,"sceIoUnassign"},
	{0x42EC03AC,&Wrap<sceIoWrite>, "sceIoWrite"},	//(int fd, void *data, int size); 
	{0x0facab19,0,"sceIoWriteAsync"},
	{0x35dbd746,&Wrap<sceIoWaitAsyncCB>,"sceIoWaitAsyncCB"},
	{0xe23eec33,&Wrap<sceIoWaitAsync>,"sceIoWaitAsync"},
};

void Register_IoFileMgrForUser()
{
	RegisterModule("IoFileMgrForUser", ARRAY_SIZE(IoFileMgrForUser), IoFileMgrForUser);
}


const HLEFunction StdioForUser[] = 
{
	{0x172D316E,&Wrap<sceKernelStdin>,"sceKernelStdin"},
	{0xA6BAB2E9,&Wrap<sceKernelStdout>,"sceKernelStdout"},
	{0xF78BA90A,&Wrap<sceKernelStderr>,"sceKernelStderr"},
};

void Register_StdioForUser()
{
	RegisterModule("StdioForUser", ARRAY_SIZE(StdioForUser), StdioForUser);
}
