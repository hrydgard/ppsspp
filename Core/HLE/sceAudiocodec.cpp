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
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceAudiocodec.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/HW/SimpleAudioDec.h"

// Following kaien_fr's sample code https://github.com/hrydgard/ppsspp/issues/5620#issuecomment-37086024
// Should probably store the EDRAM get/release status somewhere within here, etc.
struct AudioCodecContext {
	u32_le unknown[6];
	u32_le inDataPtr;   // 6
	u32_le inDataSize;  // 7
	u32_le outDataPtr;  // 8
	u32_le audioSamplesPerFrame;  // 9
	u32_le inDataSizeAgain;  // 10  ??
}; 
// Pointer to audio decoder
SimpleAudio* audio;

int sceAudiocodecInit(u32 ctxPtr, int codec) {
	if (isValideCodec(codec)){
		// Create audio decoder for given audio codec.
		audio = AudioCreate(codec);
		INFO_LOG(ME, "sceAudiocodecInit(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
		return 0;
	}
	ERROR_LOG_REPORT(ME, "sceAudiocodecInit(%08x, %i (%s)): Unknown audio codec %i", ctxPtr, codec, GetCodecName(codec), codec);
	return 0;
}

int sceAudiocodecDecode(u32 ctxPtr, int codec) {
	if (!ctxPtr){
		ERROR_LOG_REPORT(ME, "sceAudiocodecDecode(%08x, %i (%s)) got NULL pointer", ctxPtr, codec, GetCodecName(codec));
		return -1;
	}
	if (isValideCodec(codec)){
		// Use SimpleAudioDec to decode audio
		// Get AudioCodecContext
		AudioCodecContext* ctx = new AudioCodecContext;
		Memory::ReadStruct(ctxPtr, ctx);
		int outbytes = 0;
		// Decode audio
		AudioDecode(audio, Memory::GetPointer(ctx->inDataPtr), ctx->inDataSize, &outbytes, Memory::GetPointer(ctx->outDataPtr));
		DEBUG_LOG(ME, "sceAudiocodecDec(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
		// Delete AudioCodecContext
		delete(ctx);
		return 0;
	}
	ERROR_LOG_REPORT(ME, "UNIMPL sceAudiocodecDecode(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecGetInfo(u32 ctxPtr, int codec) {
	ERROR_LOG_REPORT(ME, "UNIMPL sceAudiocodecGetInfo(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecCheckNeedMem(u32 ctxPtr, int codec) {
	WARN_LOG(ME, "UNIMPL sceAudiocodecCheckNeedMem(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecGetEDRAM(u32 ctxPtr, int codec) {
	WARN_LOG(ME, "UNIMPL sceAudiocodecGetEDRAM(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecReleaseEDRAM(u32 ctxPtr, int id) {
	//id is not always a codec, so what is should be exactly? 
	//AudioClose(&audio); can not be used here, since it will crash some games.
	WARN_LOG(ME, "UNIMPL sceAudiocodecReleaseEDRAM(%08x, %i)", ctxPtr, id);
	return 0;
}

const HLEFunction sceAudiocodec[] = {
	{0x70A703F8, WrapI_UI<sceAudiocodecDecode>, "sceAudiocodecDecode"},
	{0x5B37EB1D, WrapI_UI<sceAudiocodecInit>, "sceAudiocodecInit"},
	{0x8ACA11D5, WrapI_UI<sceAudiocodecGetInfo>, "sceAudiocodecGetInfo"},
	{0x3A20A200, WrapI_UI<sceAudiocodecGetEDRAM>, "sceAudiocodecGetEDRAM" },
	{0x29681260, WrapI_UI<sceAudiocodecReleaseEDRAM>, "sceAudiocodecReleaseEDRAM" },
	{0x9D3F790C, WrapI_UI<sceAudiocodecCheckNeedMem>, "sceAudiocodecCheckNeedMem" },
	{0x59176a0f, 0, "sceAudiocodec_59176A0F"},
};

void Register_sceAudiocodec()
{
	RegisterModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}
