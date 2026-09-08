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
// The Atrac3+ frame size each decoder in the map above was created for. mpeg.prx doesn't put the
// frame size in the context, and at init time the input buffer is still empty, so for that path we
// only learn it from the first frame - at which point the decoder has to be rebuilt to match.
static std::map<u32, int> g_at3PlusFrameBytes;

static bool oldStateLoaded = false;

static_assert(sizeof(SceAudiocodecCodec) == 128);
// Games allocate this structure and the ME writes into it, so every offset is load-bearing.
static_assert(offsetof(SceAudiocodecCodec, inBuf) == 0x18);
static_assert(offsetof(SceAudiocodecCodec, outBuf) == 0x20);
static_assert(offsetof(SceAudiocodecCodec, fmt) == 0x28);
static_assert(offsetof(SceAudiocodecCodec, fmt.at3.at3Related) == 0x30);
static_assert(offsetof(SceAudiocodecCodec, fmt.mp3.version) == 0x38);
static_assert(offsetof(SceAudiocodecCodec, fmt.mp3.bitrateIndex) == 0x44);
static_assert(offsetof(SceAudiocodecCodec, fmt.mp3.sampleRateIndex) == 0x48);
static_assert(offsetof(SceAudiocodecCodec, fmt.mp3.channelConfig) == 0x54);
static_assert(offsetof(SceAudiocodecCodec, allocMem) == 0x68);

// Notes on the codec-specific fields, from watching games and from reading the firmware modules
// that drive this interface (avcodec.prx, libatrac3plus.prx, libmp3.prx). See the union in
// sceAudiocodec.h for the layout.
//
// A general point worth knowing: the hardware never needs an exact input frame size here. The
// only thing sceAudiocodecDecode does with the size is a cache writeback over inBuf before
// handing the frame to the ME, so every "size" the firmware computes or stores is an upper
// bound.

// Atrac3+ (0x1000) frame sizes, and control bytes
//
// Bitrate    Frame Size    Byte 1     Byte 2  Channels
// -----------------------------------------------------
// 48kbps     0x118           0x24       0x22     1?         // This hits "Frame data doesn't match channel configuration".
// 64kbps     0x178          (0x2e implied by the formula below)
// 96kbps?    0x230           0x28       0x45     2
// 128kbps    0x2E8           0x28       0x5c     2
//
// The frame size really is "Byte 2" * 8 + 8 - it holds for all three rows we have both numbers
// for, and libatrac3plus.prx writes 0x28/0x5c (the 128kbps row) into these two bytes at init as
// the worst case it sizes its EDRAM allocation against. So byte 2 is the hardware's own frame
// descriptor and reading it, as we do, is the right thing.
//
// The channel guess below (bit 3 of byte 1) fits both data points we have and nothing else.

// Atrac3 (0x1001)
//
// The Media Engine only ever sees the first 0x68 bytes of this structure - me_wrapper.prx does
// sceKernelDcacheWritebackInvalidateRange(ctx, 0x68) before handing it over, and avcodec.prx
// validates the same range - so whatever the hardware uses to size an Atrac3 frame has to be in
// there. The only Atrac3-specific thing anything writes is the joint-stereo flag in byte 1.
//
// Frame size          data byte           JointStereo?
// -------------------------------------------------
// 0x180               0x04                0
// 0x130               0x06                0
// 0x0C0               0x0B                1
// 0x0C0               0x0E                0
// 0x098               0x0F                0
//
// NOTE: sceAudiocodecDecode below hardcodes 384 (0x180) bytes per frame for Atrac3, which is only
// the first row. If a game ever drives Atrac3 through sceAudiocodec at one of the other sizes we
// will decode garbage.

// AAC (0x1003)
// ------------------------------------------------
// Sample rate is at offset 0x28.
// srcBytesConsumed can be very small the first frames.
// 0x1000 is always the frame size.
// The firmware sizes AAC from the bytes at 0x2c and 0x2d rather than from 0x28: input is
// 0x600 or 0x609, output 0x1000 or 0x2000, depending on those two. Consistent with the above.

// MP3 (0x1002)
// ------------------------------------------------
// The parameters live at 0x38 (MPEG version index: 0 = MPEG2, 1 = MPEG1, 2 = MPEG2.5), 0x44
// (bitrate index) and 0x48 (sample rate index), and index the standard MPEG Layer III tables.
// sceAudiocodecInit presets 0x38 to 9999 meaning "not known yet", and GetInfo fills these in -
// which is why our GetInfo writes plausible values for a 128kbps 44.1kHz stereo stream.
// 0x54 is the channel configuration: libmp3.prx reads it as (value == 3) ? mono : stereo.
// 0x28 is not this frame's size - libmp3.prx writes 0x5A1 there once, the largest an MP3 frame
// can ever be. Output is 0x1200 bytes for MPEG1 (1152 samples) and 0x900 otherwise (576).

void CalculateInputBytesAndChannelsAt3Plus(const SceAudiocodecCodec *ctx, int *inputBytes, int *channels, int *headerBytes = nullptr) {
	*inputBytes = 0;
	*channels = 2;
	if (headerBytes) {
		*headerBytes = 0;
	}

	u8 formatByte1 = ctx->fmt.at3.formatByte1;
	u8 formatByte2 = ctx->fmt.at3.formatByte2;

	// Atrac3+ frames inside a PSMF still carry their 8-byte header, starting with the 0x0FD0 sync
	// word; libatrac3plus.prx strips it before handing the frame over, mpeg.prx leaves it on for
	// the hardware to parse. So when the sync word is still there, take the size from the frame
	// and step over the header, exactly as MpegDemux does on the HLE path. Bytes 2 and 3 are the
	// same pair that ends up in the context, but with two more size bits in the first of them.
	const u8 *frame = Memory::IsValidRange(ctx->inBuf, 4) ? Memory::GetPointerUnchecked(ctx->inBuf) : nullptr;
	if (frame && frame[0] == 0x0F && frame[1] == 0xD0) {
		formatByte1 = frame[2];
		formatByte2 = frame[3];
		if (headerBytes) {
			*headerBytes = 8;
		}
		// The full size, unlike the context's, has two more high bits in the first byte. The
		// 0x10 is the header plus the 8 the context's own formula adds.
		*channels = (formatByte1 & 8) ? 2 : 1;
		*inputBytes = (((formatByte1 & 0x03) << 8) | (formatByte2 * 8)) + 0x10 - 8;
		return;
	}

	int size = formatByte2 * 8 + 8;
	// No idea if this is accurate, this is just a guess...
	if (formatByte1 & 8) {
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

// Atrac3 (0x1001). Unlike Atrac3+, the context doesn't carry a frame size - libatrac3plus.prx
// (and our mirror of it in AtracCtx2) writes only the joint-stereo flag into formatByte1 for
// Atrac3, so in general the size can't be recovered from the context alone.
//
// One case is unambiguous, though. Of the five frame sizes the hardware supports, exactly one is
// joint stereo (66kbps stereo, 0xC0 bytes), so that flag pins the size down by itself. Everything
// else falls back to the 132kbps stereo frame, which is what the old hardcoded 384 was.
static int Atrac3BytesPerFrameFromContext(const SceAudiocodecCodec *ctx, int *channels);

// The MPEG sample rates, indexed by [version][sampleRateIndex] exactly as the hardware's own
// table in avcodec.prx does. Version is 0 = MPEG2, 1 = MPEG1, 2 = MPEG2.5.
static const int g_mpegSampleRates[3][4] = {
	{ 22050, 24000, 16000, 0 },
	{ 44100, 48000, 32000, 0 },
	{ 11025, 12000,  8000, 0 },
};

// Returns 0 if the context doesn't describe a rate we recognize - which includes the common case
// where sceAudiocodecInit has run but GetInfo hasn't filled the fields in yet (version is 9999).
static int Mp3SampleRateFromContext(const SceAudiocodecCodec *ctx) {
	const int version = ctx->fmt.mp3.version;
	const int index = ctx->fmt.mp3.sampleRateIndex;
	if (version < 0 || version >= 3 || index < 0 || index >= 4) {
		return 0;
	}
	return g_mpegSampleRates[version][index];
}

// libmp3.prx reads the channel configuration this way.
static int Mp3ChannelsFromContext(const SceAudiocodecCodec *ctx) {
	return ctx->fmt.mp3.channelConfig == 3 ? 1 : 2;
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
		g_at3PlusFrameBytes.erase(ctxPtr);
		return true;
	}
	return false;
}

static void clearDecoders() {
	for (const auto &[_, decoder] : g_audioDecoderContexts) {
		delete decoder;
	}
	g_audioDecoderContexts.clear();
	g_at3PlusFrameBytes.clear();
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
	ctx->magic = 0x5100601;
	ctx->err = 0;

	int bytesPerFrame = 0;
	int channels = 2;

	uint8_t extraData[14]{};

	SceAudiocodecCodec *ptr = ctx;
	// Special actions for some codecs.
	switch (audioType) {
	case PSP_CODEC_MP3:
		// Not seeing inited in Kurok (homebrew)
		// _dbg_assert_(ctx->inited == 1);
		ctx->fmt.mp3.version = 9999;
		break;
	case PSP_CODEC_AAC:
		// AAC / mp4
		// offsets 40-42 are a 24-bit LE number specifying the sample rate. It's 32000, 44100 or 48000.
		// neededMem has been set to 0x18f20.
		break;
	case PSP_CODEC_AT3PLUS:
		CalculateInputBytesAndChannelsAt3Plus(ctx, &bytesPerFrame, &channels);
		break;
	case PSP_CODEC_AT3:
	{
		// See AtracBase::CreateDecoder. The context only tells us whether the stream is joint
		// stereo, which pins the frame size down in that one case - see the function.
		bytesPerFrame = Atrac3BytesPerFrameFromContext(ctx, &channels);
		bool jointStereo = IsAtrac3StreamJointStereo(PSP_CODEC_AT3, bytesPerFrame, channels);
		// The only thing that changes are the jointStereo_ values.
		extraData[0] = 1;
		extraData[3] = channels << 3;
		extraData[6] = jointStereo;
		extraData[8] = jointStereo;
		extraData[10] = 1;
		break;
	}
	default:
		break;
	}

	// Create audio decoder for given audio codec and push it into AudioList
	INFO_LOG(Log::ME, "sceAudioDecoder: Creating codec with %04x frame size and %d channels, codec %04x", bytesPerFrame, channels, codec);
	// We send in extra data with all codec, most ignore it.
	AudioDecoder *decoder = CreateAudioDecoder(audioType, 44100, channels, bytesPerFrame, extraData, sizeof(extraData));
	decoder->SetCtxPtr(ctxPtr);
	g_audioDecoderContexts[ctxPtr] = decoder;
	g_at3PlusFrameBytes[ctxPtr] = bytesPerFrame;
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

	int bytesPerFrame = 0;
	int channels = 2;
	int sampleRate = 0;
	int headerBytes = 0;

	switch (codec) {
	case PSP_CODEC_AT3PLUS:
		CalculateInputBytesAndChannelsAt3Plus(ctx, &bytesPerFrame, &channels, &headerBytes);
		break;
	case PSP_CODEC_MP3:
		// Not srcBytesRead - that's an output field holding what the *previous* call consumed.
		// The hardware uses the bound at 0x28, which the caller also guarantees is readable at
		// inBuf (it does a cache writeback over exactly that range), so it's the safe length to
		// hand a decoder that parses the frame header itself.
		bytesPerFrame = ctx->fmt.mp3.maxFrameBytes;
		if (bytesPerFrame <= 0) {
			bytesPerFrame = ctx->srcBytesRead;
		}
		channels = Mp3ChannelsFromContext(ctx);
		sampleRate = Mp3SampleRateFromContext(ctx);
		break;
	case PSP_CODEC_AAC:
		bytesPerFrame = ctx->srcBytesRead;
		sampleRate = ctx->fmt.aac.sampleRate;
		break;
	case PSP_CODEC_AT3:
		bytesPerFrame = Atrac3BytesPerFrameFromContext(ctx, &channels);
		break;
	}

	// find a decoder in audioList
	auto decoder = findDecoder(ctxPtr);

	if (!decoder && oldStateLoaded) {
		// We must have loaded an old state that did not have sceAudiocodec information.
		// Fake it by creating the desired context.
		decoder = CreateAudioDecoder(audioType, 44100, channels, bytesPerFrame);
		decoder->SetCtxPtr(ctxPtr);
		g_audioDecoderContexts[ctxPtr] = decoder;
	}

	if (decoder && codec == PSP_CODEC_AT3PLUS && bytesPerFrame > 0) {
		auto it = g_at3PlusFrameBytes.find(ctxPtr);
		if (it == g_at3PlusFrameBytes.end() || it->second != bytesPerFrame) {
			// Only reachable when the context didn't carry a frame size at init - mpeg.prx.
			INFO_LOG(Log::ME, "sceAudiocodecDecode: Atrac3+ frame is %04x bytes, rebuilding decoder", bytesPerFrame);
			removeDecoder(ctxPtr);
			decoder = CreateAudioDecoder(audioType, 44100, channels, bytesPerFrame);
			decoder->SetCtxPtr(ctxPtr);
			g_audioDecoderContexts[ctxPtr] = decoder;
			g_at3PlusFrameBytes[ctxPtr] = bytesPerFrame;
		}
	}

	if (decoder) {
		// Use SimpleAudioDec to decode audio
		// Decode audio
		int inDataConsumed = 0;
		int outSamples = 0;

		DEBUG_LOG(Log::ME, "decoder. in: %08x out: %08x format: %02x %02x", ctx->inBuf, ctx->outBuf, ctx->fmt.at3.formatByte1, ctx->fmt.at3.formatByte2);

		int16_t *outBuf = (int16_t *)Memory::GetPointerWriteOrException(ctx->outBuf);

		bool result = decoder->Decode(Memory::GetPointerOrException(ctx->inBuf + headerBytes), bytesPerFrame, &inDataConsumed, 2, outBuf, &outSamples);
		if (!result) {
			ctx->err = 0x20b;
			ERROR_LOG(Log::ME, "AudioCodec decode failed. Setting error to %08x", ctx->err);
		}

		ctx->srcBytesRead = inDataConsumed + headerBytes;
		ctx->dstSamplesWritten = outSamples;
	}
	return hleLogDebug(Log::ME, 0, "codec %s sampleRate: %d bytesPerFrame: %d channels: %d", GetCodecName(codec), sampleRate, bytesPerFrame, channels);
}

// This is used by sceMp3, in Beats.
// Is the return value the only output?
static int sceAudiocodecGetInfo(u32 ctxPtr, int codec) {
	if (codec < 0x1000 || codec >= 0x1006) {
		return hleLogError(Log::ME, SCE_KERNEL_ERROR_BAD_ARGUMENT, "invalid codec");
	}

	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);  // On game-owned heap, no need to allocate.

	// Write some expected values.
	switch (codec) {
	case PSP_CODEC_MP3:
		// When this is called, the caller has written:
		// * inptr
		// * outptr
		// * fmt.mp3.maxFrameBytes = 0x5A1
		// Our response is written to a bunch of fields, but I really don't know much
		// about what the values are - this is handled internally in the ME.
		ctx->fmt.mp3.unk3c = 3;
		ctx->fmt.mp3.bitrateIndex = 9;
		ctx->fmt.mp3.sampleRateIndex = 0;
		ctx->fmt.mp3.unk60 = 1;
		ctx->fmt.mp3.channelConfig = 1;
		break;
	}

	return hleLogInfo(Log::ME, 0, "codec=%s", GetCodecName(codec));
}

static int sceAudiocodecCheckNeedMem(u32 ctxPtr, int codec) {
	if (codec < 0x1000 || codec >= 0x1006) {
		return hleLogError(Log::ME, SCE_KERNEL_ERROR_BAD_ARGUMENT, "invalid codec");
	}

	if (!Memory::IsValidRange(ctxPtr, sizeof(SceAudiocodecCodec))) {
		return hleLogError(Log::ME, 0, "Bad address");
	}

	// Check for expected values.
	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);  // On game-owned heap, no need to allocate.

	switch (codec) {
	case 0x1000:
		ctx->neededMem = 0x7bc0;
		// avcodec.prx does no format check here at all - it just forwards to the ME - and the
		// caller isn't obliged to have filled these in yet, so this stays a note. libatrac3plus
		// writes 28 5c (the worst case it sizes EDRAM against); mpeg.prx writes the real frame's
		// own header bytes, so anything with bit 3 of the first byte is ordinary.
		if (ctx->fmt.at3.formatByte1 != 0x28 && ctx->fmt.at3.formatByte1 != 0x24) {
			DEBUG_LOG(Log::ME, "sceAudiocodecCheckNeedMem: unfamiliar Atrac3+ format bytes %02x %02x",
				ctx->fmt.at3.formatByte1, ctx->fmt.at3.formatByte2);
		}
		break;
	case 0x1001:
		ctx->neededMem = 0x3de0;
		break;
	case 0x1002:
		ctx->neededMem = 0x3b68;
		break;
	case 0x1003:
		// Kosmodrones uses sceAudiocodec directly (no intermediate library).
		INFO_LOG(Log::ME, "CheckNeedMem for codec %04x: format %02x %02x", codec, ctx->fmt.at3.formatByte1, ctx->fmt.at3.formatByte2);
		break;
	}

	ctx->err = 0;
	ctx->magic = 0x5100601;

	return hleLogWarning(Log::ME, 0, "%s", GetCodecName(codec));
}

static int sceAudiocodecGetEDRAM(u32 ctxPtr, int codec) {
	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);  // On game-owned heap, no need to allocate.
	// TODO: Set this a bit more dynamically. No idea what the allocation algorithm is...
	switch (codec) {
	case PSP_CODEC_MP3:
		ctx->allocMem = 0x001B3124;
		break;
	case PSP_CODEC_AT3:
	default:
		ctx->allocMem = 0x0018ea90;
		break;
	}
	ctx->edramAddr = (ctx->allocMem + 0x3f) & ~0x3f;  // round up to 64 bytes.
	return hleLogInfo(Log::ME, 0, "edram address set to %08x", ctx->edramAddr);
}

static int sceAudiocodecReleaseEDRAM(u32 ctxPtr, int id) {
	if (removeDecoder(ctxPtr)){
		return hleLogInfo(Log::ME, 0);
	}
	return hleLogWarning(Log::ME, 0, "failed to remove decoder");
}

static int sceAudiocodecGetOutputBytes(u32 ctxPtr, int codec, u32 outBytesAddr) {
	if (!Memory::IsValid4AlignedAddress(outBytesAddr)) {
		// Not tested
		return hleLogError(Log::ME, SCE_MP3_ERROR_BAD_ADDR);
	}

	int bytes = 0;
	switch (codec) {
	case PSP_CODEC_AT3PLUS: bytes = 0x2000; break;
	case PSP_CODEC_AT3: bytes = 0x1000; break;  // Atrac3
	case PSP_CODEC_MP3: bytes = 0x1200; break;
	default:
		return hleLogWarning(Log::ME, 0, "Block size query not implemented for codec %04x", codec);
	}

	Memory::WriteUnchecked_U32(bytes, outBytesAddr);
	return hleLogInfo(Log::ME, 0);
}

struct At3HeaderMap {
	u16 bytes;
	u16 channels;
	u8 jointStereo;

	bool Matches(int bytesPerFrame, int encodedChannels) const {
		return this->bytes == bytesPerFrame && this->channels == encodedChannels;
	}
};

// These should represent all possible supported bitrates (66, 104, and 132 for stereo.)
static const At3HeaderMap at3HeaderMap[] = {
	{ 0x00C0, 1, 0 }, // 132/2 (66) kbps mono
	{ 0x0098, 1, 0 }, // 105/2 (52.5) kbps mono
	{ 0x0180, 2, 0 }, // 132 kbps stereo
	{ 0x0130, 2, 0 }, // 105 kbps stereo
	// At this size, stereo can only use joint stereo.
	{ 0x00C0, 2, 1 }, // 66 kbps stereo
};

static int Atrac3BytesPerFrameFromContext(const SceAudiocodecCodec *ctx, int *channels) {
	// AtracCtx2 puts the joint-stereo flag here for Atrac3, mirroring libatrac3plus.
	const bool jointStereo = (ctx->fmt.at3.formatByte1 & 1) != 0;
	if (jointStereo) {
		for (size_t i = 0; i < ARRAY_SIZE(at3HeaderMap); ++i) {
			if (at3HeaderMap[i].jointStereo) {
				*channels = at3HeaderMap[i].channels;
				return at3HeaderMap[i].bytes;
			}
		}
	}
	// 132kbps stereo - by far the most common, and what we assumed unconditionally before.
	*channels = 2;
	return 0x180;
}

bool IsAtrac3StreamJointStereo(int codecType, int bytesPerFrame, int channels) {
	if (codecType != PSP_CODEC_AT3) {
		// Well, might actually be, but it's not used in codec setup.
		return false;
	}

	for (size_t i = 0; i < ARRAY_SIZE(at3HeaderMap); ++i) {
		if (at3HeaderMap[i].Matches(bytesPerFrame, channels)) {
			return at3HeaderMap[i].jointStereo;
		}
	}

	// Not found? Should we log?
	return false;
}


const HLEFunction sceAudiocodec[] = {
	{0X70A703F8, &WrapI_UI<sceAudiocodecDecode>,         "sceAudiocodecDecode",       'i', "xx"},
	{0X5B37EB1D, &WrapI_UI<sceAudiocodecInit>,           "sceAudiocodecInit",         'i', "xx"},
	{0X8ACA11D5, &WrapI_UI<sceAudiocodecGetInfo>,        "sceAudiocodecGetInfo",      'i', "xx"},
	{0X3A20A200, &WrapI_UI<sceAudiocodecGetEDRAM>,       "sceAudiocodecGetEDRAM",     'i', "xx"},
	{0X29681260, &WrapI_UI<sceAudiocodecReleaseEDRAM>,   "sceAudiocodecReleaseEDRAM", 'i', "xx"},
	{0X9D3F790C, &WrapI_UI<sceAudiocodecCheckNeedMem>,   "sceAudiocodecCheckNeedMem", 'i', "xx"},
	{0X59176A0F, &WrapI_UIU<sceAudiocodecGetOutputBytes>, "sceAudiocodecGetOutputBytes", 'i', "xxp" },  // params are context, codec, outptr
	{0X3DD7EE1A, &WrapI_UI<sceAudiocodecInitMono>,       "sceAudiocodecInitMono",     'i', "xx"},  // Used by sceAtrac for MOut* functions.
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
