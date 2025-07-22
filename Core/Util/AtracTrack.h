#pragma once

#include "Common/CommonTypes.h"

#include <vector>
#include <string>

#include "Core/HLE/sceAudiocodec.h"
#include "Core/HLE/AtracBase.h"

struct AtracLoopInfo {
	int cuePointID;
	int type;
	int startSample;
	int endSample;
	int fraction;
	int playCount;
};

// This is (mostly) constant info, once a track has been loaded.
struct Track {
	// This both does and doesn't belong in Track - it's fixed for an Atrac instance. Oh well.
	u32 codecType = 0;

	// Size of the full track being streamed or played. Can be a lot larger than the in-memory buffer in the streaming modes.
	u32 fileSize = 0;

	// Not really used for much except queries, this keeps track of the bitrate of the track (kbps).
	u32 bitrate = 64;

	// Signifies whether to use a more efficient coding mode with less stereo separation. For our purposes, just metadata,
	// not actually used in decoding.
	int jointStereo = 0;

	// Number of audio channels in the track.
	u16 channels = 2;

	// The size of an encoded frame in bytes.
	u16 bytesPerFrame = 0;

	// Byte offset of the first encoded frame in the input buffer. Note: Some samples may be skipped according to firstSampleOffset.
	int dataByteOffset = 0;

	// How many samples to skip from the beginning of a track when decoding.
	// Actually, the real number is this added to FirstOffsetExtra(codecType). You can call
	// FirstSampleOffset2() to get that.
	// Some use of these offsets around the code seem to be inconsistent, sometimes the extra is included,
	// sometimes not.
	int firstSampleOffset = 0;

	// Last sample number. Inclusive. Though, we made it so that in Analyze, it's exclusive in the file.
	// Does not take firstSampleOffset into account.
	int endSample = -1;

	// NOTE: The below CAN be written.
	// Loop configuration. The PSP only supports one loop but we store them all.
	std::vector<AtracLoopInfo> loopinfo;
	// The actual used loop offsets. These appear to be raw offsets, not taking FirstSampleOffset2() into account.
	int loopStartSample = -1;
	int loopEndSample = -1;

	// Input frame size
	int BytesPerFrame() const {
		return bytesPerFrame;
	}

	inline int FirstOffsetExtra() const {
		// These first samples are skipped, after first possibly skipping 0-2 full frames, it seems.
		return codecType == PSP_CODEC_AT3PLUS ? 0x170 : 0x45;
	}

	// Includes the extra offset. See firstSampleOffset comment above.
	int FirstSampleOffsetFull() const {
		return FirstOffsetExtra() + firstSampleOffset;
	}

	// Output frame size, different between the two supported codecs.
	int SamplesPerFrame() const {
		return codecType == PSP_CODEC_AT3PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES;
	}

	int Bitrate() const {
		int bitrate = (bytesPerFrame * 352800) / 1000;
		if (codecType == PSP_CODEC_AT3PLUS)
			bitrate = ((bitrate >> 11) + 8) & 0xFFFFFFF0;
		else
			bitrate = (bitrate + 511) >> 10;
		return bitrate;
	}

	// This appears to be buggy, should probably include FirstOffsetExtra?
	// Actually the units don't even make sense here.
	int DecodePosBySample(int sample) const {
		return (u32)(firstSampleOffset + sample / (int)SamplesPerFrame() * bytesPerFrame);
	}

	// This appears to be buggy, should probably include FirstOffsetExtra?
	int FileOffsetBySample(int sample) const {
		int offsetSample = sample + firstSampleOffset;
		int frameOffset = offsetSample / (int)SamplesPerFrame();
		return (u32)(dataByteOffset + bytesPerFrame + frameOffset * bytesPerFrame);
	}

	void DebugLog() const;
};

int AnalyzeAA3Track(const u8 *buffer, u32 size, u32 filesize, Track *track, std::string *error);
int AnalyzeAtracTrack(const u8 *buffer, u32 size, Track *track, std::string *error);

struct TrackInfo {
	u16 numChans;
	u16 blockAlign;
	u8 sampleSizeMaybe;
	u8 tailFlag;
	u8 unused[2];
	u32 dataOff;
	u32 endSample;
	u32 waveDataSize;
	u32 firstSampleOffset;
	u32 loopStart;
	u32 loopEnd;
};

int ParseWaveAT3(const u8 *data, int length, TrackInfo *track);
int ParseAA3(const u8 *data, int readSize, int fileSize, TrackInfo *track);
