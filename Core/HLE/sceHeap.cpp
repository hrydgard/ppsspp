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

#include "Core/HLE/HLE.h"
#include "Common/ChunkFile.h"
#include "Core/HLE/sceHeap.h"

void __HeapDoState(PointerWrap &p) {
	p.DoMarker("sceHeap");
}

int sceHeapReallocHeapMemory(u32 heapPtr, u32 memPtr, int memSize) {
	ERROR_LOG(HLE,"UNIMPL sceHeapReallocHeapMemory(%d %d %d)", heapPtr, memPtr, memSize);
	return 0;
}

int sceHeapReallocHeapMemoryWithOption(u32 heapPtr, u32 memPtr, int memSize, u32 paramsPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapReallocHeapMemoryWithOption(%d %d %d %d)", heapPtr, memPtr, memSize, paramsPtr);
        return 0;
}

int sceHeapFreeHeapMemory(u32 heapPtr, u32 memPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapFreeHeapMemory(%d %d)", heapPtr, memPtr);
	return 0;
}

int sceHeapGetMallinfo(u32 heapPtr, u32 infoPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapGetMallinfo(%d %d)", heapPtr, infoPtr);
        return 0;
}

int sceHeapAllocHeapMemoryWithOption(u32 heapPtr, int memSize, u32 paramsPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapAllocHeapMemoryWithOption(%08x, %08x, %08x)", heapPtr, memSize, paramsPtr);
	return 0;
}

int sceHeapGetTotalFreeSize(u32 heapPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapGetTotalFreeSize(%d)", heapPtr);
        return 0;
}

int sceHeapIsAllocatedHeapMemory(u32 heapPtr, u32 memPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapIsAllocatedHeapMemory(%d %d)", heapPtr, memPtr);
        return 0;
}

int sceHeapDeleteHeap(u32 heapPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapDeleteHeap(%d)", heapPtr);
        return 0;
}

int sceHeapCreateHeap(const char* name, int len, int attr, u32 paramsPtr) {
	ERROR_LOG(HLE,"UNIMPL sceHeapCreateHeap(%s %d %d %d)", name, len, attr, paramsPtr);
        return 0;
}

int sceHeapAllocHeapMemory(u32 heapAddr, int len) {
	ERROR_LOG(HLE,"UNIMPL sceHeapAllocHeapMemory(%d %d)", heapAddr, len);
        return 0;
}


static const HLEFunction sceHeap[] = 
{
	{0x0E875980,WrapI_UUI<sceHeapReallocHeapMemory>,"sceHeapReallocHeapMemory"},
	{0x2ABADC63,WrapI_UUIU<sceHeapReallocHeapMemoryWithOption>,"sceHeapReallocHeapMemoryWithOption"},
	{0x2ABADC63,WrapI_UU<sceHeapFreeHeapMemory>,"sceHeapFreeHeapMemory"},
	{0x2A0C2009,WrapI_UU<sceHeapGetMallinfo>,"sceHeapGetMallinfo"},
	{0x2B7299D8,WrapI_UIU<sceHeapAllocHeapMemoryWithOption>,"sceHeapAllocHeapMemoryWithOption"},
	{0x4929B40D,WrapI_U<sceHeapGetTotalFreeSize>,"sceHeapGetTotalFreeSize"},
	{0x7012BBDD,WrapI_UU<sceHeapIsAllocatedHeapMemory>,"sceHeapIsAllocatedHeapMemory"},
	{0x70210B73,WrapI_U<sceHeapDeleteHeap>,"sceHeapDeleteHeap"},
	{0x7DE281C2,WrapI_CIIU<sceHeapCreateHeap>,"sceHeapCreateHeap"},
	{0xA8E102A0,WrapI_UI<sceHeapAllocHeapMemory>,"sceHeapAllocHeapMemory"},
};

void Register_sceHeap()
{
	RegisterModule("sceHeap", ARRAY_SIZE(sceHeap), sceHeap);
}
