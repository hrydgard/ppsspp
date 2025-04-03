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
#include "Core/HLE/ErrorCodes.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/HW/SimpleAudioDec.h"

// Following kaien_fr's sample code https://github.com/hrydgard/ppsspp/issues/5620#issuecomment-37086024
// Should probably store the EDRAM get/release status somewhere within here, etc.

// g_audioDecoderContexts is to store current playing audios.
std::map<u32, AudioDecoder *> g_audioDecoderContexts;

static bool oldStateLoaded = false;

static_assert(sizeof(SceAudiocodecCodec) == 128);

// Atrac3+ (0x1000) frame sizes, and control bytes
//
// Bitrate    Frame Size    Byte 1     Byte 2  Channels
// -----------------------------------------------------
// 48kbps     0x118           0x24       0x22     1?         // This hits "Frame data doesn't match channel configuration".
// 64kbps     0x178
// 96kbps?    0x230           0x28       0x45     2
// 128kbps    0x2E8           0x28       0x5c     2
//
// Seems like maybe the frame size is equal to "Byte 2" * 8 + 8
//
// Known byte values.

void CalculateInputBytesAndChannelsAt3Plus(const SceAudiocodecCodec *ctx, int *inputBytes, int *channels) {
	*inputBytes = 0;
	*channels = 2;

	int size = ctx->unk41 * 8 + 8;
	// No idea if this is accurate, this is just a guess...
	if (ctx->unk40 & 8) {
		*channels = 2;
	} else {
		*channels = 1;
	}
	switch (size) {
	case 0x118:
	case 0x178:
	case 0x230:
	case 0x2E8:
		// These have been seen before, let's return it.
		*inputBytes = size;
		return;
	default:
		break;
	}
}

// find the audio decoder for corresponding ctxPtr in audioList
static AudioDecoder *findDecoder(u32 ctxPtr) {
	auto it = g_audioDecoderContexts.find(ctxPtr);
	if (it != g_audioDecoderContexts.end()) {
		return it->second;
	}
	return NULL;
}

// remove decoder from audioList
static bool removeDecoder(u32 ctxPtr) {
	auto it = g_audioDecoderContexts.find(ctxPtr);
	if (it != g_audioDecoderContexts.end()) {
		delete it->second;
		g_audioDecoderContexts.erase(it);
		return true;
	}
	return false;
}

static void clearDecoders() {
	for (const auto &[_, decoder] : g_audioDecoderContexts) {
		delete decoder;
	}
	g_audioDecoderContexts.clear();
}

void __AudioCodecInit() {
	oldStateLoaded = false;
}

void __AudioCodecShutdown() {
	// We need to kill off any still opened codecs to not leak memory.
	clearDecoders();
}

// TODO: Actually support mono output.
static int __AudioCodecInitCommon(u32 ctxPtr, int codec, bool mono) {
	const PSPAudioType audioType = (PSPAudioType)codec;
	if (!IsValidCodec(audioType)) {
		return hleLogError(Log::ME, SCE_KERNEL_ERROR_OUT_OF_RANGE, "Invalid codec");
	}

	if (removeDecoder(ctxPtr)) {
		WARN_LOG_REPORT(Log::HLE, "sceAudiocodecInit(%08x, %d): replacing existing context", ctxPtr, codec);
	}

	// Initialize the codec memory.
	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);
	ctx->unk_init = 0x5100601;  // Firmware version indicator?
	ctx->err = 0;

	int inFrameBytes = 0;
	int channels = 2;

	// Special actions for some codecs.
	switch (audioType) {
	case PSP_CODEC_MP3:
		ctx->mp3_9999 = 9999;
		break;
	case PSP_CODEC_AAC:
		// AAC / mp4
		// offsets 40-42 are a 24-bit LE number specifying the sample rate. It's 32000, 44100 or 48000.
		// neededMem has been set to 0x18f20.
		break;
	case PSP_CODEC_AT3PLUS:
		CalculateInputBytesAndChannelsAt3Plus(ctx, &inFrameBytes, &channels);
		break;
	}

	// Create audio decoder for given audio codec and push it into AudioList
	INFO_LOG(Log::ME, "sceAudioDecoder: Creating codec with %04x frame size and %d channels, codec %04x", inFrameBytes, channels, codec);
	AudioDecoder *decoder = CreateAudioDecoder(audioType, 44100, channels, inFrameBytes);
	decoder->SetCtxPtr(ctxPtr);
	g_audioDecoderContexts[ctxPtr] = decoder;
	return hleLogDebug(Log::ME, 0);
}

static int sceAudiocodecInit(u32 ctxPtr, int codec) {
	return __AudioCodecInitCommon(ctxPtr, codec, false);
}

static int sceAudiocodecInitMono(u32 ctxPtr, int codec) {
	return __AudioCodecInitCommon(ctxPtr, codec, true);
}

static int sceAudiocodecDecode(u32 ctxPtr, int codec) {
	PSPAudioType audioType = (PSPAudioType)codec;
	if (!ctxPtr) {
		ERROR_LOG(Log::ME, "sceAudiocodecDecode(%08x, %i (%s)) got NULL pointer", ctxPtr, codec, GetCodecName(audioType));
		return -1;
	}

	if (!IsValidCodec(audioType)) {
		return hleLogError(Log::ME, 0, "UNIMPL sceAudiocodecDecode(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	}

	// TODO: Should check that codec corresponds to the currently used codec in the context, I guess..

	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);  // On game-owned heap, no need to allocate.

	int inFrameBytes = 0;
	int channels = 2;

	switch (codec) {
	case PSP_CODEC_AT3PLUS:
		CalculateInputBytesAndChannelsAt3Plus(ctx, &inFrameBytes, &channels);
		break;
	case PSP_CODEC_MP3:
		inFrameBytes = ctx->srcBytesRead;
		break;
	}

	// find a decoder in audioList
	auto decoder = findDecoder(ctxPtr);

	if (!decoder && oldStateLoaded) {
		// We must have loaded an old state that did not have sceAudiocodec information.
		// Fake it by creating the desired context.
		decoder = CreateAudioDecoder(audioType, 44100, channels, inFrameBytes);
		decoder->SetCtxPtr(ctxPtr);
		g_audioDecoderContexts[ctxPtr] = decoder;
	}

	if (decoder) {
		// Use SimpleAudioDec to decode audio
		// Decode audio
		int inDataConsumed = 0;
		int outSamples = 0;

		INFO_LOG(Log::ME, "decoder. in: %08x out: %08x unk40: %d unk41: %d", ctx->inBuf, ctx->outBuf, ctx->unk40, ctx->unk41);

		int16_t *outBuf = (int16_t *)Memory::GetPointerWrite(ctx->outBuf);

		bool result = decoder->Decode(Memory::GetPointer(ctx->inBuf), inFrameBytes, &inDataConsumed, 2, outBuf, &outSamples);
		if (!result) {
			ctx->err = 0x20b;
			ERROR_LOG(Log::ME, "AudioCodec decode failed. Setting error to %08x", ctx->err);
		}

		ctx->srcBytesRead = inDataConsumed;
		ctx->dstSamplesWritten = outSamples;
	}
	return hleLogInfo(Log::ME, 0, "codec %s", GetCodecName(codec));
}

static int sceAudiocodecGetInfo(u32 ctxPtr, int codec) {
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceAudiocodecGetInfo(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

static int sceAudiocodecCheckNeedMem(u32 ctxPtr, int codec) {
	if (codec < 0x1000 || codec >= 0x1006) {
		return hleLogError(Log::ME, 0x80000004, "invalid codec");
	}

	if (!Memory::IsValidRange(ctxPtr, sizeof(SceAudiocodecCodec))) {
		return hleLogError(Log::ME, 0, "Bad address");
	}

	// Check for expected values.
	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);  // On game-owned heap, no need to allocate.

	switch (codec) {
	case 0x1000:
		ctx->neededMem = 0x7bc0;
		if (ctx->unk40 != 0x28 || ctx->unk41 != 0x5c) {
			ctx->err = 0x20f;
			return hleLogError(Log::ME, SCE_AVCODEC_ERROR_INVALID_DATA, "Bad format values: %02x %02x", ctx->unk40, ctx->unk41);
		}
		break;
	case 0x1001:
		ctx->neededMem = 0x3de0;
		break;
	case 0x1003:
		// Kosmodrones uses sceAudiocodec directly (no intermediate library).
		INFO_LOG(Log::ME, "CheckNeedMem for codec %04x: format %02x %02x", ctx->unk40, ctx->unk41);
		break;
	}

	ctx->err = 0;
	ctx->unk_init = 0x5100601;

	return hleLogWarning(Log::ME, 0, "%s", GetCodecName(codec));
}

static int sceAudiocodecGetEDRAM(u32 ctxPtr, int codec) {
	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);  // On game-owned heap, no need to allocate.
	// TODO: Set this a bit more dynamically. No idea what the allocation algorithm is...
	ctx->allocMem = 0x0018ea90;
	ctx->edramAddr = (ctx->allocMem + 0x3f) & ~0x3f;  // round up to 64 bytes.
	return hleLogInfo(Log::ME, 0, "edram address set to %08x", ctx->edramAddr);
}

static int sceAudiocodecReleaseEDRAM(u32 ctxPtr, int id) {
	if (removeDecoder(ctxPtr)){
		return hleLogInfo(Log::ME, 0);
	}
	return hleLogWarning(Log::ME, 0, "failed to remove decoder");
}

static int sceAudiocodecGetOutputBytes(u32 ctxPtr, int codec) {
	switch (codec) {
	case 0x1000: return hleLogInfo(Log::ME, 0x2000);  // Atrac3+
	case 0x1001: return hleLogInfo(Log::ME, 0x1000);  // Atrac3
	default:
		return hleLogWarning(Log::ME, 0, "Block size query not implemented for codec %04x", codec);
	}
	return hleLogInfo(Log::ME, 0);
}

const HLEFunction sceAudiocodec[] = {
	{0X70A703F8, &WrapI_UI<sceAudiocodecDecode>,         "sceAudiocodecDecode",       'i', "xi"},
	{0X5B37EB1D, &WrapI_UI<sceAudiocodecInit>,           "sceAudiocodecInit",         'i', "xi"},
	{0X8ACA11D5, &WrapI_UI<sceAudiocodecGetInfo>,        "sceAudiocodecGetInfo",      'i', "xi"},
	{0X3A20A200, &WrapI_UI<sceAudiocodecGetEDRAM>,       "sceAudiocodecGetEDRAM",     'i', "xi"},
	{0X29681260, &WrapI_UI<sceAudiocodecReleaseEDRAM>,   "sceAudiocodecReleaseEDRAM", 'i', "xi"},
	{0X9D3F790C, &WrapI_UI<sceAudiocodecCheckNeedMem>,   "sceAudiocodecCheckNeedMem", 'i', "xi"},
	{0X59176A0F, &WrapI_UI<sceAudiocodecGetOutputBytes>, "sceAudiocodecGetOutputBytes", 'i', "xxx" },  // params are context, codec, outptr
	{0X3DD7EE1A, &WrapI_UI<sceAudiocodecInitMono>,       "sceAudiocodecInitMono",     'i', "xi"},  // Used by sceAtrac for MOut* functions.
};

void Register_sceAudiocodec() {
	RegisterHLEModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}

void __sceAudiocodecDoState(PointerWrap &p){
	auto s = p.Section("AudioList", 0, 2);
	if (!s) {
		oldStateLoaded = true;
		return;
	}

	int count = (int)g_audioDecoderContexts.size();
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
				g_audioDecoderContexts[ctxPtr_[i]] = decoder;
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
			for (auto iter : g_audioDecoderContexts) {
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
