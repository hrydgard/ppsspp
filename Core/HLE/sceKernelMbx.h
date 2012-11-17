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

struct NativeMbxPacket
{
	u32 next;
	u8 priority;
	u8 padding[3];
};

struct SceKernelMbxInfo
{
	SceSize size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH+1];
	SceUInt attr;
	int numWaitThreads;
	int numMessage;
	u32 topPacketAddr;
};

SceUID sceKernelCreateMbx(const char *name, int memoryPartition, SceUInt attr, int size, u32 optAddr);
int sceKernelDeleteMbx(SceUID id);
void sceKernelSendMbx(SceUID id, u32 addPacketAddr);
void sceKernelReceiveMbx(SceUID id, u32 packetAddrPtr, u32 timeoutPtr);
void sceKernelReceiveMbxCB(SceUID id, u32 packetAddrPtr, u32 timeoutPtr);
int sceKernelPollMbx(SceUID id, u32 packetAddrPtr);
int sceKernelCancelReceiveMbx(SceUID id, u32 numWaitingThreadsAddr);
int sceKernelReferMbxStatus(SceUID id, u32 infoAddr);

