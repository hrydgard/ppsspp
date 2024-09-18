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

// audioList is to store current playing audios.
static std::map<u32, AudioDecoder *> audioList;

static bool oldStateLoaded = false;

// find the audio decoder for corresponding ctxPtr in audioList
static AudioDecoder *findDecoder(u32 ctxPtr) {
	auto it = audioList.find(ctxPtr);
	if (it != audioList.end()) {
		return it->second;
	}
	return NULL;
}

// remove decoder from audioList
static bool removeDecoder(u32 ctxPtr) {
	auto it = audioList.find(ctxPtr);
	if (it != audioList.end()) {
		delete it->second;
		audioList.erase(it);
		return true;
	}
	return false;
}

static void clearDecoders() {
	for (const auto &[_, decoder] : audioList) {
		delete decoder;
	}
	audioList.clear();
}

void __AudioCodecInit() {
	oldStateLoaded = false;
}

void __AudioCodecShutdown() {
	// We need to kill off any still opened codecs to not leak memory.
	clearDecoders();
}

static int sceAudiocodecInit(u32 ctxPtr, int codec) {
	PSPAudioType audioType = (PSPAudioType)codec;
	if (IsValidCodec(audioType)) {
		// Create audio decoder for given audio codec and push it into AudioList
		if (removeDecoder(ctxPtr)) {
			WARN_LOG_REPORT(Log::HLE, "sceAudiocodecInit(%08x, %d): replacing existing context", ctxPtr, codec);
		}
		AudioDecoder *decoder = CreateAudioDecoder(audioType);
		decoder->SetCtxPtr(ctxPtr);
		audioList[ctxPtr] = decoder;
		INFO_LOG(Log::ME, "sceAudiocodecInit(%08x, %i (%s))", ctxPtr, codec, GetCodecName(audioType));
		DEBUG_LOG(Log::ME, "Number of playing sceAudioCodec audios : %d", (int)audioList.size());
		return 0;
	}
	ERROR_LOG_REPORT(Log::ME, "sceAudiocodecInit(%08x, %i (%s)): Unknown audio codec %i", ctxPtr, codec, GetCodecName(audioType), codec);
	return 0;
}

static int sceAudiocodecDecode(u32 ctxPtr, int codec) {
	PSPAudioType audioType = (PSPAudioType)codec;
	if (!ctxPtr){
		ERROR_LOG_REPORT(Log::ME, "sceAudiocodecDecode(%08x, %i (%s)) got NULL pointer", ctxPtr, codec, GetCodecName(audioType));
		return -1;
	}

	if (IsValidCodec(audioType)){
		// find a decoder in audioList
		auto decoder = findDecoder(ctxPtr);

		if (!decoder && oldStateLoaded) {
			// We must have loaded an old state that did not have sceAudiocodec information.
			// Fake it by creating the desired context.
			decoder = CreateAudioDecoder(audioType);
			decoder->SetCtxPtr(ctxPtr);
			audioList[ctxPtr] = decoder;
		}

		if (decoder != NULL) {
			// Use SimpleAudioDec to decode audio
			auto ctx = PSPPointer<AudioCodecContext>::Create(ctxPtr);  // On stack, no need to allocate.
			// Decode audio
			int inDataConsumed = 0;
			int outSamples = 0;
			decoder->Decode(Memory::GetPointer(ctx->inDataPtr), ctx->inDataSize, &inDataConsumed, 2, (int16_t *)Memory::GetPointerWrite(ctx->outDataPtr), &outSamples);
		}
		DEBUG_LOG(Log::ME, "sceAudiocodecDec(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
		return 0;
	}
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceAudiocodecDecode(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

static int sceAudiocodecGetInfo(u32 ctxPtr, int codec) {
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceAudiocodecGetInfo(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

static int sceAudiocodecCheckNeedMem(u32 ctxPtr, int codec) {
	WARN_LOG(Log::ME, "UNIMPL sceAudiocodecCheckNeedMem(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

static int sceAudiocodecGetEDRAM(u32 ctxPtr, int codec) {
	WARN_LOG(Log::ME, "UNIMPL sceAudiocodecGetEDRAM(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

static int sceAudiocodecReleaseEDRAM(u32 ctxPtr, int id) {
	if (removeDecoder(ctxPtr)){
		INFO_LOG(Log::ME, "sceAudiocodecReleaseEDRAM(%08x, %i)", ctxPtr, id);
		return 0;
	}
	WARN_LOG(Log::ME, "UNIMPL sceAudiocodecReleaseEDRAM(%08x, %i)", ctxPtr, id);
	return 0;
}

const HLEFunction sceAudiocodec[] = {
	{0X70A703F8, &WrapI_UI<sceAudiocodecDecode>,       "sceAudiocodecDecode",       'i', "xi"},
	{0X5B37EB1D, &WrapI_UI<sceAudiocodecInit>,         "sceAudiocodecInit",         'i', "xi"},
	{0X8ACA11D5, &WrapI_UI<sceAudiocodecGetInfo>,      "sceAudiocodecGetInfo",      'i', "xi"},
	{0X3A20A200, &WrapI_UI<sceAudiocodecGetEDRAM>,     "sceAudiocodecGetEDRAM",     'i', "xi"},
	{0X29681260, &WrapI_UI<sceAudiocodecReleaseEDRAM>, "sceAudiocodecReleaseEDRAM", 'i', "xi"},
	{0X9D3F790C, &WrapI_UI<sceAudiocodecCheckNeedMem>, "sceAudiocodecCheckNeedMem", 'i', "xi"},
	{0X59176A0F, nullptr,                              "sceAudiocodecAlcExtendParameter", '?', ""  },
};

void Register_sceAudiocodec()
{
	RegisterModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}

void __sceAudiocodecDoState(PointerWrap &p){
	auto s = p.Section("AudioList", 0, 2);
	if (!s) {
		oldStateLoaded = true;
		return;
	}

	int count = (int)audioList.size();
	Do(p, count);

	if (count > 0) {
		if (p.mode == PointerWrap::MODE_READ) {
			clearDecoders();

			// loadstate if audioList is nonempty
			auto codec_ = new int[count];
			auto ctxPtr_ = new u32[count];
			// These sizeof(pointers) are wrong, but kept to avoid breaking on old saves.
			// They're not used in new savestates.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wsizeof-pointer-div"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsizeof-pointer-div"
#endif
			DoArray(p, codec_, s >= 2 ? count : (int)ARRAY_SIZE(codec_));
			DoArray(p, ctxPtr_, s >= 2 ? count : (int)ARRAY_SIZE(ctxPtr_));
			for (int i = 0; i < count; i++) {
				auto decoder = CreateAudioDecoder((PSPAudioType)codec_[i]);
				decoder->SetCtxPtr(ctxPtr_[i]);
				audioList[ctxPtr_[i]] = decoder;
			}
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
			for (auto iter : audioList) {
				const AudioDecoder *decoder = iter.second;
				codec_[i] = decoder->GetAudioType();
				ctxPtr_[i] = decoder->GetCtxPtr();
				i++;
			}
			DoArray(p, codec_, count);
			DoArray(p, ctxPtr_, count);
			delete[] codec_;
			delete[] ctxPtr_;
		}
	}
}
