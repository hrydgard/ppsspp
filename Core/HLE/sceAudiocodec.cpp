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
#include "Common/ChunkFile.h"

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

// audioList is to store current playing audios.
static std::list<SimpleAudio *> audioList;

static bool oldStateLoaded = false;

// find the audio decoder for corresponding ctxPtr in audioList
SimpleAudio * findDecoder(u32 ctxPtr){
	for (std::list<SimpleAudio *>::iterator it = audioList.begin(); it != audioList.end(); it++){
		if ((*it)->ctxPtr == ctxPtr){
			return (*it);
		}
	}
	return NULL;
}

// remove decoder from audioList
bool removeDecoder(u32 ctxPtr){
	for (std::list<SimpleAudio *>::iterator it = audioList.begin(); it != audioList.end(); it++){
		if ((*it)->ctxPtr == ctxPtr){
			delete *it;
			audioList.erase(it);
			return true;
		}
	}
	return false;
}

void __AudioCodecInit() {
	oldStateLoaded = false;
}

void __AudioCodecShutdown() {
	// We need to kill off any still opened codecs to not leak memory.
	for (std::list<SimpleAudio *>::iterator it = audioList.begin(); it != audioList.end(); it++){
		delete *it;
	}
	audioList.clear();
}

int sceAudiocodecInit(u32 ctxPtr, int codec) {
	if (isValidCodec(codec)){
		// Create audio decoder for given audio codec and push it into AudioList
		auto decoder = new SimpleAudio(ctxPtr, codec);
		audioList.push_front(decoder);
		INFO_LOG(ME, "sceAudiocodecInit(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
		DEBUG_LOG(ME, "Number of playing sceAudioCodec audios : %d", audioList.size());
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

	if (isValidCodec(codec)){
		// Use SimpleAudioDec to decode audio
		AudioCodecContext ctx;  // On stack, no need to allocate.
		Memory::ReadStruct(ctxPtr, &ctx);
		int outbytes = 0;
		// find a decoder in audioList
		auto decoder = findDecoder(ctxPtr);

		if (!decoder && oldStateLoaded) {
			// We must have loaded an old state that did not have sceAudioCodec information.
			// Fake it by creating the desired context.
			decoder = new SimpleAudio(ctxPtr, codec);
			audioList.push_front(decoder);
		}

		if (decoder != NULL){
			// Decode audio
			decoder->Decode(Memory::GetPointer(ctx.inDataPtr), ctx.inDataSize, Memory::GetPointer(ctx.outDataPtr), &outbytes);
			DEBUG_LOG(ME, "sceAudiocodecDec(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
		}
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
	if (removeDecoder(ctxPtr)){
		INFO_LOG(ME, "sceAudiocodecReleaseEDRAM(%08x, %i)", ctxPtr, id);
		return 0;
	}
	WARN_LOG(ME, "UNIMPL sceAudiocodecReleaseEDRAM(%08x, %i)", ctxPtr, id);
	return 0;
}

const HLEFunction sceAudiocodec[] = {
	{ 0x70A703F8, WrapI_UI<sceAudiocodecDecode>, "sceAudiocodecDecode" },
	{ 0x5B37EB1D, WrapI_UI<sceAudiocodecInit>, "sceAudiocodecInit" },
	{ 0x8ACA11D5, WrapI_UI<sceAudiocodecGetInfo>, "sceAudiocodecGetInfo" },
	{ 0x3A20A200, WrapI_UI<sceAudiocodecGetEDRAM>, "sceAudiocodecGetEDRAM" },
	{ 0x29681260, WrapI_UI<sceAudiocodecReleaseEDRAM>, "sceAudiocodecReleaseEDRAM" },
	{ 0x9D3F790C, WrapI_UI<sceAudiocodecCheckNeedMem>, "sceAudiocodecCheckNeedMem" },
	{ 0x59176a0f, 0, "sceAudiocodec_59176A0F" },
};

void Register_sceAudiocodec()
{
	RegisterModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}

void __sceAudiocodecDoState(PointerWrap &p){
	auto s = p.Section("AudioList", 0, 1);
	if (!s) {
		if (p.mode == PointerWrap::MODE_READ)
			oldStateLoaded = true;
		return;
	}

	auto count = (int)audioList.size();
	p.Do(count);
	if (count > 0) {
		if (p.mode == PointerWrap::MODE_READ){
			// loadstate if audioList is nonempty
			auto codec_ = new int[count];
			auto ctxPtr_ = new u32[count];
			p.DoArray(codec_, ARRAY_SIZE(codec_));
			p.DoArray(ctxPtr_, ARRAY_SIZE(ctxPtr_));
			for (int i = 0; i < count; i++){
				auto decoder = new SimpleAudio(ctxPtr_[i], codec_[i]);
				audioList.push_front(decoder);
			}
			delete[] codec_;
			delete[] ctxPtr_;
		}
		else
		{
			// savestate if audioList is nonempty
			// Some of this is only necessary in Write but won't really hurt Measure.
			auto codec_ = new int[count];
			auto ctxPtr_ = new u32[count];
			int i = 0;
			for (std::list<SimpleAudio *>::iterator it = audioList.begin(); it != audioList.end(); it++){
				codec_[i] = (*it)->audioType;
				ctxPtr_[i] = (*it)->ctxPtr;
				i++;
			}
			p.DoArray(codec_, ARRAY_SIZE(codec_));
			p.DoArray(ctxPtr_, ARRAY_SIZE(ctxPtr_));
			delete[] codec_;
			delete[] ctxPtr_;
		}
	}
}

void resetAudioList(){
	audioList.clear();
	INFO_LOG(ME, "Audio playing list is reset");
}
