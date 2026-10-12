// Copyright (c) 2013- PPSSPP Project.

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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"
#include "Core/HW/Atrac3Standalone.h"

#include <formats/raac.h>
#include <formats/rmp3.h>

// AAC (sceAac, sceAudiocodec) on raac. The PSP's AAC is AAC-LC, carried
// in ADTS frames; a buffer without ADTS framing starts with a raw access
// unit of the rate and channels the decoder was made for.
class AacAudio : public AudioDecoder {
public:
	AacAudio(int sampleRateHz, int channels) : sampleRate_(sampleRateHz), channels_(channels) {}
	~AacAudio() override {
		if (dec_)
			raac_close(dec_);
	}

	bool Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) override;
	bool IsOK() const override { return true; }
	void SetChannels(int channels) override {
		if (!dec_)
			channels_ = channels;
	}
	void FlushBuffers() override {
		if (dec_)
			raac_reset(dec_);
	}
	PSPAudioType GetAudioType() const override { return PSP_CODEC_AAC; }

private:
	// The AudioSpecificConfig the decoder was opened with.
	bool Open(uint16_t asc);

	raac_t *dec_ = nullptr;
	uint16_t asc_ = 0;
	int sampleRate_;
	int channels_;
	int16_t pcm_[RAAC_MAX_FRAME * 8];
};

static int AacRateIndex(int hz) {
	static const int rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
	for (int i = 0; i < 13; i++) {
		if (rates[i] == hz)
			return i;
	}
	return 4;  // 44100
}

bool AacAudio::Open(uint16_t asc) {
	if (dec_ && asc == asc_)
		return true;
	if (dec_)
		raac_close(dec_);
	// With a sync extension saying SBR runs at the core rate, so the decoder stays at the core
	// rate: the Media Engine decodes AAC-LC, 1024 samples a frame, and the buffers are that size.
	// Left unsaid, a 16 to 24 kHz stream would come out upsampled, twice the samples.
	const uint64_t bits = (((uint64_t)asc << 21) | (0x2B7u << 10) | (5u << 5) | (1u << 4) | ((asc >> 7) & 15u)) << 3;
	const uint8_t bytes[5] = { (uint8_t)(bits >> 32), (uint8_t)(bits >> 24), (uint8_t)(bits >> 16), (uint8_t)(bits >> 8), (uint8_t)bits };
	dec_ = raac_open(bytes, sizeof(bytes));
	if (dec_ && raac_frame_len(dec_) != 1024) {
		ERROR_LOG(Log::ME, "AAC: configuration %04x gives %u samples a frame", asc, raac_frame_len(dec_));
		raac_close(dec_);
		dec_ = nullptr;
	}
	asc_ = asc;
	if (!dec_)
		ERROR_LOG(Log::ME, "AAC: configuration %04x not decodable", asc);
	return dec_ != nullptr;
}

bool AacAudio::Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) {
	if (outSamples)
		*outSamples = 0;
	if (inbytesConsumed)
		*inbytesConsumed = 0;
	if (!inbuf || inbytes <= 0)
		return false;

	const uint8_t *payload = inbuf;
	int payloadSize = inbytes;
	int frameBytes = inbytes;
	uint16_t asc;
	if (inbytes >= 7 && inbuf[0] == 0xFF && (inbuf[1] & 0xF6) == 0xF0) {
		// ADTS: the frame says its own configuration and length.
		const int objectType = ((inbuf[2] >> 6) & 3) + 1;
		const int rateIndex = (inbuf[2] >> 2) & 15;
		const int channelConfig = ((inbuf[2] & 1) << 2) | (inbuf[3] >> 6);
		const int headerBytes = (inbuf[1] & 1) ? 7 : 9;
		frameBytes = ((inbuf[3] & 3) << 11) | (inbuf[4] << 3) | (inbuf[5] >> 5);
		if (frameBytes < headerBytes || frameBytes > inbytes) {
			// Not all here yet, or not a frame.
			return false;
		}
		asc = (uint16_t)((objectType << 11) | (rateIndex << 7) | (channelConfig << 3));
		payload = inbuf + headerBytes;
		payloadSize = frameBytes - headerBytes;
	} else {
		asc = (uint16_t)((2 << 11) | (AacRateIndex(sampleRate_) << 7) | ((channels_ == 1 ? 1 : 2) << 3));
	}
	if (!Open(asc)) {
		if (inbytesConsumed)
			*inbytesConsumed = frameBytes;
		return false;
	}

	const int n = raac_decode_s16(dec_, payload, (size_t)payloadSize, pcm_);
	// A raw unit takes what it parses to; one that does not decode is passed over
	// rather than offered again.
	if (payload == inbuf && n > 0)
		frameBytes = (int)raac_last_size(dec_);
	if (inbytesConsumed)
		*inbytesConsumed = frameBytes;
	if (n <= 0)
		return false;

	// Past stereo the channels come in ISO order, the centre first: take the front pair.
	const int ch = (int)raac_channels(dec_);
	const int lc = ch > 2 ? 1 : 0, rc = ch > 2 ? 2 : (ch > 1 ? 1 : 0);
	if (outbuf) {
		for (int i = 0; i < n; i++) {
			const int16_t l = pcm_[i * ch + lc];
			const int16_t r = pcm_[i * ch + rc];
			if (outputChannels == 1) {
				outbuf[i] = l;
			} else {
				outbuf[i * 2] = l;
				outbuf[i * 2 + 1] = r;
			}
		}
	}
	if (outSamples)
		*outSamples = n;
	return true;
}

// MP3 (sceMp3, sceAudiocodec) on rmp3, a frame at a time: the first
// frame in the buffer, whatever is ahead of its sync skipped.
class Mp3Audio : public AudioDecoder {
public:
	Mp3Audio() {
		dec_ = (rmp3dec *)malloc(sizeof(rmp3dec));
		if (dec_)
			rmp3dec_init(dec_);
	}
	~Mp3Audio() override {
		free(dec_);
	}

	bool Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) override;
	bool IsOK() const override { return dec_ != nullptr; }
	void SetChannels(int channels) override {}
	void FlushBuffers() override {
		if (dec_)
			rmp3dec_init(dec_);
	}
	PSPAudioType GetAudioType() const override { return PSP_CODEC_MP3; }

private:
	rmp3dec *dec_ = nullptr;
	int16_t pcm_[RMP3_MAX_SAMPLES_PER_FRAME];
};

bool Mp3Audio::Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) {
	if (outSamples)
		*outSamples = 0;
	if (inbytesConsumed)
		*inbytesConsumed = 0;
	if (!dec_ || !inbuf || inbytes <= 0)
		return false;

	rmp3dec_frame_info info;
	int n = rmp3dec_decode_s16(dec_, inbuf, inbytes, pcm_, &info);
	if (info.frame_bytes <= 0)
		return false;
	if (inbytesConsumed)
		*inbytesConsumed = info.frame_bytes;
	if (n <= 0) {
		if (info.hz <= 0) {
			// Junk skipped: taken, nothing out.
			return true;
		}
		// A frame whose bit reservoir starts before the decoder joined the stream: a frame of
		// silence, so the samples counted and the time played stay what the stream says.
		n = info.layer == 1 ? 384 : (info.layer == 3 && info.hz < 32000 ? 576 : 1152);
		memset(pcm_, 0, (size_t)n * (info.channels > 1 ? 2 : 1) * sizeof(int16_t));
	}

	const int ch = info.channels > 1 ? 2 : 1;
	if (outbuf) {
		for (int i = 0; i < n; i++) {
			const int16_t l = pcm_[i * ch];
			const int16_t r = ch > 1 ? pcm_[i * ch + 1] : l;
			if (outputChannels == 1) {
				outbuf[i] = l;
			} else {
				outbuf[i * 2] = l;
				outbuf[i * 2 + 1] = r;
			}
		}
	}
	if (outSamples)
		*outSamples = n;
	return true;
}

AudioDecoder *CreateAudioDecoder(PSPAudioType audioType, int sampleRateHz, int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize) {
	switch (audioType) {
	case PSP_CODEC_AT3:
		return CreateAtrac3Audio(channels, blockAlign, extraData, extraDataSize);
	case PSP_CODEC_AT3PLUS:
		return CreateAtrac3PlusAudio(channels, blockAlign);
	case PSP_CODEC_MP3:
		return new Mp3Audio();
	case PSP_CODEC_AAC:
	default:
		return new AacAudio(sampleRateHz, channels);
	}
}

void AudioClose(AudioDecoder **ctx) {
	delete *ctx;
	*ctx = nullptr;
}

static const char *const codecNames[4] = {
	"AT3+", "AT3", "MP3", "AAC",
};

const char *GetCodecName(int codec) {
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return codecNames[codec - PSP_CODEC_AT3PLUS];
	} else {
		return "(unk)";
	}
};

bool IsValidCodec(PSPAudioType codec){
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return true;
	}
	return false;
}


// sceAu module starts from here

AuCtx::AuCtx() {
}

AuCtx::~AuCtx() {
	if (decoder) {
		AudioClose(&decoder);
		decoder = nullptr;
	}
}

size_t AuCtx::FindNextMp3Sync() {
	// sourcebuff.size() - 2 underflows to a huge size_t when size() is 0 or 1,
	// turning this into an out-of-bounds scan - guard against that explicitly.
	if (sourcebuff.size() < 3) {
		return 0;
	}
	for (size_t i = 0; i < sourcebuff.size() - 2; ++i) {
		if ((sourcebuff[i] & 0xFF) == 0xFF && (sourcebuff[i + 1] & 0xC0) == 0xC0) {
			return i;
		}
	}
	return 0;
}

// return output pcm size, <0 error
u32 AuCtx::AuDecode(u32 pcmAddr) {
	u32 outptr = PCMBuf + nextOutputHalf * PCMBufSize / 2;
	auto outbuf = Memory::GetPointerWriteRangeOrException(outptr, PCMBufSize / 2);
	int outpcmbufsize = 0;

	if (pcmAddr)
		Memory::WriteOrException_U32(outptr, pcmAddr);

	// The stream is over once the decoder has consumed up to endPos, whatever is still sitting in
	// the buffer. A game can hand us more than the file actually had - audio/mp3/stream notifies
	// the full size it asked for even when the read came up short - and the hardware won't decode
	// that tail, it just reports the end. A stream that still has loops left was already rewound
	// by the block below, so this only stops us for good.
	bool end = (int64_t)readPos - AuBufAvailable >= (int64_t)endPos;

	// Decode a single frame in sourcebuff and output into PCMBuf.
	if (!end && !sourcebuff.empty()) {
		// The decoder takes whole frames, so find the sync here.
		int nextSync = 0;
		if (decoder->GetAudioType() == PSP_CODEC_MP3) {
			nextSync = (int)FindNextMp3Sync();
		}
		int inbytesConsumed = 0;
		int outSamples = 0;
		decoder->Decode(&sourcebuff[nextSync], (int)sourcebuff.size() - nextSync, &inbytesConsumed, 2, (int16_t *)outbuf, &outSamples);
		outpcmbufsize = outSamples * 2 * sizeof(int16_t);

		if (outpcmbufsize == 0 && inbytesConsumed == 0) {
			// Nothing was output, hopefully we're at the end of the stream.
			AuBufAvailable = 0;
			sourcebuff.clear();
		} else {
			// Update our total decoded samples, but don't count stereo.
			SumDecodedSamples += outSamples;
			// get consumed source length
			int srcPos = inbytesConsumed + nextSync;
			// remove the consumed source
			if (srcPos > 0)
				sourcebuff.erase(sourcebuff.begin(), sourcebuff.begin() + srcPos);
			// reduce the available Aubuff size
			// (the available buff size is now used to know if we can read again from file and how many to read)
			AuBufAvailable -= srcPos;
		}
	}

	// Check again now that the decode has consumed more. The hardware rewinds in the same call that
	// decodes the last frame, so the sum reads back as zero right after it (audio/mp3/getsumdecoded).
	end = (int64_t)readPos - AuBufAvailable >= (int64_t)endPos;
	if (end && LoopNum != 0) {
		// When looping, start the sum back off at zero and reset readPos to the start.
		SumDecodedSamples = 0;
		readPos = startPos;
		if (LoopNum > 0)
			LoopNum--;
	}

	if (outpcmbufsize == 0 && !end) {
		// If we didn't decode anything, we fill this half of the buffer with zeros.
		outpcmbufsize = PCMBufSize / 2;
		if (outbuf != nullptr)
			memset(outbuf, 0, outpcmbufsize);
	} else if ((u32)outpcmbufsize < PCMBufSize) {
		// TODO: Not sure it actually zeros this out.
		if (outbuf != nullptr)
			memset(outbuf + outpcmbufsize, 0, PCMBufSize / 2 - outpcmbufsize);
	}

	if (outpcmbufsize != 0)
		NotifyMemInfo(MemBlockFlags::WRITE, outptr, outpcmbufsize, "AuDecode");

	nextOutputHalf ^= 1;
	return outpcmbufsize;
}

// return 1 to read more data stream, 0 don't read
int AuCtx::AuCheckStreamDataNeeded() {
	// If we would ask for bytes, then some are needed.
	if (AuStreamBytesNeeded() > 0) {
		return 1;
	}
	return 0;
}

int AuCtx::AuStreamBytesNeeded() {
	if (decoder->GetAudioType() == PSP_CODEC_MP3) {
		// The endPos and readPos are not considered, except when you've read to the end.
		// Compare signed: readPos is an int and can legitimately go negative (a game can notify
		// a negative size), and promoting that to u64 would make it look like the end of the
		// stream instead of what the hardware reports.
		if ((int64_t)readPos >= (int64_t)endPos)
			return 0;

		// The area after the workarea is double buffered: the game may write ahead up to the end
		// of the half that follows the one the decoder is currently reading from, so a half only
		// opens up once the decoder has consumed past its end. Decoding a single frame therefore
		// usually frees nothing at all, which is what the hardware reports (audio/mp3/checkneeded).
		// Games depend on it: Beats sleeps 50ms every time sceMp3CheckStreamDataNeeded() says it's
		// behind, so handing back the bytes each decode consumed made it sleep once per frame and
		// fall to less than half of realtime - badly stuttering custom soundtracks.
		//
		// Every case seen so far - the two hardware tests, Beats and Wipeout Pulse - passes the
		// minimum 8192 byte buffer, so the split being exactly half is unverified for anything
		// larger. If a game with a bigger buffer ever streams badly, suspect this first: the real
		// granularity could be a fixed chunk size rather than half of whatever it was given.
		int half = AuStreamHalfSize();
		if (half <= 0)
			return 0;
		int64_t written = (int64_t)readPos - (int64_t)startPos;
		int64_t consumed = written - AuBufAvailable;
		// Floor division - consumed can go negative if a game notifies a negative size.
		int64_t halvesDone = consumed / half - ((consumed % half < 0) ? 1 : 0);
		// Note that this is deliberately not clamped to the buffer size. The hardware reports
		// 6721 bytes to write for an 8192 byte buffer after notifying a size of -1.
		return (int)std::max((int64_t)0, (halvesDone + 2) * half - written);
	}

	// TODO: Untested.  Maybe similar to MP3.
	return std::min((int)AuBufSize - AuBufAvailable, (int)endPos - readPos);
}

int AuCtx::AuStreamWorkareaSize() {
	// Note that this is 31 bytes more than the max layer 3 frame size.
	if (decoder->GetAudioType() == PSP_CODEC_MP3)
		return 0x05c0;
	return 0;
}

// Size of each of the two halves the stream buffer is split into, after the workarea.
int AuCtx::AuStreamHalfSize() {
	return ((int)AuBufSize - AuStreamWorkareaSize()) / 2;
}

// Offset into the stream buffer (past the workarea) that the next added bytes go to. The write
// position simply walks the two halves in turn and wraps around, it doesn't follow the decoder.
int AuCtx::AuStreamWriteOffset() {
	int size = AuStreamHalfSize() * 2;
	if (size <= 0)
		return 0;
	int64_t pos = ((int64_t)readPos - (int64_t)startPos) % size;
	if (pos < 0)
		pos += size;
	return (int)pos;
}

// check how many bytes we have read from source file
u32 AuCtx::AuNotifyAddStreamData(int size) {
	int offset = AuStreamWorkareaSize();
	// Where AuGetInfoToAddStreamData pointed the game, i.e. where the bytes it just added start.
	// Has to be sampled before readPos moves on below.
	const int writeOffset = AuStreamWriteOffset();

	if (askedReadSize != 0) {
		// Old save state, numbers already adjusted.
		int diffsize = size - askedReadSize;
		// Notify the real read size
		if (diffsize != 0) {
			readPos += diffsize;
			AuBufAvailable += diffsize;
		}
		askedReadSize = 0;
	} else {
		readPos += size;
		AuBufAvailable += size;
	}

	// `size` is game-supplied and was previously trusted outright: a negative value
	// would make sourcebuff.resize() attempt a huge allocation (size_t underflow),
	// and an unbounded positive value would grow sourcebuff without limit (DoS).
	// The validated range also has to match what's actually read below - it was
	// checking [AuBuf, AuBuf+size) while the copy reads from [AuBuf+offset, ...).
	if (size > 0 && (int64_t)offset + writeOffset + size <= (int64_t)AuBufSize &&
		Memory::IsValidRange(AuBuf + offset + writeOffset, size)) {
		sourcebuff.resize(sourcebuff.size() + size);
		Memory::MemcpyUnchecked(&sourcebuff[sourcebuff.size() - size], AuBuf + offset + writeOffset, size);
	}

	return 0;
}

// read from stream position srcPos of size bytes into buff
// buff, size and srcPos are all pointers
u32 AuCtx::AuGetInfoToAddStreamData(u32 bufPtr, u32 sizePtr, u32 srcPosPtr) {
	int readsize = AuStreamBytesNeeded();
	int offset = AuStreamWorkareaSize();

	// The write position walks forward through the two halves as data is added and wraps around,
	// so point the game at that rather than at the start of the work area.
	if (readsize != 0) {
		if (Memory::IsValidAddress(bufPtr))
			Memory::WriteUnchecked_U32(AuBuf + offset + AuStreamWriteOffset(), bufPtr);
		if (Memory::IsValidAddress(sizePtr))
			Memory::WriteUnchecked_U32(readsize, sizePtr);
		if (Memory::IsValidAddress(srcPosPtr))
			Memory::WriteUnchecked_U32(readPos, srcPosPtr);
	} else {
		if (Memory::IsValidAddress(bufPtr))
			Memory::WriteUnchecked_U32(0, bufPtr);
		if (Memory::IsValidAddress(sizePtr))
			Memory::WriteUnchecked_U32(0, sizePtr);
		if (Memory::IsValidAddress(srcPosPtr))
			Memory::WriteUnchecked_U32(0, srcPosPtr);
	}

	// Just for old save states.
	askedReadSize = 0;
	return 0;
}

u32 AuCtx::AuResetPlayPositionByFrame(int frame) {
	// Note: this doesn't correctly handle padding or slot size, but the PSP doesn't either.
	uint32_t bytesPerSecond = (MaxOutputSample / 8) * BitRate * 1000;
	readPos = startPos + (frame * bytesPerSecond) / SamplingRate;
	// Not sure why, but it seems to consistently seek 1 before, maybe in case it's off slightly.
	if (frame != 0)
		readPos -= 1;
	SumDecodedSamples = frame * MaxOutputSample;
	AuBufAvailable = 0;
	sourcebuff.clear();
	return 0;
}

u32 AuCtx::AuResetPlayPosition() {
	readPos = startPos;
	SumDecodedSamples = 0;
	AuBufAvailable = 0;
	sourcebuff.clear();
	return 0;
}

void AuCtx::DoState(PointerWrap &p) {
	auto s = p.Section("AuContext", 0, 2);
	if (!s)
		return;

	Do(p, startPos);
	Do(p, endPos);
	Do(p, AuBuf);
	Do(p, AuBufSize);
	Do(p, PCMBuf);
	Do(p, PCMBufSize);
	Do(p, freq);
	Do(p, SumDecodedSamples);
	Do(p, LoopNum);
	Do(p, Channels);
	Do(p, MaxOutputSample);
	Do(p, readPos);
	int audioType = decoder ? (int)decoder->GetAudioType() : 0;
	Do(p, audioType);
	Do(p, BitRate);
	Do(p, SamplingRate);
	Do(p, askedReadSize);
	int dummy = 0;
	Do(p, dummy);
	Do(p, FrameNum);

	if (s < 2) {
		AuBufAvailable = 0;
		Version = 3;
	} else {
		Do(p, Version);
		Do(p, AuBufAvailable);
		Do(p, sourcebuff);
		Do(p, nextOutputHalf);
	}

	if (p.mode == p.MODE_READ) {
		decoder = CreateAudioDecoder((PSPAudioType)audioType);
	}
}
