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
	int	mode;

	/** Context data */
	u8	result[0x10];
	u8  key[0x10];
	int	keyLength;
} pspChnnlsvContext1;

typedef struct _pspChnnlsvContext2 {
	/** Context data */
	int mode;
	int unkn;
	u8  cryptedData[0x92];
} pspChnnlsvContext2;

int sceSdSetIndex_(pspChnnlsvContext1& ctx, int value);
int sceSdRemoveValue_(pspChnnlsvContext1& ctx, u8* data, int length);
int sceSdCreateList_(pspChnnlsvContext2& ctx2, int mode, int uknw, u8* data, u8* cryptkey);
int sceSdSetMember_(pspChnnlsvContext2& ctx, u8* data, int alignedLen);
int sceChnnlsv_21BE78B4_(pspChnnlsvContext2& ctx);
int sceSdGetLastIndex_(pspChnnlsvContext1& ctx, u8* in_hash, u8* in_key);

void Register_sceChnnlsv();
