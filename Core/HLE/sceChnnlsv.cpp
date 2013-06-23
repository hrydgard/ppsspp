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

#include "sceChnnlsv.h"
#include "sceKernel.h"
extern "C"
{
#include "ext/libkirk/kirk_engine.h"
}

u8 dataBuf[2048+20];
u8* dataBuf2 = dataBuf + 20;

static const u8 hash198C[16] = {0xFA, 0xAA, 0x50, 0xEC, 0x2F, 0xDE, 0x54, 0x93, 0xAD, 0x14, 0xB2, 0xCE, 0xA5, 0x30, 0x05, 0xDF};
static const u8 hash19BC[16] = {0xCB, 0x15, 0xF4, 0x07, 0xF9, 0x6A, 0x52, 0x3C, 0x04, 0xB9, 0xB2, 0xEE, 0x5C, 0x53, 0xFA, 0x86};

static const u8 key19CC[16]  = {0x70, 0x44, 0xA3, 0xAE, 0xEF, 0x5D, 0xA5, 0xF2, 0x85, 0x7F, 0xF2, 0xD6, 0x94, 0xF5, 0x36, 0x3B};
static const u8 key19DC[16]  = {0xEC, 0x6D, 0x29, 0x59, 0x26, 0x35, 0xA5, 0x7F, 0x97, 0x2A, 0x0D, 0xBC, 0xA3, 0x26, 0x33, 0x00};
static const u8 key199C[16]  = {0x36, 0xA5, 0x3E, 0xAC, 0xC5, 0x26, 0x9E, 0xA3, 0x83, 0xD9, 0xEC, 0x25, 0x6C, 0x48, 0x48, 0x72};
static const u8 key19AC[16]  = {0xD8, 0xC0, 0xB0, 0xF3, 0x3E, 0x6B, 0x76, 0x85, 0xFD, 0xFB, 0x4D, 0x7D, 0x45, 0x1E, 0x92, 0x03};

void *memxor(void * dest, const void * src, size_t n)
{
  char const *s = (char const*)src;
  char *d = (char*)dest;

  for (; n > 0; n--)
    *d++ ^= *s++;

  return dest;
}

// The reason for the values from *FromMode calculations are not known.
int numFromMode(int mode)
{
	int num = 0;
	switch(mode)
	{
	case 1:
		num = 3;
		break;
	case 2:
		num = 5;
		break;
	case 3:
		num = 12;
		break;
	case 4:
		num = 13;
		break;
	case 6:
		num = 17;
		break;
	default:
		num = 16;
		break;
	}
	return num;
}
int numFromMode2(int mode)
{
	int num = 18;
	if (mode == 1)
		num = 4;
	else if (mode == 3)
		num = 14;
	return num;
}

int typeFromMode(int mode)
{
	return (mode == 1 || mode == 2) ? 83 :
	      ((mode == 3 || mode == 4) ? 87 : 100);	
}

int kirkSendCmd(u8* data, int length, int num, bool encrypt)
{
	*(int*)(data+0) = encrypt ? KIRK_MODE_ENCRYPT_CBC : KIRK_MODE_DECRYPT_CBC;
	*(int*)(data+4) = 0;
	*(int*)(data+8) = 0;
	*(int*)(data+12) = num;
	*(int*)(data+16) = length;

	if (sceUtilsBufferCopyWithRange(data, length + 20, data, length + 20, encrypt ? KIRK_CMD_ENCRYPT_IV_0 : KIRK_CMD_DECRYPT_IV_0))
		return -257;

	return 0;
}

int kirkSendFuseCmd(u8* data, int length, bool encrypt)
{
	*(int*)(data+0) = encrypt ? KIRK_MODE_ENCRYPT_CBC : KIRK_MODE_DECRYPT_CBC;
	*(int*)(data+4) = 0;
	*(int*)(data+8) = 0;
	*(int*)(data+12) = 256;
	*(int*)(data+16) = length;

	// Note: CMD 5 and 8 are not available, will always return -1
	if (sceUtilsBufferCopyWithRange(data, length + 20, data, length + 20, encrypt ? KIRK_CMD_ENCRYPT_IV_FUSE : KIRK_CMD_DECRYPT_IV_FUSE))
		return -258;

	return 0;
}

int sub_15B0(u8* data, int alignedLen, u8* buf, int val)
{
	u8 sp0[16];
	memcpy(sp0, data+alignedLen+4, 16);

	int res = kirkSendCmd(data, alignedLen, val, false);
	if (res)
		return res;

	memxor(data, buf, 16);
	memcpy(buf, sp0, 16);
	return 0;
}

int sub_0000(u8* data_out, u8* data, int alignedLen, u8* data2, int& data3, int mode)
{
	memcpy(data_out+20, data2, 16);
	// Mode 1:2 is 83, 3:4 is 87, 5:6 is 100
	int type = typeFromMode(mode);
	int res;

	if (type == 87)
		memxor(data_out+20, key19AC, 16);
	else if (type == 100)
		memxor(data_out+20, key19DC, 16);

	// Odd is Cmd, Even is FuseCmd
	switch(mode)
	{
	case 2: case 4:	case 6:	res = kirkSendFuseCmd(data_out, 16, false);
	break;
	case 1:	case 3:	default:res = kirkSendCmd(data_out, 16, numFromMode2(mode), false);
	break;
	}

	if (type == 87)
		memxor(data_out, key199C, 16);
	else if (type == 100)
		memxor(data_out, key19CC, 16);

	if (res)
		return res;

	u8 sp0[16], sp16[16];
	memcpy(sp16, data_out, 16);
	if (data3 == 1)
	{
		memset(sp0, 0, 16);
	}
	else
	{
		memcpy(sp0, sp16, 12);
		*(u32*)(sp0+12) = data3-1;
	}

	if (alignedLen > 0)
	{
		for(int i = 20; i < alignedLen + 20; i += 16)
		{
			memcpy(data_out+i, sp16, 12);
			*(u32*)(data_out+12+i) = data3;
			data3++;
		}
	}

	res = sub_15B0(data_out, alignedLen, sp0, type);
	if (res)
		return res;

	if (alignedLen > 0)
		memxor(data, data_out, alignedLen);

	return 0;
}

int sub_1510(u8* data, int size, u8* result , int num)
{
	memxor(data+20, result, 16);

	int res = kirkSendCmd(data, size, num, true);
	if(res)
		return res;

	memcpy(result, data+size+4, 16);
	return 0;
}

int sub_17A8(u8* data)
{
	if (sceUtilsBufferCopyWithRange(data, 20, 0, 0, 14) == 0)
		return 0;
	return -261;
}

int sceSdGetLastIndex(u32 addressCtx, u32 addressHash, u32 addressKey)
{
	pspChnnlsvContext1 ctx;
	Memory::ReadStruct(addressCtx, &ctx);
	int res = sceSdGetLastIndex_(ctx, Memory::GetPointer(addressHash), Memory::GetPointer(addressKey));
	Memory::WriteStruct(addressCtx, &ctx);
	return res;
}

int sceSdGetLastIndex_(pspChnnlsvContext1& ctx, u8* in_hash, u8* in_key)
{
	if(ctx.keyLength >= 17)
		return -1026;

	int num = numFromMode(ctx.mode);

	memset(dataBuf2, 0, 16);

	int res = kirkSendCmd(dataBuf, 16, num, true);
	if(res)
		return res;

	u8 data1[16], data2[16];

	memcpy(data1, dataBuf2, 16);
	int tmp1 = (data1[0] & 0x80) ? 135 : 0;

	for(int i = 0; i < 15; i++)
	{
		u8 val1 = data1[i] << 1;
		u8 val2 = data1[i+1] >> 7;
		data1[i] = val1 | val2;
	}

	u8 tmp2 = data1[15] << 1;
	tmp2 = tmp1 ^ tmp2;
	data1[15] = tmp2;

	if(ctx.keyLength < 16)
	{
		tmp1 = 0;
		if((s8)data1[0] < 0)
		{
			tmp1 = 135;
		}
		for(int i = 0; i < 15; i++)
		{
			u8 val1 = data1[i] << 1;
			u8 val2 = data1[i+1] >> 7;
			data1[i] = val1 | val2;
		}
		u8 tmp2 = data1[15] << 1;
		tmp2 = tmp1 ^ tmp2;
		data1[15] = tmp2;

		int oldKeyLength = ctx.keyLength;
		*(s8*)(ctx.key + ctx.keyLength) = -128;
		int i = oldKeyLength + 1;
		if(i < 16)
			memset(ctx.key + i, 0, 16 - i);
	}

	memxor(ctx.key, data1, 16);
	memcpy(dataBuf2, ctx.key, 16);
	memcpy(data2, ctx.result, 16);

	int ret = sub_1510(dataBuf, 16, data2, num);
	if(ret)
		return ret;

	if(ctx.mode == 3 || ctx.mode == 4)
		memxor(data2, hash198C, 16);
	else if(ctx.mode == 5 || ctx.mode == 6)
		memxor(data2, hash19BC, 16);

	int cond = ((ctx.mode ^ 0x2) < 1 || (ctx.mode ^ 0x4) < 1 || ctx.mode == 6);
	if(cond != 0)
	{
		memcpy(dataBuf2, data2, 16);
		int ret = kirkSendFuseCmd(dataBuf, 16, true);
		if(ret)
			return ret;

		int res = kirkSendCmd(dataBuf, 16, num, true);
		if(res)
			return res;

		memcpy(data2, dataBuf2, 16);
	}

	if(in_key != 0)
	{
		for(int i = 0; i < 16; i++)
		{
			data2[i] = in_key[i] ^ data2[i];
		}

		memcpy(dataBuf2, data2, 16);

		int res = kirkSendCmd(dataBuf, 16, num, true);
		if(res)
			return res;

		memcpy(data2, dataBuf2, 16);
	}
	memcpy(in_hash, data2, 16);
	sceSdSetIndex_(ctx, 0);

	return 0;
}

int sceSdSetIndex(u32 addressCtx, int value)
{
	pspChnnlsvContext1 ctx;
	Memory::ReadStruct(addressCtx,&ctx);
	int res = sceSdSetIndex_(ctx, value);
	Memory::WriteStruct(addressCtx,&ctx);
	return res;
}

int sceSdSetIndex_(pspChnnlsvContext1& ctx, int value)
{
	ctx.mode = value;
	memset(ctx.result, 0, 16);
	memset(ctx.key, 0, 16);
	ctx.keyLength = 0;
	return 0;
}


int sceSdRemoveValue(u32 addressCtx, u32 addressData, int length)
{
	pspChnnlsvContext1 ctx;
	Memory::ReadStruct(addressCtx, &ctx);
	int res = sceSdRemoveValue_(ctx, Memory::GetPointer(addressData), length);
	Memory::WriteStruct(addressCtx, &ctx);

	return res;
}

int sceSdRemoveValue_(pspChnnlsvContext1& ctx, u8* data, int length)
{
	if(ctx.keyLength >= 17)
		return -1026;

	if(ctx.keyLength + length < 17)
	{
		memcpy(ctx.key+ctx.keyLength, data, length);
		ctx.keyLength = ctx.keyLength + length;
		return 0;
	}
	int num = numFromMode(ctx.mode);

	memset(dataBuf2, 0, 2048);
	memcpy(dataBuf2, ctx.key, ctx.keyLength);

	int len = (ctx.keyLength + length) & 0xF;
	if(len == 0) len = 16;

	int newSize = ctx.keyLength;
	ctx.keyLength = len;

	int diff = length - len;
	memcpy(ctx.key, data+diff, len);
	for(int i = 0; i < diff; i++)
	{
		if(newSize == 2048)
		{
			int res = sub_1510(dataBuf, 2048, ctx.result, num);
			if(res)
				return res;
			newSize = 0;
		}
		dataBuf2[newSize] = data[i];
		newSize++;
	}
	if(newSize)
		sub_1510(dataBuf, newSize, ctx.result, num);
	// The RE code showed this always returning 0. I suspect it would want to return res instead.
	return 0;
}

int sceSdCreateList(u32 ctx2Addr, int mode, int unkwn, u32 dataAddr, u32 cryptkeyAddr)
{
	pspChnnlsvContext2 ctx2;
	Memory::ReadStruct(ctx2Addr, &ctx2);
	u8* data = Memory::GetPointer(dataAddr);
	u8* cryptkey = Memory::GetPointer(cryptkeyAddr);

	int res = sceSdCreateList_(ctx2, mode, unkwn, data, cryptkey);

	Memory::WriteStruct(ctx2Addr, &ctx2);

	return res;
}

int sceSdCreateList_(pspChnnlsvContext2& ctx2, int mode, int uknw, u8* data, u8* cryptkey)
{
	ctx2.mode = mode;
	ctx2.unkn = 1;
	if (uknw == 2)
	{
		memcpy(ctx2.cryptedData, data, 16);
		if (cryptkey)
			memxor(ctx2.cryptedData, cryptkey, 16);

		return 0;
	}
	else if (uknw == 1)
	{
		u8 kirkHeader[37];
		u8* kirkData = kirkHeader+20;
		int res = sub_17A8(kirkHeader);
		if (res)
			return res;

		memcpy(kirkHeader+20, kirkHeader, 16);
		memset(kirkHeader+32, 0, 4);

		int type = typeFromMode(mode);
		if (type == 87)
			memxor(kirkData, key199C, 16);
		else if (type == 100)
			memxor(kirkData, key19CC, 16);

		switch (mode)
		{
		case 2:	case 4:	case 6:	res = kirkSendFuseCmd(kirkHeader, 16, true);
		break;
		case 1:	case 3:	default:res = kirkSendCmd(kirkHeader, 16, numFromMode2(mode), true);
		break;
		}

		if (type == 87)
			memxor(kirkData, key19AC, 16);
		else if (type == 100)
			memxor(kirkData, key19DC, 16);

		if (res)
			return res;

		memcpy(ctx2.cryptedData, kirkData, 16);
		memcpy(data, kirkData, 16);
		if (cryptkey)
			memxor(ctx2.cryptedData, cryptkey, 16);
	}

	return 0;
}

int sceSdSetMember(u32 ctxAddr, u32 dataAddr, int alignedLen)
{
	pspChnnlsvContext2 ctx;
	Memory::ReadStruct(ctxAddr, &ctx);
	u8* data = Memory::GetPointer(dataAddr);

	int res = sceSdSetMember_(ctx, data, alignedLen);

	Memory::WriteStruct(ctxAddr, &ctx);

	return res;
}

int sceSdSetMember_(pspChnnlsvContext2& ctx, u8* data, int alignedLen)
{
	if (alignedLen == 0)
	{
		return 0;
	}
	if ((alignedLen & 0xF) != 0)
	{
		return -1025;
	}
	int i = 0;
	u8 kirkData[20+2048];
	if ((u32)alignedLen >= (u32)2048)
	{
		for(i = 0; alignedLen >= 2048; i += 2048)
		{
			int res = sub_0000(kirkData, data + i, 2048, ctx.cryptedData, ctx.unkn, ctx.mode);
			alignedLen -= 2048;
			if (res)
				return res;
		}
	}
	if (alignedLen == 0)
	{
		return 0;
	}

	return sub_0000(kirkData, data + i, alignedLen, ctx.cryptedData, ctx.unkn, ctx.mode);
}

int sceChnnlsv_21BE78B4(u32 ctxAddr)
{
	pspChnnlsvContext2 ctx;
	Memory::ReadStruct(ctxAddr, &ctx);

	int res = sceChnnlsv_21BE78B4_(ctx);

	Memory::WriteStruct(ctxAddr, &ctx);
	return res;
}

int sceChnnlsv_21BE78B4_(pspChnnlsvContext2& ctx)
{
	memset(ctx.cryptedData, 0, 16);
	ctx.unkn = 0;
	ctx.mode = 0;

	return 0;
}

const HLEFunction sceChnnlsv[] =
{
	{0xE7833020,WrapI_UI<sceSdSetIndex>,"sceSdSetIndex"},
	{0xF21A1FCA,WrapI_UUI<sceSdRemoveValue>,"sceSdRemoveValue"},
	{0xC4C494F8,WrapI_UUU<sceSdGetLastIndex>,"sceSdGetLastIndex"},
	{0xABFDFC8B,WrapI_UIIUU<sceSdCreateList>,"sceSdCreateList"},
	{0x850A7FA1,WrapI_UUI<sceSdSetMember>,"sceSdSetMember"},
	{0x21BE78B4,WrapI_U<sceChnnlsv_21BE78B4>,"sceChnnlsv_21BE78B4"},
};

void Register_sceChnnlsv()
{
	RegisterModule("sceChnnlsv", ARRAY_SIZE(sceChnnlsv), sceChnnlsv);
	kirk_init();
}
