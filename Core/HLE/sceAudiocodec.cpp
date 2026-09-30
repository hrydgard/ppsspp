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
#include "Common/Serialize/SerializeMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceAudiocodec.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/scePower.h"
#include "Core/HLE/sceVideocodec.h"
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
// How many more successfully decoded frames will produce no output, per context (see the notes on
// Atrac3+ below).
static std::map<u32, int> g_primingFrames;

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
// 48kbps     0x118           0x24       0x22     1
// 64kbps     0x178           0x28       0x2e     2
// 96kbps?    0x230           0x28       0x45     2
// 128kbps    0x2E8           0x28       0x5c     2
//
// The frame size is ((byte 1 & 3) << 8 | byte 2) * 8 + 8, and the channel count is bits 2-4 of
// byte 1 (AtracTrack checks the same bits). The 0x0FD0-sync header on PSMF frames carries the same
// two bytes. On hardware (pspautotests audio/audiocodec), a header claiming byte 1 = 0x29 made the
// decoder read 0x800 bytes more, so the two low bits of byte 1 really are size bits; a context of
// 28 5c on a 28 2e stream decodes fine and reports 744 bytes read, since the decoder only reads
// what the frame needs; 28 22 (too small) fails with err 0x214; and byte 1 = 0x20 (no channels)
// fails Init with err 0x202.
//
// at3Related (0x30) says whether frames carry that 8-byte header. mpeg.prx sets it and passes
// headered frames, libatrac3plus.prx clears it and strips the header. With it set, a frame without
// the 0F D0 sync fails with err 0x211, and one whose header disagrees with the context's channel
// count with err 0x213; with it clear, a header is taken for audio data, which fails to decode.
//
// Bitstream errors are 0x20a or 0x208 (junk and zeros give 0x20a; a real frame read from 4 bytes
// in can parse further and give 0x208). Neither is sticky: the next good frame decodes normally.
// Every failure returns SCE_AVCODEC_ERROR_INVALID_DATA with 0 bytes read and written.
//
// The first frame decoded successfully produces no output (dstBytesWritten 0); from then on
// frame n gives the samples ffmpeg's decoder gives for frame n, so it's dropped, not delayed.
// AAC drops two frames the same way. Atrac3 and MP3 drop none.

// Atrac3 (0x1001)
//
// The Media Engine only ever sees the first 0x68 bytes of this structure - me_wrapper.prx does
// sceKernelDcacheWritebackInvalidateRange(ctx, 0x68) before handing it over, and avcodec.prx
// validates the same range.
//
// The parameter at 0x28 (a word) selects the frame layout. libatrac3plus.prx's SetData (0880645c)
// looks it up in a table keyed by frame size and the stream's joint-stereo flag, and the ME decodes
// by it alone. See at3Params below. Anything above 0x0F fails Init with err 0x186; a bad frame fails
// with err 0x182 and isn't sticky either.

// AAC (0x1003)
// ------------------------------------------------
// Sample rate is at offset 0x28. The input frame size is 0x609 when the byte at 0x2c is nonzero
// and 0x600 when it is zero (avcodec.prx decodeUtility, case 0x1003); the output size comes from
// the byte at 0x2d and is 0x1000 or 0x2000. Setting the byte at 0x2c alone (to 1) fails Init on
// hardware, so something else has to go with it.

// MP3 (0x1002)
// ------------------------------------------------
// The parameters live at 0x38 (MPEG version index: 0 = MPEG2, 1 = MPEG1, 2 = MPEG2.5), 0x44
// (bitrate index) and 0x48 (sample rate index), and index the standard MPEG Layer III tables.
// sceAudiocodecInit presets 0x38 to 9999 meaning "not known yet", and GetInfo fills these in -
// which is why our GetInfo writes plausible values for a 128kbps 44.1kHz stereo stream.
// 0x54 is the channel configuration: libmp3.prx reads it as (value == 3) ? mono : stereo.
// 0x28 is not this frame's size - libmp3.prx writes 0x5A1 there once, the largest an MP3 frame
// can ever be. Output is 0x1200 bytes for MPEG1 (1152 samples) and 0x900 otherwise (576).

// Atrac3+ frame size and channel count from a pair of format bytes (from the context, or from a
// PSMF frame header).
static void At3PlusFormat(u8 formatByte1, u8 formatByte2, int *inputBytes, int *channels) {
	*channels = (formatByte1 >> 2) & 7;
	*inputBytes = (((formatByte1 & 3) << 8) | formatByte2) * 8 + 8;
}

// For a decode (headerBytes non-null), also handles the frame header, and returns the ME error
// code for a frame that doesn't match the context, or 0.
static int CalculateInputBytesAndChannelsAt3Plus(const SceAudiocodecCodec *ctx, int *inputBytes, int *channels, int *headerBytes = nullptr) {
	At3PlusFormat(ctx->fmt.at3.formatByte1, ctx->fmt.at3.formatByte2, inputBytes, channels);
	if (!headerBytes) {
		return 0;
	}
	*headerBytes = 0;
	if (ctx->fmt.at3.at3Related == 0) {
		return 0;
	}

	const u8 *frame = Memory::IsValidRange(ctx->inBuf, 8) ? Memory::GetPointerUnchecked(ctx->inBuf) : nullptr;
	if (!frame || frame[0] != 0x0F || frame[1] != 0xD0) {
		return 0x211;
	}
	int frameChannels;
	At3PlusFormat(frame[2], frame[3], inputBytes, &frameChannels);
	if (frameChannels != *channels) {
		return 0x213;
	}
	*headerBytes = 8;
	return 0;
}

static bool Atrac3LayoutFromContext(const SceAudiocodecCodec *ctx, int *bytesPerFrame, int *channels, bool *jointStereo);

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
		g_primingFrames.erase(ctxPtr);
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
	g_primingFrames.clear();
}

void __AudioCodecInit() {
	// findDecoder keys on a game-chosen context address, so don't leave decoders from a previous
	// game around for the next one to find.
	clearDecoders();
	oldStateLoaded = false;
}

void __AudioCodecShutdown() {
	// We need to kill off any still opened codecs to not leak memory.
	clearDecoders();
}

// Everything below that reaches the ME blocks the caller while it answers. Measured at 222MHz
// (pspautotests audio/audiocodec/timing):
//             CheckNeedMem  GetEDRAM  Init  ReleaseEDRAM  GetInfo
//   Atrac3+        147         56     646        55          82
//   Atrac3          80         56     210        56           3
//   MP3             79         57     517        57          82
//   AAC             57         57     230        55           3
// A GetInfo of 3us didn't reach the ME. Initializing a mono Atrac3+ decoder (as InitMono does for
// libatrac3plus.prx's MOut functions) takes 524us. Failed decodes take 214us for an Atrac3+
// bitstream error, 142us for a bad Atrac3+ frame header, and 169us for an Atrac3 bitstream error.
static int MECall(int result, int us) {
	return hleDelayResult(result, "audiocodec", MEScheduleJob(PowerScaleFromDefaultClock(us)));
}

int AudioCodecInitUs(int codec, bool monoAt3Plus) {
	switch (codec) {
	case PSP_CODEC_AT3PLUS: return monoAt3Plus ? 524 : 646;
	case PSP_CODEC_AT3: return 210;
	case PSP_CODEC_MP3: return 517;
	default: return 230;
	}
}

static int InitUs(int codec, const SceAudiocodecCodec *ctx) {
	return AudioCodecInitUs(codec, codec == PSP_CODEC_AT3PLUS && ((ctx->fmt.at3.formatByte1 >> 2) & 7) == 1);
}

// libmp4.prx puts the sample rate here. On hardware 22050 and 44100 are accepted, 0 and 12345
// aren't; that the rest of the standard AAC rates are accepted is an assumption. 0 if not valid.
static int AacSampleRateFromContext(const SceAudiocodecCodec *ctx) {
	static const int aacRates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
	for (int rate : aacRates) {
		if ((int)ctx->fmt.aac.sampleRate == rate) {
			return rate;
		}
	}
	return 0;
}

// Creates a context's decoder from what the context holds. Init does this, and so does a state
// load, since the decoders themselves aren't saved.
static AudioDecoder *CreateDecoderForContext(u32 ctxPtr, PSPAudioType audioType) {
	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);

	int bytesPerFrame = 0;
	int channels = 2;
	int sampleRate = 44100;

	uint8_t extraData[14]{};

	switch (audioType) {
	case PSP_CODEC_AT3PLUS:
		CalculateInputBytesAndChannelsAt3Plus(ctx, &bytesPerFrame, &channels);
		break;
	case PSP_CODEC_AT3:
	{
		bool jointStereo;
		Atrac3LayoutFromContext(ctx, &bytesPerFrame, &channels, &jointStereo);
		// The only thing that changes are the jointStereo_ values.
		extraData[0] = 1;
		extraData[3] = channels << 3;
		extraData[6] = jointStereo;
		extraData[8] = jointStereo;
		extraData[10] = 1;
		break;
	}
	case PSP_CODEC_AAC:
		if (AacSampleRateFromContext(ctx)) {
			sampleRate = AacSampleRateFromContext(ctx);
		}
		break;
	default:
		break;
	}

	// Create audio decoder for given audio codec and push it into AudioList
	INFO_LOG(Log::ME, "sceAudioDecoder: Creating codec with %04x frame size and %d channels, codec %04x", bytesPerFrame, channels, (int)audioType);
	// We send in extra data with all codec, most ignore it.
	AudioDecoder *decoder = CreateAudioDecoder(audioType, sampleRate, channels, bytesPerFrame, extraData, sizeof(extraData));
	decoder->SetCtxPtr(ctxPtr);
	g_audioDecoderContexts[ctxPtr] = decoder;
	g_at3PlusFrameBytes[ctxPtr] = bytesPerFrame;
	return decoder;
}

// TODO: Actually support mono output.
static int __AudioCodecInitCommon(u32 ctxPtr, int codec, bool mono) {
	const PSPAudioType audioType = (PSPAudioType)codec;
	if (!IsValidCodec(audioType)) {
		return hleLogError(Log::ME, SCE_KERNEL_ERROR_OUT_OF_RANGE, "Invalid codec");
	}

	// Re-initialising a context that still has a decoder is normal, not a report-worthy
	// surprise: mpeg.prx sizes the allocation through a scratch context of its own and only ever
	// releases that one, so the context it actually decodes through still holds a decoder when the
	// next movie starts. Once per video, on every game running the real module.
	if (removeDecoder(ctxPtr)) {
		INFO_LOG(Log::HLE, "sceAudiocodecInit(%08x, %d): replacing existing context", ctxPtr, codec);
	}

	// Initialize the codec memory.
	auto ctx = PSPPointer<SceAudiocodecCodec>::Create(ctxPtr);
	ctx->magic = 0x5100601;
	ctx->err = 0;

	int primingFrames = 0;

	// Special actions for some codecs, and what the hardware rejects.
	switch (audioType) {
	case PSP_CODEC_MP3:
		// Not seeing inited in Kurok (homebrew)
		// _dbg_assert_(ctx->inited == 1);
		ctx->fmt.mp3.version = 9999;
		break;
	case PSP_CODEC_AAC:
		if (!AacSampleRateFromContext(ctx)) {
			return MECall(hleLogError(Log::ME, SCE_AVCODEC_ERROR_UNSUPPORTED, "bad AAC sample rate %d", ctx->fmt.aac.sampleRate), InitUs(codec, ctx));
		}
		primingFrames = 2;
		break;
	case PSP_CODEC_AT3PLUS:
	{
		int bytesPerFrame, channels;
		CalculateInputBytesAndChannelsAt3Plus(ctx, &bytesPerFrame, &channels);
		if (channels == 0) {
			ctx->err = 0x202;
			return MECall(hleLogError(Log::ME, SCE_AVCODEC_ERROR_INVALID_DATA, "bad Atrac3+ format byte %02x", ctx->fmt.at3.formatByte1), InitUs(codec, ctx));
		}
		primingFrames = 1;
		break;
	}
	case PSP_CODEC_AT3:
	{
		int bytesPerFrame, channels;
		bool jointStereo;
		if (!Atrac3LayoutFromContext(ctx, &bytesPerFrame, &channels, &jointStereo)) {
			ctx->err = 0x186;
			return MECall(hleLogError(Log::ME, SCE_AVCODEC_ERROR_INVALID_DATA, "bad Atrac3 parameter %08x", *(const u32_le *)ctx->fmt.raw), InitUs(codec, ctx));
		}
		break;
	}
	default:
		break;
	}

	CreateDecoderForContext(ctxPtr, audioType);
	// Not in CreateDecoderForContext: a state load restores what's left of it instead.
	g_primingFrames[ctxPtr] = primingFrames;
	return MECall(hleLogDebug(Log::ME, 0), InitUs(codec, ctx));
}

// How long the ME takes over one frame, in microseconds at the default 222MHz clock. Fitted to
// sceAudiocodecDecode timed on a PSP (pspautotests audio/audiocodec/timing), which at 222MHz gave:
//   Atrac3+ stereo: 2410us at 376 bytes/frame, 2990 at 744. Mono: 2126 at 744.
//   Atrac3 stereo: 1138 at 0x180, 1063 at 0xC0 joint stereo. Mono: 685 at 0x98.
//   MP3 MPEG1 (1152 samples): 2575 at 418 bytes, 2698 at 1045. MPEG2 (576): 1411 at 104, 1464 at 209.
//   AAC-LC stereo 44.1kHz: 1651 at ~190 bytes, 1990 at ~373, 2042 at ~559.
// The mono Atrac3+ slope is a guess from its one data point. Content matters as well as size: the
// synthetic two-tone AAC in video/mp4 decodes about 15% faster than music at the same bitrate.
static int EstimateDecodeUs(int codec, int channels, int frameBytes, const SceAudiocodecCodec *ctx) {
	switch (codec) {
	case PSP_CODEC_AT3PLUS:
		return channels == 1 ? 1500 + frameBytes * 84 / 100 : 1815 + frameBytes * 158 / 100;
	case PSP_CODEC_AT3:
		return channels == 1 ? 685 : 970 + frameBytes * 44 / 100;
	case PSP_CODEC_MP3:
		return ctx->fmt.mp3.version == 1 ? 2492 + frameBytes / 5 : 1358 + frameBytes / 2;
	case PSP_CODEC_AAC:
		return 1400 + std::min(frameBytes, 400) * 16 / 10;
	default:
		return 0;
	}
}

int AudioCodecDecodeUs(int codec, int channels, int frameBytes) {
	_dbg_assert_(codec == PSP_CODEC_AT3PLUS || codec == PSP_CODEC_AT3);
	return EstimateDecodeUs(codec, channels, frameBytes, nullptr);
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
	{
		const int frameError = CalculateInputBytesAndChannelsAt3Plus(ctx, &bytesPerFrame, &channels, &headerBytes);
		if (frameError) {
			ctx->err = frameError;
			ctx->srcBytesRead = 0;
			ctx->dstBytesWritten = 0;
			return MECall(hleLogWarning(Log::ME, SCE_AVCODEC_ERROR_INVALID_DATA, "Atrac3+ frame doesn't match the context: err %03x", frameError), 142);
		}
		break;
	}
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
		// avcodec.prx sizes the AAC input frame from the byte at 0x2c: 0x609 when it is nonzero,
		// 0x600 when it is zero (decodeUtility, case 0x1003). srcBytesRead, which this used to read,
		// is an output field and is 0 on the first call, so the decoder got nothing to read and
		// audio never started (seen in Meururu no Atelier Plus running the real mpeg.prx).
		bytesPerFrame = ctx->fmt.aac.unk2c ? 0x609 : 0x600;
		sampleRate = ctx->fmt.aac.sampleRate;
		break;
	case PSP_CODEC_AT3:
	{
		bool jointStereo;
		Atrac3LayoutFromContext(ctx, &bytesPerFrame, &channels, &jointStereo);
		break;
	}
	}

	// find a decoder in audioList
	auto decoder = findDecoder(ctxPtr);

	if (!decoder && oldStateLoaded) {
		// We must have loaded an old state that did not have sceAudiocodec information.
		// Fake it by creating the desired context.
		decoder = CreateDecoderForContext(ctxPtr, audioType);
	}

	if (decoder && codec == PSP_CODEC_AT3PLUS && bytesPerFrame > 0) {
		auto it = g_at3PlusFrameBytes.find(ctxPtr);
		if (it == g_at3PlusFrameBytes.end() || it->second != bytesPerFrame) {
			// Only reachable when the context didn't carry a frame size at init - mpeg.prx.
			INFO_LOG(Log::ME, "sceAudiocodecDecode: Atrac3+ frame is %04x bytes, rebuilding decoder", bytesPerFrame);
			// The hardware has one decoder throughout, so this doesn't restart the priming.
			const int primingFrames = g_primingFrames[ctxPtr];
			removeDecoder(ctxPtr);
			decoder = CreateAudioDecoder(audioType, 44100, channels, bytesPerFrame);
			decoder->SetCtxPtr(ctxPtr);
			g_audioDecoderContexts[ctxPtr] = decoder;
			g_at3PlusFrameBytes[ctxPtr] = bytesPerFrame;
			g_primingFrames[ctxPtr] = primingFrames;
		}
	}

	int decodeUs = 0;
	if (decoder) {
		// Use SimpleAudioDec to decode audio
		// Decode audio
		int inDataConsumed = 0;
		int outSamples = 0;

		DEBUG_LOG(Log::ME, "decoder. in: %08x out: %08x format: %02x %02x", ctx->inBuf, ctx->outBuf, ctx->fmt.at3.formatByte1, ctx->fmt.at3.formatByte2);

		int16_t *outBuf = (int16_t *)Memory::GetPointerWriteOrException(ctx->outBuf);

		// For Atrac3+ in a PSMF the length came out of the frame's own header, so it's only as
		// trustworthy as the stream - check the whole span before handing it to the decoder
		// rather than just the first byte, which is all GetPointerOrException would look at.
		// IsValidRange first: the range accessors raise a memory exception rather than returning
		// null, and a stream that lies about its length shouldn't fault the game.
		const u32 inAddr = ctx->inBuf + headerBytes;
		const u8 *inBuf = (bytesPerFrame > 0 && Memory::IsValidRange(inAddr, bytesPerFrame))
			? Memory::GetPointerUnchecked(inAddr) : nullptr;
		if (!inBuf) {
			ctx->err = 0x20b;
			return hleLogError(Log::ME, 0, "%d bytes at %08x isn't readable", bytesPerFrame, inAddr);
		}

		bool result = decoder->Decode(inBuf, bytesPerFrame, &inDataConsumed, 2, outBuf, &outSamples);
		if (!result && (codec == PSP_CODEC_AT3PLUS || codec == PSP_CODEC_AT3)) {
			// What the hardware reports for a frame that doesn't decode (0x208 is also possible for
			// Atrac3+, depending on how far into the frame the problem is).
			ctx->err = codec == PSP_CODEC_AT3PLUS ? 0x20a : 0x182;
			ctx->srcBytesRead = 0;
			ctx->dstBytesWritten = 0;
			return MECall(hleLogWarning(Log::ME, SCE_AVCODEC_ERROR_INVALID_DATA, "%s frame failed to decode", GetCodecName(codec)), codec == PSP_CODEC_AT3PLUS ? 214 : 169);
		}
		if (!result) {
			ctx->err = 0x20b;
			ERROR_LOG(Log::ME, "AudioCodec decode failed. Setting error to %08x", ctx->err);
		} else {
			ctx->err = 0;
			auto priming = g_primingFrames.find(ctxPtr);
			if (priming != g_primingFrames.end() && priming->second > 0) {
				priming->second--;
				outSamples = 0;
			}
		}

		ctx->srcBytesRead = inDataConsumed + headerBytes;
		// In bytes, not samples. sceAudiocodecGetOutputBytes describes the same quantity in bytes
		// (0x1200 for MPEG1 MP3), and libmp3.prx takes this as the length of the PCM to hand on -
		// reporting the sample count instead gave it a quarter of every frame, which played back
		// fast and metallic. The decoder always writes stereo 16-bit, whatever the source is.
		ctx->dstBytesWritten = outSamples * 2 * (int)sizeof(int16_t);

		decodeUs = EstimateDecodeUs(codec, channels, inDataConsumed, ctx);
	}
	if (decodeUs > 0) {
		decodeUs = MEScheduleJob(PowerScaleFromDefaultClock(decodeUs));
		return hleDelayResult(hleLogDebug(Log::ME, 0, "codec %s sampleRate: %d bytesPerFrame: %d channels: %d", GetCodecName(codec), sampleRate, bytesPerFrame, channels), "audiocodec decode", decodeUs);
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
	{
		// The caller has written inBuf, outBuf and maxFrameBytes, and left version at the 9999
		// sceAudiocodecInit puts there to mean "not known yet". Filling that in is the point of
		// this call - libmp3.prx reads it straight back out, and leaving it at 9999 is what made
		// Meruru no Atelier Plus go silent: it got this far and then stopped without ever asking
		// for a decode.
		//
		// The hardware reads these off the frame, so read them off the frame. Apart from the
		// version index, which has its own numbering, they are the raw MPEG header fields.
		ctx->fmt.mp3.unk3c = 3;
		ctx->fmt.mp3.unk60 = 1;

		// Header version bits are 0 = MPEG2.5, 2 = MPEG2, 3 = MPEG1 (1 is reserved); the field
		// wants 0 = MPEG2, 1 = MPEG1, 2 = MPEG2.5.
		static const int versionFromHeader[4] = { 2, -1, 0, 1 };
		const u8 *header = Memory::IsValidRange(ctx->inBuf, 4) ? Memory::GetPointerUnchecked(ctx->inBuf) : nullptr;
		const bool haveFrame = header && header[0] == 0xFF && (header[1] & 0xE0) == 0xE0 &&
			versionFromHeader[(header[1] >> 3) & 3] >= 0;
		if (haveFrame) {
			ctx->fmt.mp3.version = versionFromHeader[(header[1] >> 3) & 3];
			ctx->fmt.mp3.bitrateIndex = (header[2] >> 4) & 0x0F;
			ctx->fmt.mp3.sampleRateIndex = (header[2] >> 2) & 0x03;
			ctx->fmt.mp3.channelConfig = (header[3] >> 6) & 0x03;
			INFO_LOG(Log::ME, "GetInfo MP3: sdk=%08x version=%d bitrateIdx=%d sampleRateIdx=%d channelConfig=%d (hdr %02x %02x %02x %02x)",
				sceKernelGetCompiledSdkVersion(), (int)ctx->fmt.mp3.version, (int)ctx->fmt.mp3.bitrateIndex, (int)ctx->fmt.mp3.sampleRateIndex,
				(int)ctx->fmt.mp3.channelConfig, header[0], header[1], header[2], header[3]);
		} else {
			// Nothing readable to look at. Claim 128kbps 44.1kHz stereo, as this used to
			// unconditionally - but do set the version, since 9999 stops the caller dead.
			// Worth hearing about: everything the caller does with the stream follows from these,
			// so if a game ever lands here its audio will be wrong in a way that starts right here.
			WARN_LOG(Log::ME, "sceAudiocodecGetInfo: no MP3 frame at inBuf %08x, guessing 128kbps 44.1kHz stereo",
				ctx->inBuf);
			ctx->fmt.mp3.version = 1;
			ctx->fmt.mp3.bitrateIndex = 9;
			ctx->fmt.mp3.sampleRateIndex = 0;
			ctx->fmt.mp3.channelConfig = 1;
		}
		break;
	}
	}

	if (codec == PSP_CODEC_MP3 || codec == PSP_CODEC_AT3PLUS) {
		return MECall(hleLogInfo(Log::ME, 0, "codec=%s", GetCodecName(codec)), 82);
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

	// The sizes are fixed per codec: on hardware they don't depend on the Atrac3+ format bytes,
	// inited, or the AAC sample rate.
	switch (codec) {
	case 0x1000:
		// The ME does look at the format bytes, though: both zero fails.
		if (ctx->fmt.at3.formatByte1 == 0 && ctx->fmt.at3.formatByte2 == 0) {
			ctx->err = 0x20f;
			return MECall(hleLogError(Log::ME, SCE_AVCODEC_ERROR_INVALID_DATA, "no Atrac3+ format"), 147);
		}
		ctx->neededMem = 0x7bc0;
		break;
	case 0x1001:
		ctx->neededMem = 0x3de0;
		break;
	case 0x1002:
		ctx->neededMem = 0x3b68;
		break;
	case 0x1003:
		// Kosmodrones uses sceAudiocodec directly (no intermediate library).
		ctx->neededMem = 0x658c;
		break;
	case 0x1004:
		return hleLogError(Log::ME, SCE_AVCODEC_ERROR_UNSUPPORTED, "codec 1004");
	case 0x1005:
		// What the hardware does, without asking the ME. Looks like an error code in the size field.
		ctx->neededMem = 0x80000003;
		ctx->err = 0;
		return hleLogWarning(Log::ME, 0, "codec 1005");
	}

	ctx->err = 0;
	ctx->magic = 0x5100601;

	static const int needMemUs[4] = { 147, 80, 79, 57 };
	return MECall(hleLogInfo(Log::ME, 0, "%s: %x", GetCodecName(codec), ctx->neededMem), needMemUs[codec - 0x1000]);
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
	return MECall(hleLogInfo(Log::ME, 0, "edram address set to %08x", ctx->edramAddr), 56);
}

// One parameter, not two: the real module (audiocodec_260.prx, 080007c0) reads only a0 and sets
// up a1-a3 itself, so a second argument here just logs whatever was left in the register.
//
// Releasing the EDRAM without ever having made a decoder is normal - mpeg.prx calls
// CheckNeedMem/GetEDRAM to size the allocation and only creates a decoder if the stream turns out
// to need one, so there is often nothing here to drop.
static int sceAudiocodecReleaseEDRAM(u32 ctxPtr) {
	if (Memory::IsValidRange(ctxPtr, sizeof(SceAudiocodecCodec))) {
		PSPPointer<SceAudiocodecCodec>::Create(ctxPtr)->edramAddr = 0;
	}
	if (removeDecoder(ctxPtr)) {
		return MECall(hleLogInfo(Log::ME, 0), 56);
	}
	return MECall(hleLogDebug(Log::ME, 0, "no decoder for this context"), 56);
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

// The Atrac3 parameter at 0x28, as libatrac3plus.prx's table at 08807f08 maps it from frame size
// and joint stereo. The hardware decoded 0x04, 0x0B and 0x0F streams as listed.
struct At3Param {
	u8 param;
	u16 bytes;
	u8 channels;
	u8 jointStereo;
};

static const At3Param at3Params[] = {
	{ 0x04, 0x0180, 2, 0 },
	{ 0x06, 0x0130, 2, 0 },
	{ 0x0B, 0x00C0, 2, 1 },
	{ 0x0E, 0x00C0, 1, 0 },
	{ 0x0F, 0x0098, 1, 0 },
};

// Returns false for a parameter the hardware rejects.
static bool Atrac3LayoutFromContext(const SceAudiocodecCodec *ctx, int *bytesPerFrame, int *channels, bool *jointStereo) {
	const u32 param = *(const u32_le *)ctx->fmt.raw;
	for (const At3Param &p : at3Params) {
		if (p.param == param) {
			*bytesPerFrame = p.bytes;
			*channels = p.channels;
			*jointStereo = p.jointStereo != 0;
			return true;
		}
	}
	// Not in libatrac3plus.prx's table. Fall back to 132kbps stereo, the most common.
	*bytesPerFrame = 0x180;
	*channels = 2;
	*jointStereo = false;
	if (param > 0x0F) {
		return false;
	}
	WARN_LOG(Log::ME, "Unknown Atrac3 parameter %02x", param);
	return true;
}

// libatrac3plus.prx's SetData picks the parameter by frame size and the header's joint
// stereo flag, scanning from the last entry, and the channel count plays no part. LocoRoco 2 writes
// 2 channels into every track header it builds, and its 0xC0 MuiMui house track is mono (#8647).
// No match fails SetData with 0x80630008.
bool Atrac3DecoderChannels(int bytesPerFrame, bool jointStereo, int *channels) {
	for (int i = ARRAY_SIZE(at3Params) - 1; i >= 0; i--) {
		if (at3Params[i].bytes == bytesPerFrame && (at3Params[i].jointStereo != 0) == jointStereo) {
			*channels = at3Params[i].channels;
			return true;
		}
	}
	return false;
}

bool IsAtrac3StreamJointStereo(int codecType, int bytesPerFrame, int channels) {
	if (codecType != PSP_CODEC_AT3) {
		// Well, might actually be, but it's not used in codec setup.
		return false;
	}

	for (const At3Param &p : at3Params) {
		if (p.bytes == bytesPerFrame && p.channels == channels) {
			return p.jointStereo != 0;
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
	{0X29681260, &WrapI_U<sceAudiocodecReleaseEDRAM>,    "sceAudiocodecReleaseEDRAM", 'i', "x"},
	{0X9D3F790C, &WrapI_UI<sceAudiocodecCheckNeedMem>,   "sceAudiocodecCheckNeedMem", 'i', "xx"},
	{0X59176A0F, &WrapI_UIU<sceAudiocodecGetOutputBytes>, "sceAudiocodecGetOutputBytes", 'i', "xxp" },  // params are context, codec, outptr
	{0X3DD7EE1A, &WrapI_UI<sceAudiocodecInitMono>,       "sceAudiocodecInitMono",     'i', "xx"},  // Used by sceAtrac for MOut* functions.
};

void Register_sceAudiocodec() {
	RegisterHLEModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}

void __sceAudiocodecDoState(PointerWrap &p){
	auto s = p.Section("AudioList", 0, 3);
	if (!s) {
		if (p.mode == PointerWrap::MODE_READ) {
			clearDecoders();
			oldStateLoaded = true;
		}
		return;
	}
	if (p.mode == PointerWrap::MODE_READ) {
		oldStateLoaded = false;
	}

	int count = (int)g_audioDecoderContexts.size();
	Do(p, count);

	if (p.mode == PointerWrap::MODE_READ) {
		clearDecoders();
	}

	if (count > 0) {
		if (p.mode == PointerWrap::MODE_READ) {
			// loadstate if audioList is nonempty
			// v1 read a fixed two, whatever the count.
			auto codec_ = new int[std::max(count, 2)];
			auto ctxPtr_ = new u32[std::max(count, 2)];
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
				if (!Memory::IsValidRange(ctxPtr_[i], sizeof(SceAudiocodecCodec))) {
					ERROR_LOG(Log::ME, "Savestate has an audiocodec context at an invalid address %08x", ctxPtr_[i]);
					continue;
				}
				CreateDecoderForContext(ctxPtr_[i], (PSPAudioType)codec_[i]);
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

	if (s >= 3) {
		Do(p, g_primingFrames);
	} else if (p.mode == PointerWrap::MODE_READ) {
		g_primingFrames.clear();
	}
}
