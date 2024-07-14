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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MemMap.h"
#include "Core/HLE/sceSsl.h"

#define ERROR_SSL_NOT_INIT          0x80435001;
#define ERROR_SSL_ALREADY_INIT      0x80435020;
#define ERROR_SSL_OUT_OF_MEMORY     0x80435022;
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
	auto s = p.Section("sceSsl", 1);
	if (!s)
		return;

	Do(p, isSslInit);
	Do(p, maxMemSize);
	Do(p, currentMemSize);
}

static int sceSslInit(int heapSize)
{
	DEBUG_LOG(Log::HLE, "sceSslInit %d", heapSize);
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

static int sceSslEnd()
{
	DEBUG_LOG(Log::HLE, "sceSslEnd");
	if (!isSslInit) 
	{
		return ERROR_SSL_NOT_INIT;
	}
	isSslInit = false;
	return 0;
}

static int sceSslGetUsedMemoryMax(u32 maxMemPtr)
{
	DEBUG_LOG(Log::HLE, "sceSslGetUsedMemoryMax %d", maxMemPtr);
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

static int sceSslGetUsedMemoryCurrent(u32 currentMemPtr)
{
	DEBUG_LOG(Log::HLE, "sceSslGetUsedMemoryCurrent %d", currentMemPtr);
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
	{0X957ECBE2, &WrapI_I<sceSslInit>,                 "sceSslInit",                 'i', "i"},
	{0X191CDEFF, &WrapI_V<sceSslEnd>,                  "sceSslEnd",                  'i', "" },
	{0X5BFB6B61, nullptr,                              "sceSslGetNotAfter",          '?', "" },
	{0X17A10DCC, nullptr,                              "sceSslGetNotBefore",         '?', "" },
	{0X3DD5E023, nullptr,                              "sceSslGetSubjectName",       '?', "" },
	{0X1B7C8191, nullptr,                              "sceSslGetIssuerName",        '?', "" },
	{0XCC0919B0, nullptr,                              "sceSslGetSerialNumber",      '?', "" },
	{0X058D21C0, nullptr,                              "sceSslGetNameEntryCount",    '?', "" },
	{0XD6D097B4, nullptr,                              "sceSslGetNameEntryInfo",     '?', "" },
	{0XB99EDE6A, &WrapI_U<sceSslGetUsedMemoryMax>,     "sceSslGetUsedMemoryMax",     'i', "x"},
	{0X0EB43B06, &WrapI_U<sceSslGetUsedMemoryCurrent>, "sceSslGetUsedMemoryCurrent", 'i', "x"},
	{0XF57765D3, nullptr,                              "sceSslGetKeyUsage",          '?', "" },
};

void Register_sceSsl()
{
	RegisterModule("sceSsl", ARRAY_SIZE(sceSsl), sceSsl);
}
