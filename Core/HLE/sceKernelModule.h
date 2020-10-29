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
#include "Core/HLE/sceKernel.h"

struct PspModuleInfo {
	u16_le moduleAttrs; //0x0000 User Mode, 0x1000 Kernel Mode
	u16_le moduleVersion;
	// 28 bytes of module name, packed with 0's.
	char name[28];
	u32_le gp;               // ptr to MIPS GOT data  (global offset table)
	u32_le libent;           // ptr to .lib.ent section
	u32_le libentend;        // ptr to end of .lib.ent section
	u32_le libstub;          // ptr to .lib.stub section
	u32_le libstubend;       // ptr to end of .lib.stub section
};

class PointerWrap;
struct SceKernelSMOption;

KernelObject *__KernelModuleObject();
void __KernelModuleDoState(PointerWrap &p);
void __KernelModuleShutdown();

u32 __KernelGetModuleGP(SceUID module);
bool KernelModuleIsKernelMode(SceUID module);
bool __KernelLoadGEDump(const std::string &base_filename, std::string *error_string);
bool __KernelLoadExec(const char *filename, u32 paramPtr, std::string *error_string);
void __KernelGPUReplay();
void __KernelReturnFromModuleFunc();
SceUID KernelLoadModule(const std::string &filename, std::string *error_string);
int KernelStartModule(SceUID moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, SceKernelSMOption *smoption, bool *needsWait);
u32 hleKernelStopUnloadSelfModuleWithOrWithoutStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr, bool WithStatus);
u32 sceKernelFindModuleByUID(u32 uid);

void Register_ModuleMgrForUser();
void Register_ModuleMgrForKernel();

// Expose for use by KUBridge.
u32 sceKernelLoadModule(const char *name, u32 flags, u32 optionAddr);
