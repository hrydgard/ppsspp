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

#include "HLE.h"
#include "ChunkFile.h"

#include "sceSsl.h"

#define ERROR_SSL_NOT_INIT 0x80435001;
#define ERROR_SSL_ALREADY_INIT	0x80435020;
#define ERROR_SSL_OUT_OF_MEMORY 0x80435022;
#define ERROR_SSL_INVALID_PARAMETER 0x804351FE;

bool isSslInit;
u32 maxMemSize;
u32 currentMemSize;

void __SslInit() 
{
	isSslInit = 0;
	maxMemSize = 0;
	currentMemSize = 0;
}

void __SslDoState(PointerWrap &p)
{
	p.Do(isSslInit);
	p.Do(maxMemSize);
	p.Do(currentMemSize);
	p.DoMarker("sceSsl");
}

int sceSslInit(int heapSize)
{
	DEBUG_LOG(HLE, "sceSslInit %d", heapSize);
	if (isSslInit) 
	{
		return ERROR_SSL_ALREADY_INIT;
	}
	if (heapSize <= 0) 
	{
		return ERROR_SSL_INVALID_PARAMETER;
	}

	maxMemSize = heapSize;
	currentMemSize = heapSize / 2; // As per jpcsp
	isSslInit = true;
	return 0;
}

int sceSslEnd() 
{
	DEBUG_LOG(HLE, "sceSslEnd");
	if (!isSslInit) 
	{
		return ERROR_SSL_NOT_INIT;
	}
	isSslInit = false;
	return 0;
}

int sceSslGetUsedMemoryMax(u32 maxMemPtr) 
{
	DEBUG_LOG(HLE, "sceSslGetUsedMemoryMax %d", maxMemPtr);
	if (!isSslInit) 
	{
		return ERROR_SSL_NOT_INIT;
	}

	if (Memory::IsValidAddress(maxMemPtr))
	{
		Memory::Write_U32(maxMemSize, maxMemPtr);
	}
	return 0;
}

int sceSslGetUsedMemoryCurrent(u32 currentMemPtr) 
{
	DEBUG_LOG(HLE, "sceSslGetUsedMemoryCurrent %d", currentMemPtr);
	if (!isSslInit) 
	{
		return ERROR_SSL_NOT_INIT;
	}

	if (Memory::IsValidAddress(currentMemPtr))
	{
		Memory::Write_U32(currentMemSize, currentMemPtr);
	}
	return 0;
}

const HLEFunction sceSsl[] = 
{
	{0x957ECBE2, WrapI_I<sceSslInit>, "sceSslInit"},
	{0x191CDEFF, WrapI_V<sceSslEnd>, "sceSslEnd"},
	{0x5BFB6B61, 0, "sceSslGetNotAfter"},
	{0x17A10DCC, 0, "sceSslGetNotBefore"},
	{0x3DD5E023, 0, "sceSslGetSubjectName"},
	{0x1B7C8191, 0, "sceSslGetIssuerName"},
	{0xCC0919B0, 0, "sceSslGetSerialNumber"},
	{0x058D21C0, 0, "sceSslGetNameEntryCount"},
	{0xD6D097B4, 0, "sceSslGetNameEntryInfo"},
	{0xB99EDE6A, WrapI_U<sceSslGetUsedMemoryMax>, "sceSslGetUsedMemoryMax"},
	{0x0EB43B06, WrapI_U<sceSslGetUsedMemoryCurrent>, "sceSslGetUsedMemoryCurrent"},
	{0xF57765D3, 0, "sceSslGetKeyUsage"},
};

void Register_sceSsl()
{
	RegisterModule("sceSsl", ARRAY_SIZE(sceSsl), sceSsl);
}