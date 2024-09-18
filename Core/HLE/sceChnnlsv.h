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

typedef struct _pspChnnlsvContext1 {
	/** Cipher mode */
	s32_le mode;

	/** Context data */
	u8 result[0x10];
	u8 key[0x10];
	s32_le keyLength;
} pspChnnlsvContext1;

typedef struct _pspChnnlsvContext2 {
	/** Context data */
	s32_le mode;
	s32_le unkn;
	u8 cryptedData[0x92];
} pspChnnlsvContext2;

int sceSdSetIndex_(pspChnnlsvContext1& ctx, int value);
int sceSdRemoveValue_(pspChnnlsvContext1& ctx, const u8* data, int length);
int sceSdCreateList_(pspChnnlsvContext2& ctx2, int mode, int uknw, u8* data, const u8* cryptkey);
int sceSdSetMember_(pspChnnlsvContext2& ctx, u8* data, int alignedLen);
int sceSdCleanList_(pspChnnlsvContext2& ctx);
int sceSdGetLastIndex_(pspChnnlsvContext1& ctx, u8* in_hash, const u8* in_key);

void Register_sceChnnlsv();
