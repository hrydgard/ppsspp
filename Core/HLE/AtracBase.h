#pragma once

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

#include "Core/MemMap.h"
#include "Core/HLE/sceAudiocodec.h"

constexpr u32 ATRAC3_MAX_SAMPLES = 0x400;  // 1024
constexpr u32 ATRAC3PLUS_MAX_SAMPLES = 0x800;   // 2048

// The "state" member of SceAtracIdInfo.
enum AtracStatus : u8 {
	ATRAC_STATUS_UNINITIALIZED = 0,  // bad state

	ATRAC_STATUS_NO_DATA = 1,

	// The entire file is loaded into memory, no further file access needed.
	ATRAC_STATUS_ALL_DATA_LOADED = 2,

	// The buffer is sized to fit the entire file, but it's only partially loaded, so you can start playback before loading the whole file.
	ATRAC_STATUS_HALFWAY_BUFFER = 3,

	// In these ones, the buffer is smaller than the file, and data is streamed into it as needed for playback.
	// These are the most complex modes, both to implement and use.
	ATRAC_STATUS_STREAMED_WITHOUT_LOOP = 4,
	ATRAC_STATUS_STREAMED_LOOP_FROM_END = 5,
	// This means there's additional audio after the loop.
	// i.e. ~~before loop~~ [ ~~this part loops~~ ] ~~after loop~~
	// The "fork in the road" means a second buffer is needed for the second path.
	ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER = 6,

	// In this mode, the only API to call is sceAtracLowLevelDecode, which decodes a stream packet by packet without any other metadata.
	ATRAC_STATUS_LOW_LEVEL = 8,

	// This mode is for using an Atrac context as the audio source for an sceSas channel. Not used a lot (Sol Trigger).
	ATRAC_STATUS_FOR_SCESAS = 16,

	// Bitwise-and the status with this to check for any of the streaming modes in a single test.
	ATRAC_STATUS_STREAMED_MASK = 4,
};

const char *AtracStatusToString(AtracStatus status);

inline bool AtracStatusIsStreaming(AtracStatus status) {
	return (status & ATRAC_STATUS_STREAMED_MASK) != 0;
}
inline bool AtracStatusIsNormal(AtracStatus status) {
	return (int)status >= ATRAC_STATUS_ALL_DATA_LOADED && (int)status <= ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
}

struct SceAtracIdInfo {
	s32 decodePos;        // Sample position in the song that we'll next be decoding from.
	s32 endSample;        // Last sample index of the track.
	s32 loopStart;        // Start of the loop (sample index)
	s32 loopEnd;          // End of the loop (sample index)
	s32 firstValidSample; // Seems to be the number of skipped samples at the start. After SetID, decodePos will match this. Was previously misnamed 'samplesPerChan'.
	u8 numSkipFrames;      // This is 1 for a single frame when a loop is triggered, otherwise seems to stay at 0. Likely mis-named.
	AtracStatus state;    // State enum, see AtracStatus.
	u8 curBuffer;         // Current buffer (1 == second, 2 == done?) Previously unk
	u8 numChan;           // Number of audio channels, usually 2 but 1 is possible.
	u16 sampleSize;       // Size in bytes of an encoded audio frame.
	u16 codec;            // Codec. 0x1000 is Atrac3+, 0x1001 is Atrac3. See the PSP_CODEC_ enum (only these two are supported).
	s32 dataOff;          // File offset in bytes where the Atrac3+ frames start appearing. The first dummy packet starts here.
	s32 curFileOff;       // File offset in bytes corresponding to the start of next packet that will be *decoded* (on the next call to sceAtracDecodeData).
	s32 fileDataEnd;      // File size in bytes.
	s32 loopNum;          // Current loop counter. If 0, will not loop. -1 loops for ever, positive numbers get decremented on the loop end. So to play a song 3 times and then end, set this to 2.
	s32 streamDataByte;   // Number of bytes of queued/buffered/uploaded data. In full and half-way modes, this isn't decremented as you decode.
	s32 streamOff;        // Streaming modes only: The byte offset inside the RAM buffer where sceAtracDecodeData will read from next. ONLY points to even packet boundaries.
	s32 secondStreamOff;  // A kind of stream position in the secondary buffer.
	u32 buffer;           // Address in RAM of the main buffer.
	u32 secondBuffer;     // Address in RAM of the second buffer, or 0 if not used.
	u32 bufferByte;       // Size in bytes of the main buffer.
	u32 secondBufferByte; // Size in bytes of the second buffer.
	// Offset 72 here.
	// make sure the size is 128
	u32 unk[14];

	// Simple helpers. Similar ones are on track_, but we shouldn't need track_ anymore when playing back.

	int SamplesPerFrame() const {
		return codec == 0x1000 ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES;
	}
	int SamplesFrameMask() const {
		return SamplesPerFrame() - 1;
	}
	int SkipSamples() const {
		// These first samples are skipped, after first possibly skipping 0-2 full frames, it seems.
		return codec == 0x1000 ? 0x170 : 0x45;
	}
	int BitRate() const {
		int bitrate = (sampleSize * 352800) / 1000;
		if (codec == PSP_CODEC_AT3PLUS) {
			bitrate = ((bitrate >> 11) + 8) & 0xFFFFFFF0;
		} else {
			bitrate = (bitrate + 511) >> 10;
		}
		return bitrate;
	}
};

// One of these structs is allocated for each Atrac context.
// The raw codec state is stored in 'codec'.
// The internal playback state is stored in 'info', and that is used for all state keeping in the Atrac2 implementation,
// imitating what happens on hardware as closely as possible.
struct SceAtracContext {
	// size 128
	SceAudiocodecCodec codec;
	// size 128
	SceAtracIdInfo info;
};

struct Atrac3LowLevelParams {
	int encodedChannels;
	int outputChannels;
	int bytesPerFrame;
};

struct AtracSingleResetBufferInfo {
	u32_le writePosPtr;
	u32_le writableBytes;
	u32_le minWriteBytes;
	u32_le filePos;
};

struct AtracResetBufferInfo {
	AtracSingleResetBufferInfo first;
	AtracSingleResetBufferInfo second;
};

struct AtracSasStreamState {
	u32 bufPtr[2]{};
	u32 bufSize[2]{};
	int streamOffset = 0;
	int fileOffset = 0;
	int curBuffer = 0;
	bool isStreaming = false;

	int CurPos() const {
		int retval = fileOffset - bufSize[curBuffer] + streamOffset;
		_dbg_assert_(retval >= 0);
		return retval;
	}
};

const int PSP_ATRAC_ALLDATA_IS_ON_MEMORY = -1;
const int PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY = -2;
const int PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY = -3;

// This is not a PSP-native struct.
// But, it's stored in its entirety in savestates, which makes it awkward to change it.
// This is used for both first_ and second_, but the latter doesn't use all the fields.
struct InputBuffer {
	// Address of the buffer.
	u32 addr;
	// Size of data read so far into dataBuf_ (to be removed.)
	u32 size;
	// Offset into addr at which new data is added.
	u32 offset;
	// Last writableBytes number (to be removed.)
	u32 writableBytes;
	// Unused, always 0.
	u32 neededBytes;
	// Total size of the entire file data.
	u32 _filesize_dontuse;
	// Offset into the file at which new data is read.
	u32 fileoffset;
};

class AudioDecoder;
class PointerWrap;
struct Track;

class AtracBase {
public:
	virtual ~AtracBase();

	virtual void DoState(PointerWrap &p) = 0;

	// TODO: Find a way to get rid of this from the base class.
	virtual void UpdateContextFromPSPMem() = 0;

	virtual int Channels() const = 0;

	int GetOutputChannels() const {
		return outputChannels_;
	}
	void SetOutputChannels(int channels) {
		// Only used for sceSas audio. To be refactored away in the future.
		outputChannels_ = channels;
	}

	virtual u32 GetInternalCodecError() const { return 0; }

	PSPPointer<SceAtracContext> context_{};

	virtual AtracStatus BufferState() const = 0;

	virtual int SetLoopNum(int loopNum) = 0;
	virtual int LoopNum() const = 0;
	virtual int LoopStatus() const = 0;

	virtual int CodecType() const = 0;

	AudioDecoder *Decoder() const {
		return decoder_;
	}

	void CreateDecoder(int codecType, int bytesPerFrame, int channels);

	virtual void NotifyGetContextAddress() = 0;

	virtual int GetNextDecodePosition(int *pos) const = 0;
	virtual int RemainingFrames() const = 0;
	virtual bool HasSecondBuffer() const = 0;
	virtual int Bitrate() const = 0;
	virtual int BytesPerFrame() const = 0;
	virtual int SamplesPerFrame() const = 0;

	virtual void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) = 0;  // This should be const, but the legacy impl stops it (it's wrong).
	virtual int AddStreamData(u32 bytesToAdd) = 0;
	virtual int ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) = 0;
	virtual int GetBufferInfoForResetting(AtracResetBufferInfo *bufferInfo, int sample, bool *delay) = 0;  // NOTE: Not const! This can cause SkipFrames!
	virtual int SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, u32 fileSize, int outputChannels, bool isAA3) = 0;

	virtual int GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) const = 0;
	virtual int SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) = 0;
	virtual u32 DecodeData(u8 *outbuf, u32 outbufPtr, int *SamplesNum, int *finish, int *remains) = 0;
	virtual int DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) = 0;

	virtual u32 GetNextSamples() = 0;  // This should be const, but the legacy impl stops it (it's wrong).
	virtual void InitLowLevel(const Atrac3LowLevelParams &params, int codecType) = 0;

	virtual void CheckForSas() = 0;
	virtual int EnqueueForSas(u32 address, u32 ptr) = 0;
	virtual void DecodeForSas(s16 *dstData, int *bytesWritten, int *finish) = 0;
	virtual const AtracSasStreamState *StreamStateForSas() const { return nullptr; }

	virtual int GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const = 0;

	virtual int GetContextVersion() const = 0;

protected:
	u16 outputChannels_ = 2;

	// TODO: Save the internal state of this, now technically possible.
	AudioDecoder *decoder_ = nullptr;
};
