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

#pragma once

#include <string>

#include "../System.h"
#include "HLE.h"
#include "sceKernel.h"

class FileNode : public KernelObject {
public:
	FileNode() : callbackID(0), callbackArg(0), asyncResult(0), pendingAsyncResult(false), sectorBlockMode(false) {}
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
		p.DoMarker("File");
	}

	std::string fullpath;
	u32 handle;

	u32 callbackID;
	u32 callbackArg;

	u32 asyncResult;

	bool pendingAsyncResult;
	bool sectorBlockMode;
};

void __IoInit();
void __IoDoState(PointerWrap &p);
void __IoShutdown();
KernelObject *__KernelFileNodeObject();
KernelObject *__KernelDirListingObject();

void Register_IoFileMgrForUser();
void Register_StdioForUser();
